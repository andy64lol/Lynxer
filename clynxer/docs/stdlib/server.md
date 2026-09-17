# server

HTTP and WebSocket server backed by [Crow](https://github.com/CrowCpp/Crow)
(Boost.Asio). Pair it with `network` for local round-trip tests.

```c
global setup(){
    import("server");
    import("network");
}
```

Requires Boost and Crow headers staged by `make -C clynxer deps`.

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

Paths must start with `/`. Crow logging is quieted to errors only.

HTML is supplied as a normal Lynxer string and is returned unchanged. For
example:

```c
global main(){
    global.server.get("/", "<!doctype html><html><body><h1>Hello</h1></body></html>");
    println(global.server.start(8080));
    while (global.server.running()) {}
}
```
