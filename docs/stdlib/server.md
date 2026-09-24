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

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
