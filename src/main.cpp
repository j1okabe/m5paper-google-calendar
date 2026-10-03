#include <Arduino.h>
#include <M5GFX.h>
#include <M5Unified.h>
#include <ArduinoJson.h>
#include <apps/esp_sntp.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "mywifi.h"
#include "statusbar.h"
#include "google/auth.h"
#include "google/calendar.h"

#define FNT28 &fonts::lgfxJapanGothic_28
#define FNT24 &fonts::lgfxJapanGothic_24

const char *NTP_SERVER = "ntp.nict.jp";
const char *NTP_SERVER2 = "1.pool.ntp.org";
const char *NTP_SERVER3 = "2.pool.ntp.org";
const char *TIME_ZONE = "JST-9";
#define SyncIntervalSec (60 * 60 * 2) // 2時間に1回更新
void generateCalendarStartEnd(struct tm *start, struct tm *end);
void drawWrappedText(const String &text, int32_t x, int32_t y, int32_t lineHeight, int maxCharsPerLine, int maxLines);
bool syncTime();
bool shouldSyncTime();
bool isUsbPowered();
void sleepUntilNextSync();
void onTimeSynced(struct timeval *tv);

M5Canvas canvas(&M5.Display);
volatile bool timeSynced = false;
int32_t batt = 0;
namespace
{
    const uint64_t MICROSECONDS_PER_SECOND = 1000000ULL;
    const int16_t USB_POWERED_VBUS_MV = 4400;
    const uint8_t M5PAPER_S3_USB_DET_PIN = 5;
    const int32_t EVENT_MARGIN_TOP = 12;
    const int32_t EVENT_TIME_X = 10;
    const int32_t EVENT_TITLE_X = 30;
    const int32_t EVENT_TIME_TO_TITLE_GAP_FNT28 = 34;
    const int32_t EVENT_TITLE_TO_LINE_GAP_FNT28 = 36;
    const int32_t EVENT_LINE_TO_NEXT_GAP_FNT28 = 6;
    const int32_t EVENT_TIME_TO_TITLE_GAP_FNT24 = 29;
    const int32_t EVENT_TITLE_TO_LINE_GAP_FNT24 = 30;
    const int32_t EVENT_LINE_TO_NEXT_GAP_FNT24 = 6;
}

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    // portrait mode
    M5.Display.setRotation(0);
    M5.Display.setEpdMode(epd_quality);
    M5.Display.clear(TFT_WHITE);

    const int32_t displayWidth = M5.Display.width();
    const int32_t displayHeight = M5.Display.height();

    canvas.setPsram(true);
    canvas.setColorDepth(16);
    if (canvas.createSprite(displayWidth, displayHeight) == nullptr)
    {
        M5.Display.drawString("canvas create failed", 0, displayHeight / 2);
        return;
    }
    canvas.fillSprite(TFT_WHITE);
    canvas.setFont(FNT28);
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_BLACK, TFT_WHITE);
    canvas.setTextDatum(top_left);

    bool connected = MYWIFI::connect(config::WIFI_SSID, config::WIFI_PASSWORD, 10);
    if (!connected)
    {
        canvas.drawString("wifi connect failed", 0, displayHeight / 2);
        canvas.pushSprite(0, 0);
        return;
    }
    setenv("TZ", TIME_ZONE, 1);
    tzset();
    M5.Rtc.setSystemTimeFromRtc();
    tzset();

    // 1日に一回の更新を行う。JSTの0時から1:59、またはRTC時刻が無効な場合だけNTP同期する。
    if (shouldSyncTime())
    {
        Serial.println("sync time because it's between 0:00 and 1:59");
        if (!syncTime())
        {
            canvas.drawString("time sync failed", 0, displayHeight / 2);
            canvas.pushSprite(0, 0);
            MYWIFI::disconnect();
            return;
        }
    }

    // draw status bar
    StatusBar::draw(&canvas, displayWidth, &batt);
    // get access token
    Serial.print("before auth");
    DynamicJsonDocument doc = GoogleAuthorization::getAccessToken(config::GOOGLE_REFRESH_TOKEN);
    const char *accessToken = doc["access_token"];
    if (accessToken == NULL || strcmp(accessToken, "") == 0)
    {
        const char *error = doc["error"] | "unknown_error";
        const char *errorDescription = doc["error_description"] | "failed to get access token";
        Serial.print("Google auth failed: ");
        Serial.print(error);
        Serial.print(" - ");
        Serial.println(errorDescription);
        canvas.drawString("google auth failed", EVENT_TIME_X, StatusBar::height() + EVENT_MARGIN_TOP);
        canvas.pushSprite(0, 0);
        MYWIFI::disconnect();
        return;
    }
    Serial.println("access token received");

    // calendar list
    // auto calendarList = GoogleCalendar::getCalendars(accessToken);
    // for (int i = 0 ; i < calendarList.size() ; i++) {
    //    Serial.println(calendarList[i].id);
    //}
    //// use fierst calendar id
    // String showCalendarID = calendarList[0].id;

    // get events
    struct tm start = {0};
    struct tm end = {0};
    generateCalendarStartEnd(&start, &end);
    const char *calendarIds[] = {
        config::GOOGLE_CALENDAR_ID,
        config::GOOGLE_CALENDAR_ID_2,
    };
    GoogleCalendarEventList *events = GoogleCalendar::getEvents(accessToken, calendarIds, 2, &start, &end);
    if (events == NULL)
    {
        canvas.drawString("calendar fetch failed", EVENT_TIME_X, StatusBar::height() + EVENT_MARGIN_TOP);
        canvas.setFont(FNT24);
        drawWrappedText(GoogleCalendar::lastFetchError(),
                        EVENT_TIME_X,
                        StatusBar::height() + EVENT_MARGIN_TOP + EVENT_TITLE_TO_LINE_GAP_FNT28,
                        EVENT_TITLE_TO_LINE_GAP_FNT24,
                        30,
                        6);
        canvas.pushSprite(0, 0);
        MYWIFI::disconnect();
        return;
    }

    const int eventCount = events->length();
    int32_t timeToTitleGap;
    int32_t titleToLineGap;
    int32_t lineToNextGap;
    if (eventCount >= 12)
    {
        canvas.setFont(FNT24);
        timeToTitleGap = EVENT_TIME_TO_TITLE_GAP_FNT24;
        titleToLineGap = EVENT_TITLE_TO_LINE_GAP_FNT24;
        lineToNextGap = EVENT_LINE_TO_NEXT_GAP_FNT24;
    }
    else
    {
        canvas.setFont(FNT28);
        timeToTitleGap = EVENT_TIME_TO_TITLE_GAP_FNT28;
        titleToLineGap = EVENT_TITLE_TO_LINE_GAP_FNT28;
        lineToNextGap = EVENT_LINE_TO_NEXT_GAP_FNT28;
    }

    int dy = StatusBar::height() + EVENT_MARGIN_TOP;
    for (int i = 0; i < eventCount; i++)
    {
        auto *event = events->get(i);
        const char *summary = event->summary();
        const char *displaySummary = (summary != NULL && strcmp(summary, "") != 0) ? summary : "Hidden";
        Serial.println("---- ---- ----");
        Serial.println(displaySummary);
        // time
        if (event->isPeriod())
        {
            // 期間予定
            canvas.drawString(event->startEndDatePeriodString(), EVENT_TIME_X, dy);
        }
        else
        {
            // 通常予定
            canvas.drawString(event->startEndDateTimePeriodString(), EVENT_TIME_X, dy);
        }
        dy += timeToTitleGap;
        // title
        canvas.drawString(displaySummary, EVENT_TITLE_X, dy);
        dy += titleToLineGap;
        canvas.drawFastHLine(0, dy, displayWidth, TFT_BLACK);
        dy += lineToNextGap;
    }
    if (eventCount == 0)
    {
        canvas.drawString("NO schedules", EVENT_TIME_X, dy);
    }
    delete events;

    canvas.pushSprite(0, 0);
    MYWIFI::disconnect();
    Serial.println(" now time ");
    int utime = time(NULL);
    Serial.println(String("") + utime);
}

