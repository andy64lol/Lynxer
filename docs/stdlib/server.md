# server

HTTP and WebSocket server backed by Rust [`axum`](https://docs.rs/axum) on
[`tokio`](https://docs.rs/tokio). Pair it with `network` for local round-trip tests.

```lynx
global setup(){
    import("server");
    import("network");
}
```

Built from the Rust crate `rust/server`; no Boost or Crow dependency.

## API

Register routes while the server is stopped, then `start(port)` / `stop()`.

| Function | Description |
|----------|-------------|
| `routeGet(path, body)` | Fixed GET response rendered as `text/html; charset=utf-8` |
| `routePost(path, body)` | Fixed POST response rendered as `text/html; charset=utf-8` |
| `get(path, html)` | HTML GET route alias for `routeGet` |
| `post(path, html)` | HTML POST route alias for `routePost` |
| `htmlGet(path, html)` | Explicit HTML GET route alias |
| `htmlPost(path, html)` | Explicit HTML POST route alias |
| `routeEcho(path)` | POST that returns the request body |
| `wsEcho(path)` | WebSocket echo endpoint |
| `clearRoutes()` | Drop pending routes |
| `start(port)` | Listen on `127.0.0.1:port` in the background |
| `stop()` | Stop the listener |
| `running()` | `true` while listening |
| `port()` | Bound port, or `0` |

Paths must start with `/`. The listener binds to `127.0.0.1` only.

HTML is supplied as a normal Lynxer string and is returned unchanged. For
example:

```lynx
global main(){
    global.server.get("/", "<!doctype html><html><body><h1>Hello</h1></body></html>");
    println(global.server.start(8080));
    while (global.server.running()) {}
}
```

## Routes

Register routes while the server is stopped. Paths must start with `/`, and a
duplicate of the same verb and path is rejected.

| Function | Description |
|----------|-------------|
| `routeGet(path, body)` / `get` / `htmlGet` | Fixed GET response as `text/html; charset=utf-8` |
| `routePost(path, body)` / `post` / `htmlPost` | Fixed POST response |
| `put(path, body)` / `delete(path, body)` / `patch(path, body)` | The other verbs |
| `anyHttp(path, body)` | One route answering GET, POST, PUT, DELETE and PATCH |
| `getStatus(path, body, status)` | HTML GET with a custom status code |
| `routeEcho(path)` | POST that returns the request body as `text/plain` |
| `jsonGet(path, json)` / `jsonPost(path, json)` | JSON responses (`application/json`) |
| `jsonRoute(path, json)` | JSON for GET and POST |
| `jsonStatus(path, json, status)` | JSON with a custom status code |
| `redirect(path, target)` / `redirect301(path, target)` | 302 / 301 redirects |
| `template(path, file, dataJson)` / `templatePost(...)` | Render a template file |
| `templateString(path, source, dataJson)` | Render an inline template |
| `serveFile(path, filepath)` | Serve one file at `path` |
| `staticFiles(urlPrefix, directory)` | Serve `directory` under the URL prefix |
| `staticSite(directory)` | Serve `directory`; `/` maps to `/index.html` |
| `wsEcho(path)` | WebSocket echo endpoint |
| `clearRoutes()` | Drop pending routes, WebSocket paths and static trees |

`HEAD` is served by the matching `GET` route.

### Templates

`dataJson` is a JSON object whose keys become template variables, addressed as
`{{ key }}` and `{{ nested.key }}`. A Jinja-compatible subset is implemented:

- `{{ expression }}` — output, with filters (`{{ name | upper }}`)
- `{% if %}` / `{% elif %}` / `{% else %}` / `{% endif %}`
- `{% for x in items %}` / `{% endfor %}` — with `loop.index`, `loop.index0`,
  `loop.first`, `loop.last` and `loop.length`; `items` may be an array, an
  object (its keys), a string (its characters) or `range(n)`
- `{% set name = expression %}`
- `{# comment #}`

Expressions cover literals, dotted paths, `or`/`and`/`not`, `==`/`!=`/`<`/
`<=`/`>`/`>=`, `in`, `+`/`-`/`*`/`/`/`%`, `~` (concatenation), grouping and
`range()`, plus the filters `upper`, `lower`, `trim`, `capitalize`, `title`,
`length`, `first`, `last`, `reverse`, `join`, `default`, `int`, `float`,
`round`, `replace` and `string`. Inheritance, macros and includes are not
implemented, there is no auto-escaping, and an unknown key renders as the empty
string. `setTemplateFolder(folder)` sets the directory that `template` /
`templatePost` resolve file names against.

A template renders **inside** the request, so it can read the request through
the `request` object:

| Path | Value |
|------|-------|
| `request.method` / `request.path` / `request.query` | The request line |
| `request.args` | Object of query-string parameters |
| `request.headers` / `request.cookies` | Objects of header / cookie values |
| `request.body` / `request.contentType` | Raw body and content type |
| `request.json` | The body parsed as JSON (`application/json` only) |
| `request.form` | The body parsed as a form (form-urlencoded only) |

The query-string arguments also overlay the root context, so `?name=Ada` makes
`{{ name }}` render `Ada` even without the prefix.

```lynx
global.server.templateString(
    "/greet",
    "<h1>Hello, {{ name | upper }}!</h1>"
    "{% if request.args.n %}<p>n={{ request.args.n }}</p>{% endif %}",
    "{\"name\":\"World\"}");
```

## Middleware and error bodies

| Function | Description |
|----------|-------------|
| `cors()` | `Access-Control-Allow-Origin: *` on every response |
| `corsOrigin(origin)` | Allow one origin instead of `*` |
| `addGlobalHeader(key, value)` | Append a header to every response |
| `enableRequestLog()` | Print `METHOD /path` for each request |
| `setDebug(enabled)` | Debug flag; also enables request logging |
| `notFound(body)` / `serverError(body)` / `forbidden(body)` / `methodNotAllowed(body)` | Replace the default 404 / 500 / 403 / 405 body |

With CORS enabled, an `OPTIONS` preflight is answered `204` with
`Access-Control-Allow-Methods` and `-Headers`.

## Request context

These read the **most recently handled request**. A Lynxer route is a fixed
string rather than a callback, and the interpreter evaluates one frame at a
time, so there is no point at which such a reader could run *inside* a request;
the original's Flask request context has no equivalent here. Each returns `""`
when no request has been handled yet.

| Function | Description |
|----------|-------------|
| `getMethod()` / `getPath()` / `getUrl()` | Verb, path, and `http://host/path?query` |
| `getBody()` / `getContentType()` | Raw body and the request `Content-Type` |
| `getHeader(name)` / `getCookie(name)` | A header (case-insensitive) or cookie value |
| `getArg(name)` / `getForm(name)` | Query-string parameter / form field |
| `getRemoteAddr()` | Client IP address |

## Lifecycle

| Function | Description |
|----------|-------------|
| `start(port)` | Listen on `127.0.0.1:port` in the background |
| `stop()` | Stop the listener |
| `running()` / `port()` | Listener state and bound port |
| `init(host, port)` | Record the host and port for `run()` |
| `run()` | Start on the recorded host/port, then **block** until the process ends |
| `runHTTPS(cert, key)` | Start a **TLS** listener on the `init` host/port from a PEM certificate and key |
| `runSSLAdhoc()` | Start a TLS listener with a self-signed certificate generated in-process |

`run()` blocks, matching the original. Use `start()` + `stop()` when the program
has to keep running.

## TLS

`runHTTPS(certPath, keyPath)` and `runSSLAdhoc()` start an HTTPS listener with
the same routes, on the host and port recorded by `init(host, port)` — call that
first, or the op answers `ERROR: call init(host, port) before …`. Unlike `run()`
they do **not** block: they return `"ok"` once the socket is listening, and
`stop()` shuts the listener down. Both bound the port before returning, so a
port clash is reported by the call.

`runHTTPS` reads the PEM files and rejects one it cannot use *before* binding:
a missing file reports `ERROR: cannot read '<path>': …`, a file with no
certificate reports `ERROR: the certificate file contains no PEM certificate`,
and a key file with no key reports `ERROR: the key file contains no PEM private
key`. The certificate and key must match; a mismatch fails the handshake, not
the call.

`runSSLAdhoc()` mints a self-signed certificate for `localhost` and `127.0.0.1`
with `rcgen`, so it needs no files at all — it is the quickest way to stand up
HTTPS, and a client must skip verification (the certificate is not signed by any
authority it trusts).

TLS is `rustls` with the `ring` provider behind `axum-server` — pure Rust, no
system OpenSSL — with `rustls-pemfile` for the PEM files and `rcgen` for the
self-signed certificate. `docs/stdlib_server_tls.lynx` is the fixture: it mints
a certificate, serves over HTTPS and completes a real handshake.

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
