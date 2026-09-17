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
| `routeGet(path, body)` | Fixed GET response |
| `routePost(path, body)` | Fixed POST response |
| `routeEcho(path)` | POST that returns the request body |
| `wsEcho(path)` | WebSocket echo endpoint |
| `clearRoutes()` | Drop pending routes |
| `start(port)` | Listen on `127.0.0.1:port` in the background |
| `stop()` | Stop the listener |
| `running()` | `true` while listening |
| `port()` | Bound port, or `0` |

Paths must start with `/`. Crow logging is quieted to errors only.
