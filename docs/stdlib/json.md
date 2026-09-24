# json

JSON helpers built on Python's `json` module.

Functions and behaviors:

- `jsonValid(s)` → `true` if `s` is valid JSON, otherwise `false`.
- `jsonParse(s)` → pretty-printed JSON (2-space indent) or `""` on parse error.
- `jsonGet(s, key)` → string value for `key` or `""` if missing/wrong type.
- `jsonGetInt(s, key)`, `jsonGetFloat(s, key)`, `jsonGetBool(s, key)` → typed getters returning `0`, `0.0`, or `false` on error.
- `jsonKeys(s)` → top-level keys as a comma-separated string or `""` on error.
- `jsonStringify(s)` → JSON-encode a Lynxer string value.
- `jsonArray(lst)`, `jsonObject(lst)` → helpers to build JSON structures from Lynxer lists.
- `jsonHas(s, key)` → `true` if `key` exists.
- `jsonLength(s)` → number of top-level keys/elements or `0` on error.
- `jsonSet(s, key, val)` → set a key to JSON value `val` (if `val` parses as JSON) or string `val` otherwise; returns updated JSON string.
- `jsonDelete(s, key)` → returns updated JSON string with `key` removed.
- `jsonMerge(a, b)` → merge JSON objects with `b` overwriting `a`.
- `jsonSetInt(s, key, value)` → set integer value.
- `jsonPretty(s)` → pretty-print with 4-space indent.
- `jsonType(s, key)` → returns JSON type name for key (e.g. `string`, `int`, `array`, `object`, `null`).
- `jsonBuild(pairs)` → build object from `key=value|key2=v2` string.

Notes:
- Many functions return safe defaults on error; validate before use when necessary.