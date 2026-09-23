#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

static std::tm localTime(std::time_t value) {
    std::tm result{};
#if defined(_WIN32)
    localtime_s(&result, &value);
#else
    localtime_r(&value, &result);
#endif
    return result;
}

static std::string formatNow(const char* format) {
    const auto now = std::time(nullptr);
    const auto tm = localTime(now);
    std::ostringstream output;
    output << std::put_time(&tm, format);
    return output.str();
}

static const char* stable(std::string value) {
    thread_local std::string result;
    result = std::move(value);
    return result.c_str();
}

static std::int64_t daysFromCivil(std::int64_t year, unsigned month,
                                  unsigned day) {
    year -= month <= 2;
    const std::int64_t era =
        (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra =
        static_cast<unsigned>(year - era * 400);
    const unsigned dayOfYear =
        (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 +
        day - 1;
    const unsigned dayOfEra =
        yearOfEra * 365 +
        yearOfEra / 4 -
        yearOfEra / 100 +
        dayOfYear;
    return era * 146097 +
           static_cast<std::int64_t>(dayOfEra) -
           719468;
}

static void civilFromDays(std::int64_t days,
                          std::int64_t& year,
                          unsigned& month,
                          unsigned& day) {
    days += 719468;
    const std::int64_t era =
        (days >= 0 ? days : days - 146096) / 146097;
    const unsigned dayOfEra =
        static_cast<unsigned>(days - era * 146097);
    const unsigned yearOfEra =
        (dayOfEra -
         dayOfEra / 1460 +
         dayOfEra / 36524 -
         dayOfEra / 146096) / 365;
    const std::int64_t y =
        static_cast<std::int64_t>(yearOfEra) + era * 400;
    const unsigned dayOfYear =
        dayOfEra -
        (365 * yearOfEra +
         yearOfEra / 4 -
         yearOfEra / 100);
    const unsigned mp =
        (5 * dayOfYear + 2) / 153;
    day =
        dayOfYear -
        (153 * mp + 2) / 5 +
        1;
    month =
        mp + (mp < 10 ? 3 : -9);
    year = y + (month <= 2);
}

static std::int64_t civilMonthDays(std::int64_t year, int month) {
    static const int lengths[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    if (month < 1 || month > 12) {
        return 0;
    }

    const bool leap =
        year % 4 == 0 &&
        (year % 100 != 0 || year % 400 == 0);

    return lengths[month - 1] +
           (month == 2 && leap ? 1 : 0);
}

static bool parseTimestamp(const char* text,
                           std::int64_t& seconds) {
    if (text == nullptr) {
        return false;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int used = 0;

    if (std::sscanf(
            text,
            "%d-%d-%d %d:%d:%d%n",
            &year,
            &month,
            &day,
            &hour,
            &minute,
            &second,
            &used) == 6 &&
        text[used] == '\0') {
    } else {
        used = 0;

        if (std::sscanf(
                text,
                "%d-%d-%d%n",
                &year,
                &month,
                &day,
                &used) != 3 ||
            text[used] != '\0') {
            return false;
        }

        hour = 0;
        minute = 0;
        second = 0;
    }

    if (month < 1 ||
        month > 12 ||
        day < 1 ||
        day > 31 ||
        hour < 0 ||
        hour > 23 ||
        minute < 0 ||
        minute > 59 ||
        second < 0 ||
        second > 59) {
        return false;
    }

    if (day > civilMonthDays(year, month)) {
        return false;
    }

    seconds =
        daysFromCivil(
            year,
            static_cast<unsigned>(month),
            static_cast<unsigned>(day)
        ) * 86400 +
        static_cast<std::int64_t>(hour) * 3600 +
        static_cast<std::int64_t>(minute) * 60 +
        static_cast<std::int64_t>(second);

    return true;
}

static std::string formatUtc(std::int64_t seconds,
                             bool withTime) {
    std::int64_t days = seconds / 86400;
    std::int64_t remainder = seconds % 86400;

    if (remainder < 0) {
        remainder += 86400;
        days -= 1;
    }

    std::int64_t year = 0;
    unsigned month = 0;
    unsigned day = 0;

    civilFromDays(
        days,
        year,
        month,
        day
    );

    char buffer[64];

    if (withTime) {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%04lld-%02u-%02u %02lld:%02lld:%02lld",
            static_cast<long long>(year),
            month,
            day,
            static_cast<long long>(remainder / 3600),
            static_cast<long long>((remainder % 3600) / 60),
            static_cast<long long>(remainder % 60)
        );
    } else {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%04lld-%02u-%02u",
            static_cast<long long>(year),
            month,
            day
        );
    }

    return std::string(buffer);
}

extern "C" const char* time_fromTimestamp(double value) {
    return stable(
        formatUtc(
            static_cast<std::int64_t>(value),
            true
        )
    );
}

extern "C" double time_toTimestamp(const char* text) {
    std::int64_t seconds = 0;

    if (!parseTimestamp(text, seconds)) {
        return -1.0;
    }

    return static_cast<double>(seconds);
}

extern "C" const char* time_addDays(const char* text,
                                    std::int64_t days) {
    std::int64_t seconds = 0;

    if (!parseTimestamp(text, seconds)) {
        return stable("");
    }

    return stable(
        formatUtc(
            seconds + days * 86400,
            false
        )
    );
}

extern "C" std::int64_t time_diffDays(const char* left,
                                      const char* right) {
    std::int64_t a = 0;
    std::int64_t b = 0;

    if (!parseTimestamp(left, a) ||
        !parseTimestamp(right, b)) {
        return 0;
    }

    return (b - a) / 86400;
}

extern "C" const char* time_now() {
    return stable(formatNow("%Y-%m-%d %H:%M:%S"));
}

extern "C" const char* time_getTime() {
    return stable(formatNow("%H:%M:%S"));
}

extern "C" const char* time_getDate() {
    return stable(formatNow("%Y-%m-%d"));
}

extern "C" const char* time_isoNow() {
    return stable(formatNow("%Y-%m-%dT%H:%M:%S"));
}

extern "C" const char* time_format(const char* pattern) {
    return stable(formatNow(pattern));
}

extern "C" std::int64_t time_getYear() {
    return localTime(std::time(nullptr)).tm_year + 1900;
}

extern "C" std::int64_t time_getMonth() {
    return localTime(std::time(nullptr)).tm_mon + 1;
}

extern "C" std::int64_t time_getDay() {
    return localTime(std::time(nullptr)).tm_mday;
}

extern "C" std::int64_t time_getHour() {
    return localTime(std::time(nullptr)).tm_hour;
}

extern "C" std::int64_t time_getMinute() {
    return localTime(std::time(nullptr)).tm_min;
}

extern "C" std::int64_t time_getSecond() {
    return localTime(std::time(nullptr)).tm_sec;
}

extern "C" std::int64_t time_getWeekdayNum() {
    const auto weekday =
        localTime(std::time(nullptr)).tm_wday;

    return weekday == 0 ? 6 : weekday - 1;
}

extern "C" const char* time_getWeekday() {
    return stable(formatNow("%A"));
}

extern "C" double time_timestamp() {
    return static_cast<double>(std::time(nullptr));
}

extern "C" std::int64_t time_isLeapYear(
    std::int64_t year) {
    return (
        year % 4 == 0 &&
        (year % 100 != 0 || year % 400 == 0)
    ) ? 1 : 0;
}

extern "C" std::int64_t time_daysInMonth(
    std::int64_t year,
    std::int64_t month) {
    static const int days[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    if (month < 1 || month > 12) {
        return 0;
    }

    return days[month - 1] +
           (month == 2 && time_isLeapYear(year) ? 1 : 0);
}

extern "C" int lynxer_module_init_v1(
    RegisterFunction f,
    RegisterConstant,
    RegisterType) {
    return f("now", "time_now",
             "cdecl:cstring()") &&
           f("getTime", "time_getTime",
             "cdecl:cstring()") &&
           f("getDate", "time_getDate",
             "cdecl:cstring()") &&
           f("isoNow", "time_isoNow",
             "cdecl:cstring()") &&
           f("format", "time_format",
             "cdecl:cstring(cstring)") &&
           f("fromTimestamp", "time_fromTimestamp",
             "cdecl:cstring(double)") &&
           f("toTimestamp", "time_toTimestamp",
             "cdecl:double(cstring)") &&
           f("addDays", "time_addDays",
             "cdecl:cstring(cstring,int64)") &&
           f("diffDays", "time_diffDays",
             "cdecl:int64(cstring,cstring)") &&
           f("getYear", "time_getYear",
             "cdecl:int64()") &&
           f("getMonth", "time_getMonth",
             "cdecl:int64()") &&
           f("getDay", "time_getDay",
             "cdecl:int64()") &&
           f("getHour", "time_getHour",
             "cdecl:int64()") &&
           f("getMinute", "time_getMinute",
             "cdecl:int64()") &&
           f("getSecond", "time_getSecond",
             "cdecl:int64()") &&
           f("getWeekdayNum", "time_getWeekdayNum",
             "cdecl:int64()") &&
           f("getWeekday", "time_getWeekday",
             "cdecl:cstring()") &&
           f("timestamp", "time_timestamp",
             "cdecl:double()") &&
           f("isLeapYear", "time_isLeapYear",
             "cdecl:int64(int64)") &&
           f("daysInMonth", "time_daysInMonth",
             "cdecl:int64(int64,int64)")
               ? 0
               : 1;
}
