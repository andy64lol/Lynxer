# http

Simple HTTP client built on Python's urllib. No extra dependencies.

Common patterns:
- Functions return the response body as a string on success, or a string beginning with `ERROR:` on failure.
- `getStatus` returns numeric HTTP status or `-1` on error.

Functions:

- `get(url)` → response body or `ERROR: ...`.
- `getStatus(url)` → numeric status code (e.g. `200`) or `-1`.
- `getHeaders(url)` → response headers as a newline-separated string, or `ERROR: ...`.
- `post(url, body, contentType)` → response body or `ERROR: ...`.
- `put(url, body, contentType)` → response body or `ERROR: ...`.
- `delete(url)` → response body or `ERROR: ...`.
- `patch(url, body, contentType)` → response body or `ERROR: ...`.
- `getJson(url)` → convenience `GET` that requests `application/json` (returns body or `ERROR:`).
- `postJson(url, jsonBody)` → POST with `application/json`.
- `download(url, filepath)` → writes to `filepath` and returns `"ok"` or `ERROR: ...`.
- `urlencode(text)` → URL-encode `text` (plus-style).

Timeouts and errors:
- Network calls use a 30s timeout; HTTP errors are converted into `ERROR: HTTP <code> <reason>` strings.