void loop()
{
    // need delay because drawing canvas is too slow
    delay(1000);
    if(batt < 4){
        // show low battery warning at bottom of screen
        canvas.drawString("Battery low, please charge", 10, M5.Display.height() - 40);
        canvas.pushSprite(0, 0);
        delay(5000);
        // power off to prevent battery damage
        M5.Power.powerOff();
    }else{
        // sleep until next sync
        sleepUntilNextSync();
    }
    // sleepUntilNextSync();
}

bool isUsbPowered()
{
    int16_t vbusVoltage = M5.Power.getVBUSVoltage();
    if (vbusVoltage >= USB_POWERED_VBUS_MV)
    {
        return true;
    }

    if (M5.Power.isCharging() == m5::Power_Class::is_charging)
    {
        return true;
    }

    if (M5.getBoard() == m5::board_t::board_M5PaperS3)
    {
        pinMode(M5PAPER_S3_USB_DET_PIN, INPUT);
        return digitalRead(M5PAPER_S3_USB_DET_PIN) == HIGH;
    }

    return false;
}

void sleepUntilNextSync()
{
    const bool usbPowered = isUsbPowered();
    Serial.printf("usb powered: %s\n", usbPowered ? "yes" : "no");

    if (usbPowered)
    {
        Serial.println("USB powered: use ESP32 deep sleep timer for wakeup");
        Serial.flush();
        M5.Power.deepSleep(SyncIntervalSec * MICROSECONDS_PER_SECOND, false);
        return;
    }

    Serial.println("Battery powered: use M5.Power.timerSleep for wakeup");

    // restart after SyncIntervalSec
    // 2時間後のtimeを計算してtimerSleepする
    time_t now = time(nullptr);
    if (now <= 0)
    {
        M5.Power.timerSleep(SyncIntervalSec);
        return;
    }

    time_t nextSyncTime = now + SyncIntervalSec;
    m5::rtc_time_t wakeTime;
    wakeTime.hours = (nextSyncTime / 3600) % 24;
    wakeTime.minutes = (nextSyncTime / 60) % 60;
    wakeTime.seconds = nextSyncTime % 60;
    M5.Power.timerSleep(wakeTime);
}

