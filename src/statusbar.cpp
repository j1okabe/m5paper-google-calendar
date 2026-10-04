#include "statusbar.h"

#include <M5Unified.h>

namespace
{
    const int32_t JST_OFFSET_HOURS = 9;
    const char *WEEKDAYS[] = {"日", "月", "火", "水", "木", "金", "土"};

    bool isLeapYear(int year) {
        return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    }

    int daysInMonth(int year, int month) {
        static const int DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month == 2 && isLeapYear(year)) {
            return 29;
        }
        return DAYS[month - 1];
    }

    bool isValidDateTime(const m5::rtc_datetime_t &datetime) {
        if (datetime.date.year < 2024 || datetime.date.month < 1 || datetime.date.month > 12) {
            return false;
        }
        if (datetime.date.date < 1 || datetime.date.date > daysInMonth(datetime.date.year, datetime.date.month)) {
            return false;
        }
        if (datetime.date.weekDay < 0 || datetime.date.weekDay > 6) {
            return false;
        }
        return datetime.time.hours >= 0 && datetime.time.hours < 24 &&
               datetime.time.minutes >= 0 && datetime.time.minutes < 60 &&
               datetime.time.seconds >= 0 && datetime.time.seconds < 60;
    }

    void addJstOffset(m5::rtc_datetime_t *datetime) {
        datetime->time.hours += JST_OFFSET_HOURS;
        if (datetime->time.hours < 24) {
            return;
        }

        datetime->time.hours -= 24;
        datetime->date.date++;
        datetime->date.weekDay = (datetime->date.weekDay + 1) % 7;

        if (datetime->date.date <= daysInMonth(datetime->date.year, datetime->date.month)) {
            return;
        }

        datetime->date.date = 1;
        datetime->date.month++;
        if (datetime->date.month <= 12) {
            return;
        }

        datetime->date.month = 1;
        datetime->date.year++;
    }
}

void StatusBar::draw(M5Canvas *canvas, int32_t width, int32_t *batt) {

    // bar
    canvas->drawFastHLine(0, StatusBar::height() - 1, width, TFT_BLACK);
    canvas->drawFastHLine(0, StatusBar::height() - 2, width, TFT_BLACK);
    canvas->drawFastHLine(0, StatusBar::height() - 3, width, TFT_BLACK);


    // battery percentage calculation from
    // https://github.com/m5stack/M5Paper_FactoryTest/blob/ef8d1ff94490a9364479231d6ba7e343d9adaa06/src/frame/frame_main.cpp#L272
    // int32_t batteryLevel = M5.Power.getBatteryLevel();
    // int32_t vol = M5.Power.getBatteryVoltage();

    // if (batteryLevel < 0) {
    //     if (vol < 3300) {
    //         vol = 3300;
    //     }
    //     else if (vol > 4350) {
    //         vol = 4350;
    //     }
    //     batteryLevel = (vol - 3300) * 100 / (4350 - 3300);
    // }
    // if (batt != nullptr) {
    //     *batt = batteryLevel;
    // }

    const int32_t margin_top = 6;
    // const int32_t battery_width = 170;
    // char battString[32];
    // snprintf(battString, sizeof(battString), "batt: %ld %%", batteryLevel);
    // canvas->drawString(battString, width - battery_width, margin_top);

    // RTC time stays UTC; only the displayed string is JST.
    m5::rtc_datetime_t displayTime;
    if (!M5.Rtc.getDateTime(&displayTime) || !isValidDateTime(displayTime)) {
        Serial.println("RTC time is invalid");
        canvas->drawString("time: --", 5, margin_top);
        return;
    }
    addJstOffset(&displayTime);

    char buf[36];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d%s %02d:%02d:%02d",
             displayTime.date.year,
             displayTime.date.month,
             displayTime.date.date,
             WEEKDAYS[displayTime.date.weekDay],
             displayTime.time.hours,
             displayTime.time.minutes,
             displayTime.time.seconds);
    canvas->drawString(buf, 5, margin_top);
}

int StatusBar::height() {
    return 42;
}
