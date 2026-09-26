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
`{{ key }}` and `{{ nested.key }}`. The original used Jinja2; this is the
documented substitution subset — loops and conditionals are not implemented.
An unknown key renders as the empty string. `setTemplateFolder(folder)` sets the
directory that `template` / `templatePost` resolve file names against.

```lynx
global.server.templateString("/greet", "<h1>Hello, {{ name }}!</h1>",
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
| `runHTTPS(cert, key)` / `runSSLAdhoc()` | Not available — see below |

`run()` blocks, matching the original. Use `start()` + `stop()` when the program
has to keep running.

**TLS is not built.** `runHTTPS` and `runSSLAdhoc` return an `ERROR:` string
explaining that this build has no TLS backend: `axum-server`, `tokio-rustls`,
`rustls-pemfile` and `rcgen` are not among the pinned dependencies, and adding
them would make the module require network access to build.

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
