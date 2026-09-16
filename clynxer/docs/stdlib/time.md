# time

Date and time helpers.

**Backend:** native — `stdlib/time.so`, built from `stdlib/time.cpp` over
`<chrono>` and `<ctime>`. **Import:** `import("time")` → `global.time.*`

| Function | Signature | Notes |
| --- | --- | --- |
| `now` | `() -> str` | `YYYY-MM-DD HH:MM:SS` local time |
| `getTime` | `() -> str` | `HH:MM:SS` |
| `getDate` | `() -> str` | `YYYY-MM-DD` |
| `isoNow` | `() -> str` | `YYYY-MM-DDTHH:MM:SS` |
| `format` | `(str pattern) -> str` | `strftime` with the current local time |
| `fromTimestamp` | `(float value) -> str` | Formats a Unix timestamp as `YYYY-MM-DD HH:MM:SS` |
| `toTimestamp` | `(str value) -> float` | Parses `YYYY-MM-DD[ HH:MM:SS]`; `-1.0` on error |
| `addDays` | `(str date, int days) -> str` | Adds days to a `YYYY-MM-DD` date; `""` on error |
| `diffDays` | `(str left, str right) -> int` | Whole days from `left` to `right` |
| `timestamp` | `() -> float` | Current Unix time |
| `getYear` … `getSecond` | `() -> int` | Local clock fields |
| `getWeekdayNum` | `() -> int` | Weekday number, Monday = `0` |
| `getWeekday` | `() -> str` | Full weekday name, e.g. `Wednesday` |
| `isLeapYear` | `(int year) -> bool` | Leap-year test |
| `daysInMonth` | `(int year, int month) -> int` | `0` for an out-of-range month |

## Example

```lynx
global setup(){ import("time"); }

global main(){
    println(global.time.getDate());
    println(global.time.addDays("2026-02-27", 2));
    println(global.time.daysInMonth(2024, 2));
}
```
