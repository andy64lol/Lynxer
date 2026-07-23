# time

Date and time helpers built on Python's `datetime` module.

Functions:

- `now()` → current `YYYY-MM-DD HH:MM:SS`.
- `getTime()` → `HH:MM:SS`.
- `getDate()` → `YYYY-MM-DD`.
- `getYear()`, `getMonth()`, `getDay()`, `getHour()`, `getMinute()`, `getSecond()` → numeric components.
- `getWeekday()`, `getWeekdayNum()` → weekday name and weekday number (`0=Monday`).
- `isoNow()` → ISO 8601 datetime.
- `format(pattern)` → format current datetime with `strftime` pattern.
- `timestamp()` → Unix epoch timestamp (float).
- `fromTimestamp(ts)` → convert epoch to `YYYY-MM-DD HH:MM:SS`.
- `toTimestamp(dt)` → parse `YYYY-MM-DD HH:MM:SS` to timestamp (`-1.0` on error).
- `addDays(date, days)` → add days to `YYYY-MM-DD` string.
- `diffDays(date1, date2)` → days between two dates.
- `isLeapYear(year)`, `daysInMonth(year, month)` — calendar helpers.

Notes:
- Use `toTimestamp` carefully; it expects the exact `YYYY-MM-DD HH:MM:SS` format.