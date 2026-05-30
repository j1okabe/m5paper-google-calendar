#include "calendar.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ArduinoJson.h>
#include "../http/http.h"

const int MAX_EVENT_COUNT = 20;
const int JST_OFFSET_SECONDS = 60 * 60 * 9;
const char *GOOGLE_CALENDAR_LIST = "https://www.googleapis.com/calendar/v3/users/me/calendarList";
const char *GOOGLE_CALENDAR_EVENT_LIST_PREFIX = "https://www.googleapis.com/calendar/v3/calendars/"; // + "/calendarId/events"
const char *GOOGLE_CALENDAR_EVENT_LIST_POSTFIX = "/events";
const char *WEEKDAYS[] = {"日", "月", "火", "水", "木", "金", "土"};
static String lastCalendarFetchError = "";
String timeToRFC3339(struct tm *tm) ;
String formatLocalDateTimePeriod(const char *start, const char *end);
String formatLocalTimePeriod(const char *start, const char *end);
String formatLocalDateTime(const char *datetime, const char *format);
String formatLocalDate(const char *date);
String formatDateWithWeekday(struct tm *date);
bool parseRFC3339ToEpoch(const char *datetime, time_t *epoch);
bool parseDateToEpoch(const char *date, time_t *epoch);
bool eventStartToEpoch(GoogleCalendarEvent *event, time_t *epoch);
int parseTwoDigits(const char *value);
time_t epochFromUtcComponents(int year, int month, int day, int hour, int minute, int second);
int64_t daysFromCivil(int year, unsigned month, unsigned day);
void copyOrEmpty(char *dest, size_t destSize, const char *src);

GoogleCalendarEvent::GoogleCalendarEvent(const char *summary, const char *start, const char *end, bool isPeriod) {
    copyOrEmpty(_summary, sizeof(_summary), summary);
    copyOrEmpty(_start, sizeof(_start), start);
    copyOrEmpty(_end, sizeof(_end), end);
    _isPeriod = isPeriod;
}
bool GoogleCalendarEvent::isPeriod() {
    return _isPeriod;
}
const char *GoogleCalendarEvent::summary() {
    return _summary;
}
const char *GoogleCalendarEvent::start() {
    return _start;
}
const char *GoogleCalendarEvent::end() {
    return _end;
}

// YYYY-MM-DD hh:mm - hh:mm
String GoogleCalendarEvent::startEndDateTimePeriodString() {
    return formatLocalDateTimePeriod(_start, _end);
}

// YYYY-MM-DD - YYYY-MM-DD
// only if isPeriod() == true
String GoogleCalendarEvent::startEndDatePeriodString() {
    return formatLocalDate(_start) + " - " + formatLocalDate(_end);
}

// hh:mm - hh:mm
String GoogleCalendarEvent::startEndTimePeriodString() {
    return formatLocalTimePeriod(_start, _end);
}


GoogleCalendarEventList::GoogleCalendarEventList(int maxLength) : _length(0), _maxLength(maxLength) {
    _events = new GoogleCalendarEvent*[maxLength];
}
GoogleCalendarEventList::~GoogleCalendarEventList() {
    for (int i = 0; i < _length; i++) {
        delete _events[i];
    }
    delete[] _events;
}
bool GoogleCalendarEventList::add(GoogleCalendarEvent &event) {
    if (_length == _maxLength) {
        return false;
    }
    _events[_length] = new GoogleCalendarEvent(event.summary(), event.start(), event.end(), event.isPeriod());
    _length++;
    return true;
}
GoogleCalendarEvent *GoogleCalendarEventList::get (int pos) const {
    return _events[pos];
}
void GoogleCalendarEventList::sortByStartTime() {
    for (int i = 0; i < _length - 1; i++) {
        for (int j = i + 1; j < _length; j++) {
            time_t left = 0;
            time_t right = 0;
            bool leftParsed = eventStartToEpoch(_events[i], &left);
            bool rightParsed = eventStartToEpoch(_events[j], &right);
            if ((!leftParsed && rightParsed) || (leftParsed && rightParsed && right < left)) {
                GoogleCalendarEvent *tmp = _events[i];
                _events[i] = _events[j];
                _events[j] = tmp;
            }
        }
    }
}

