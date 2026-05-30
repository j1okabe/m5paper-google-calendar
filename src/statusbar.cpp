#include "statusbar.h"

#include <M5Unified.h>
#include <time.h>

namespace
{
    const time_t JST_OFFSET_SECONDS = 9 * 60 * 60;
    const char *WEEKDAYS[] = {"日", "月", "火", "水", "木", "金", "土"};
}

void StatusBar::draw(M5Canvas *canvas, int32_t width) {

    // bar
    canvas->drawFastHLine(0, StatusBar::height() - 1, width, TFT_BLACK);
    canvas->drawFastHLine(0, StatusBar::height() - 2, width, TFT_BLACK);
    canvas->drawFastHLine(0, StatusBar::height() - 3, width, TFT_BLACK);


    // battery percentage calculation from
    // https://github.com/m5stack/M5Paper_FactoryTest/blob/ef8d1ff94490a9364479231d6ba7e343d9adaa06/src/frame/frame_main.cpp#L272
    int32_t batteryLevel = M5.Power.getBatteryLevel();
    int32_t vol = M5.Power.getBatteryVoltage();

    if (batteryLevel < 0) {
        if (vol < 3300) {
            vol = 3300;
        }
        else if (vol > 4350) {
            vol = 4350;
        }
        batteryLevel = (vol - 3300) * 100 / (4350 - 3300);
    }

    const int32_t margin_top = 6;
    const int32_t battery_width = 170;
    char battString[32];
    snprintf(battString, sizeof(battString), "batt: %ld %%", batteryLevel);
    canvas->drawString(battString, width - battery_width, margin_top);

    // time string (RTC/system time stays UTC; only the displayed string is JST)
    time_t now = time(nullptr);
    if (now <= 0) {
        Serial.println("time() failed");
        canvas->drawString("time: --", 5, margin_top);
        return;
    }
    time_t jst = now + JST_OFFSET_SECONDS;
    struct tm displayTime = {0};
    gmtime_r(&jst, &displayTime);

    char buf[36];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d%s %02d:%02d:%02d",
             displayTime.tm_year + 1900,
             displayTime.tm_mon + 1,
             displayTime.tm_mday,
             WEEKDAYS[displayTime.tm_wday],
             displayTime.tm_hour,
             displayTime.tm_min,
             displayTime.tm_sec);
    canvas->drawString(buf, 5, margin_top);
}

int StatusBar::height() {
    return 42;
}
