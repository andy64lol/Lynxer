#include <chrono>
#include <cstdint>
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
static bool parseDate(const char* text, std::tm& result) {
    std::istringstream input(text);
    input >> std::get_time(&result, "%Y-%m-%d");
    return !input.fail();
}
extern "C" const char* time_fromTimestamp(double value) {
    const auto tm = localTime(static_cast<std::time_t>(value));
    std::ostringstream output; output << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return stable(output.str());
}
extern "C" double time_toTimestamp(const char* text) {
    std::tm value{};
    std::istringstream input(text);
    input >> std::get_time(&value, "%Y-%m-%d %H:%M:%S");
    if (input.fail()) return -1.0;
    return static_cast<double>(std::mktime(&value));
}
extern "C" const char* time_addDays(const char* text, std::int64_t days) {
    std::tm value{};
    if (!parseDate(text, value)) return stable("");
    value.tm_mday += static_cast<int>(days);
    std::mktime(&value);
    std::ostringstream output; output << std::put_time(&value, "%Y-%m-%d");
    return stable(output.str());
}
extern "C" std::int64_t time_diffDays(const char* left, const char* right) {
    std::tm a{}, b{};
    if (!parseDate(left, a) || !parseDate(right, b)) return 0;
    return static_cast<std::int64_t>(std::difftime(std::mktime(&b), std::mktime(&a)) / 86400.0);
}
extern "C" const char* time_now() { return stable(formatNow("%Y-%m-%d %H:%M:%S")); }
extern "C" const char* time_getTime() { return stable(formatNow("%H:%M:%S")); }
extern "C" const char* time_getDate() { return stable(formatNow("%Y-%m-%d")); }
extern "C" const char* time_isoNow() { return stable(formatNow("%Y-%m-%dT%H:%M:%S")); }
extern "C" const char* time_format(const char* pattern) {
    return stable(formatNow(pattern));
}
extern "C" std::int64_t time_getYear() { return localTime(std::time(nullptr)).tm_year + 1900; }
extern "C" std::int64_t time_getMonth() { return localTime(std::time(nullptr)).tm_mon + 1; }
extern "C" std::int64_t time_getDay() { return localTime(std::time(nullptr)).tm_mday; }
extern "C" std::int64_t time_getHour() { return localTime(std::time(nullptr)).tm_hour; }
extern "C" std::int64_t time_getMinute() { return localTime(std::time(nullptr)).tm_min; }
extern "C" std::int64_t time_getSecond() { return localTime(std::time(nullptr)).tm_sec; }
extern "C" std::int64_t time_getWeekdayNum() { return localTime(std::time(nullptr)).tm_wday == 0 ? 6 : localTime(std::time(nullptr)).tm_wday - 1; }
extern "C" double time_timestamp() { return static_cast<double>(std::time(nullptr)); }
extern "C" std::int64_t time_isLeapYear(std::int64_t year) {
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1 : 0;
}
extern "C" std::int64_t time_daysInMonth(std::int64_t year, std::int64_t month) {
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 0;
    return days[month - 1] + (month == 2 && time_isLeapYear(year) ? 1 : 0);
}
extern "C" int lynxer_module_init_v1(RegisterFunction f, RegisterConstant, RegisterType) {
    return f("now","time_now","cdecl:cstring()") &&
           f("getTime","time_getTime","cdecl:cstring()") &&
           f("getDate","time_getDate","cdecl:cstring()") &&
           f("isoNow","time_isoNow","cdecl:cstring()") &&
           f("format","time_format","cdecl:cstring(cstring)") &&
           f("fromTimestamp","time_fromTimestamp","cdecl:cstring(double)") &&
           f("toTimestamp","time_toTimestamp","cdecl:double(cstring)") &&
           f("addDays","time_addDays","cdecl:cstring(cstring,int64)") &&
           f("diffDays","time_diffDays","cdecl:int64(cstring,cstring)") &&
           f("getYear","time_getYear","cdecl:int64()") &&
           f("getMonth","time_getMonth","cdecl:int64()") &&
           f("getDay","time_getDay","cdecl:int64()") &&
           f("getHour","time_getHour","cdecl:int64()") &&
           f("getMinute","time_getMinute","cdecl:int64()") &&
           f("getSecond","time_getSecond","cdecl:int64()") &&
           f("getWeekdayNum","time_getWeekdayNum","cdecl:int64()") &&
           f("timestamp","time_timestamp","cdecl:double()") &&
           f("isLeapYear","time_isLeapYear","cdecl:int64(int64)") &&
           f("daysInMonth","time_daysInMonth","cdecl:int64(int64,int64)") ? 0 : 1;
}