GoogleCalendarEventList *GoogleCalendar::getEvents(const char *accessToken, const char *calendarId, struct tm *start, struct tm *end) {
    lastCalendarFetchError = "";
    MyHTTPClient client;
    KeyValues headers(1);
    String bearer = "Bearer ";
    bearer += accessToken;
    headers.add("Authorization", bearer.c_str());

    KeyValues data(7);
    data.add("timeMin", timeToRFC3339(start));
    data.add("timeMax", timeToRFC3339(end));
    data.add("maxResults", String(MAX_EVENT_COUNT));
    data.add("orderBy", "startTime");
    data.add("singleEvents", "true");
    data.add("maxAttendees", "1");
    data.add("timeZone", "Asia/Tokyo");

    String url = GOOGLE_CALENDAR_EVENT_LIST_PREFIX + String(calendarId) + GOOGLE_CALENDAR_EVENT_LIST_POSTFIX;
    Serial.println(url);
    String res = client.get(url.c_str(), &headers, &data);
    //Serial.println("res is ");
    //Serial.println(res);

    if (res == "") {
        lastCalendarFetchError = "calendar ";
        lastCalendarFetchError += calendarId;
        lastCalendarFetchError += ": ";
        String httpError = client.lastError();
        lastCalendarFetchError += httpError.length() > 0 ? httpError : "empty response";
        return NULL;
    }
    Serial.println("parse json");
    DynamicJsonDocument doc(35000);
    Serial.println(String("doc capacity() = ") + doc.capacity());
    DeserializationError error = deserializeJson(doc, res);
    Serial.println("deserialized json");
    if (error) {
        Serial.println("deserializeJson() failed");
        Serial.println(error.c_str());
    }
    if (doc.overflowed()) { 
        Serial.println(" overflowed ");
    }
    Serial.println(String("memoryUsage = ") + doc.memoryUsage());
   
    GoogleCalendarEventList *eventList = new GoogleCalendarEventList(MAX_EVENT_COUNT);
    JsonArray items = doc["items"].as<JsonArray>();
    Serial.println(String("item size = ") + items.size());
    for (int i = 0 ; i < items.size() ; i++) {
        Serial.print(" item =  ");
        Serial.println(i);
        const char *summary = items[i]["summary"] | "";
        
        String start = "";
        bool isPeriodEvent = false;
        const char *datetime = items[i]["start"]["dateTime"] | "";
        Serial.println(datetime);
        // datetimeは基本常に!=NULL、なので空文字列で存在チェック
        if (strcmp("", datetime) != 0) {
            Serial.println("dateTime use(start)");
            start += datetime;
        }
        else if (items[i]["start"]["date"] != NULL) {
            isPeriodEvent = true;
            Serial.println("date use(start)");
            start += (const char *)items[i]["start"]["date"];
        }
        else {
            Serial.println("no dateTime or date");
        }
        Serial.println(start);
        String end = "";
        datetime = items[i]["end"]["dateTime"] | "";
        if (strcmp("", datetime) != 0) {
            end += datetime;
        }
        else if (items[i]["end"]["date"] != NULL) {
            Serial.println("date use(end)");
            end += (const char *)items[i]["end"]["date"];
        }
        else {
            Serial.println("no dateTime or date");
        }
        Serial.println(end);

        GoogleCalendarEvent event(summary, start.c_str(), end.c_str(), isPeriodEvent);

        eventList->add(event);
    }

    return eventList;
}

GoogleCalendarEventList *GoogleCalendar::getEvents(const char *accessToken, const char *calendarIds[], int calendarIdCount, struct tm *start, struct tm *end) {
    lastCalendarFetchError = "";
    GoogleCalendarEventList *mergedEventList = new GoogleCalendarEventList(MAX_EVENT_COUNT * calendarIdCount);
    int attemptedCalendarCount = 0;
    int successfulCalendarCount = 0;

    for (int i = 0; i < calendarIdCount; i++) {
        const char *calendarId = calendarIds[i];
        if (calendarId == NULL || strcmp(calendarId, "") == 0) {
            continue;
        }

        attemptedCalendarCount++;
        GoogleCalendarEventList *eventList = GoogleCalendar::getEvents(accessToken, calendarId, start, end);
        if (eventList == NULL) {
            Serial.print("calendar fetch failed; skip calendarId=");
            Serial.println(calendarId);
            String error = lastCalendarFetchError;
            if (error.length() > 0) {
                lastCalendarFetchError = error;
            }
            continue;
        }
        successfulCalendarCount++;

        for (int j = 0; j < eventList->length(); j++) {
            if (!mergedEventList->add(*eventList->get(j))) {
                Serial.println("merged event list is full; skip remaining events");
                break;
            }
        }
        delete eventList;
    }

    if (attemptedCalendarCount > 0 && successfulCalendarCount == 0) {
        delete mergedEventList;
        return NULL;
    }

    mergedEventList->sortByStartTime();
    return mergedEventList;
}

String GoogleCalendar::lastFetchError() {
    return lastCalendarFetchError;
}

