# server

Minimal HTTP server helpers for tests and quick mocks. Wraps Python's `http.server`.

Functions:

- `init(host, port)` — prepares an in-process HTTP server bound to `(host, port)`.
- `get(path, response)` — register a handler for `GET` requests returning `response`.
- `post(path, response)` — register a handler for `POST` requests returning `response`.
- `run()` — start the server (`serve_forever`).

Notes:
- This server is intended primarily for simple test scenarios. Responses are served as `text/plain`.
- The server stores routes in builtin objects and should be started in a dedicated execution context.