void generateCalendarStartEnd(struct tm *start, struct tm *end)
{
    struct tm now;
    if (!getLocalTime(&now))
    {
        Serial.println("getLocalTime() failed in generateCalendarStartEnd");
        memset(start, 0, sizeof(struct tm));
        memset(end, 0, sizeof(struct tm));
        return;
    }

    // start 現時刻の１時間前
    *start = now;
    start->tm_sec = 0;
    start->tm_min = 0;
    start->tm_hour = now.tm_hour - 1 < 0 ? 0 : now.tm_hour - 1;

    // 48時間後を作る
    time_t tend = mktime(start);
    tend += 60 * 60 * 48;
    localtime_r(&tend, end);

    end->tm_sec = 59;
    end->tm_min = 59;
    end->tm_hour = 23;
}

void drawWrappedText(const String &text, int32_t x, int32_t y, int32_t lineHeight, int maxCharsPerLine, int maxLines)
{
    if (text.length() == 0)
    {
        return;
    }

    int line = 0;
    int pos = 0;
    while (pos < text.length() && line < maxLines)
    {
        String part = text.substring(pos, pos + maxCharsPerLine);
        canvas.drawString(part, x, y + lineHeight * line);
        pos += maxCharsPerLine;
        line++;
    }
}

bool syncTime()
{
    timeSynced = false;
    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    sntp_set_time_sync_notification_cb(onTimeSynced);
    configTzTime(TIME_ZONE, NTP_SERVER, NTP_SERVER2, NTP_SERVER3);

    struct tm now = {0};
    for (int i = 0; i < 20; i++)
    {
        delay(500);
        if (timeSynced || sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED)
        {
            if (!getLocalTime(&now, 100))
            {
                Serial.println("NTP synced, but getLocalTime() failed");
                return false;
            }
            Serial.printf("time synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                          now.tm_year + 1900,
                          now.tm_mon + 1,
                          now.tm_mday,
                          now.tm_hour,
                          now.tm_min,
                          now.tm_sec);
            time_t t = time(nullptr) + 1; // Advance one second.
            while (t > time(nullptr))
                ; /// Synchronization in seconds
            M5.Rtc.setDateTime(gmtime(&t));
            return true;
        }
        Serial.println("waiting for NTP sync...");
    }

    return false;
}

bool shouldSyncTime()
{
    if (M5.Rtc.getVoltLow())
    {
        Serial.println("RTC voltage low; sync time");
        return true;
    }

    struct tm now = {0};
    if (!getLocalTime(&now, 100))
    {
        Serial.println("RTC time is invalid; sync time");
        return true;
    }

    const int currentYear = now.tm_year + 1900;
    if (currentYear < 2024)
    {
        Serial.printf("RTC year is invalid: %d; sync time\n", currentYear);
        return true;
    }

    Serial.printf("current local time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  currentYear,
                  now.tm_mon + 1,
                  now.tm_mday,
                  now.tm_hour,
                  now.tm_min,
                  now.tm_sec);
    return now.tm_hour == 0 || now.tm_hour == 1;
}

void onTimeSynced(struct timeval *tv)
{
    timeSynced = true;
}