std::vector<GoogleCalendarListItem> GoogleCalendar::getCalendars(const char *accessToken) {
    std::vector<GoogleCalendarListItem> calendars;

    MyHTTPClient client;
    KeyValues headers(1);
    String bearer = "Bearer ";
    bearer += accessToken;
    headers.add("Authorization", bearer.c_str());

    String res = client.get(GOOGLE_CALENDAR_LIST, &headers, NULL);

    if (res == "") {
        return calendars;
    }

    DynamicJsonDocument doc(8000);
    deserializeJson(doc, res);
    JsonArray items = doc["items"].as<JsonArray>();
    for (int i = 0 ; i < items.size() ; i++) {
        GoogleCalendarListItem item;
        const char *id = items[i]["id"];
        item.id = id;
        const char *summary = items[i]["summary"];
        item.summary = summary;
        calendars.push_back(item);
    }
    return calendars;
}

void copyOrEmpty(char *dest, size_t destSize, const char *src) {
    if (destSize == 0) {
        return;
    }
    if (src == NULL) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';
}


String timeToRFC3339(struct tm *tm) {
    char buf[30];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", tm);
    return String(buf) + "+09:00";
}

String formatLocalDateTimePeriod(const char *start, const char *end) {
    return formatLocalDateTime(start, "%Y-%m-%d") + formatLocalDateTime(start, "%H:%M") + " - " + formatLocalDateTime(end, "%H:%M");
}

String formatLocalTimePeriod(const char *start, const char *end) {
    return formatLocalDateTime(start, "%H:%M") + " - " + formatLocalDateTime(end, "%H:%M");
}

String formatLocalDateTime(const char *datetime, const char *format) {
    time_t epoch = 0;
    if (!parseRFC3339ToEpoch(datetime, &epoch)) {
        return String(datetime);
    }

    time_t jstEpoch = epoch + JST_OFFSET_SECONDS;
    struct tm local;
    gmtime_r(&jstEpoch, &local);

    if (strcmp(format, "%Y-%m-%d") == 0) {
        return formatDateWithWeekday(&local) + " ";
    }

    char buf[32];
    strftime(buf, sizeof(buf), format, &local);
    return String(buf);
}

String formatLocalDate(const char *date) {
    time_t epoch = 0;
    if (!parseDateToEpoch(date, &epoch)) {
        return String(date);
    }

    struct tm local;
    gmtime_r(&epoch, &local);
    return formatDateWithWeekday(&local);
}

String formatDateWithWeekday(struct tm *date) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d%s",
             date->tm_year + 1900,
             date->tm_mon + 1,
             date->tm_mday,
             WEEKDAYS[date->tm_wday]);
    return String(buf);
}

bool parseRFC3339ToEpoch(const char *datetime, time_t *epoch) {
    if (datetime == NULL || strlen(datetime) < 19) {
        return false;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (sscanf(datetime, "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour, &minute, &second) != 6) {
        return false;
    }

    const char *tz = datetime + 19;
    while (*tz != '\0' && *tz != 'Z' && *tz != '+' && *tz != '-') {
        tz++;
    }

    int offsetSeconds = 0;
    if (*tz == '+' || *tz == '-') {
        int sign = (*tz == '+') ? 1 : -1;
        int offsetHour = parseTwoDigits(tz + 1);
        int offsetMinute = 0;
        if (*(tz + 3) == ':') {
            offsetMinute = parseTwoDigits(tz + 4);
        }
        else {
            offsetMinute = parseTwoDigits(tz + 3);
        }
        if (offsetHour < 0 || offsetMinute < 0) {
            return false;
        }
        offsetSeconds = sign * ((offsetHour * 60 + offsetMinute) * 60);
    }
    else if (*tz != 'Z') {
        return false;
    }

    *epoch = epochFromUtcComponents(year, month, day, hour, minute, second) - offsetSeconds;
    return true;
}

bool parseDateToEpoch(const char *date, time_t *epoch) {
    if (date == NULL || strlen(date) < 10) {
        return false;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    if (sscanf(date, "%4d-%2d-%2d", &year, &month, &day) != 3) {
        return false;
    }

    *epoch = epochFromUtcComponents(year, month, day, 0, 0, 0);
    return true;
}

bool eventStartToEpoch(GoogleCalendarEvent *event, time_t *epoch) {
    if (event == NULL) {
        return false;
    }
    if (event->isPeriod()) {
        return parseDateToEpoch(event->start(), epoch);
    }
    return parseRFC3339ToEpoch(event->start(), epoch);
}

int parseTwoDigits(const char *value) {
    if (!isdigit(value[0]) || !isdigit(value[1])) {
        return -1;
    }
    return (value[0] - '0') * 10 + (value[1] - '0');
}

time_t epochFromUtcComponents(int year, int month, int day, int hour, int minute, int second) {
    int64_t days = daysFromCivil(year, month, day);
    return (time_t)(days * 86400 + hour * 3600 + minute * 60 + second);
}

int64_t daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}
