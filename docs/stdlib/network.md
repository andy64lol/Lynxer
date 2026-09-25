# network

HTTP and WebSocket client backed by Rust [`ureq`](https://docs.rs/ureq) with
[`rustls`](https://docs.rs/rustls) and [`tungstenite`](https://docs.rs/tungstenite).
This replaces the older `http` and `net` modules.

```lynx
global setup(){
    import("network");
}
```

HTTPS/WSS use `rustls`, so there is no system OpenSSL dependency. Built from the
Rust crate `rust/network`.

## HTTP client

| Function | Description |
|----------|-------------|
| `get(url)` | Response body, or `ERROR: ...` |
| `getStatus(url)` | Status code, or `-1` |
| `getHeaders(url)` | Newline-separated headers, or `ERROR: ...` |
| `post(url, body, contentType)` | POST body |
| `put(url, body, contentType)` | PUT body |
| `delete(url)` | DELETE body |
| `patch(url, body, contentType)` | PATCH body |
| `getJson(url)` | GET with `Accept: application/json` |
| `postJson(url, jsonBody)` | POST JSON |
| `download(url, filepath)` | Write body to disk (`"ok"` / `ERROR:`) |
| `urlencode(text)` | Plus-style query encoding |
| `httpHead(url)` | HEAD status, or `-1` |

## WebSocket client

Connections are keyed by a name string.

| Function | Description |
|----------|-------------|
| `wsConnect(name, uri)` | Open `ws://` or `wss://` (`"ok"` / `ERROR:`) |
| `wsSend(name, message)` | Send text |
| `wsReceive(name)` | Wait for the next message |
| `wsSendReceive(name, message)` | Round-trip helper |
| `wsClose(name)` | Close and forget |
| `wsConnected(name)` | `true` while open |

## Raw TCP client

Plaintext TCP, keyed by a name like the WebSocket registry. Every connection
function returns `"ok"` or `ERROR: ...`; nothing here does TLS, which belongs to
the HTTP/WebSocket client above.

| Function | Description |
|----------|-------------|
| `tcpConnect(name, host, port)` | Connect to `host:port` (`"ok"` / `ERROR:`) |
| `tcpSend(name, data)` | Send UTF-8 text |
| `tcpReceive(name, bufSize)` | Receive up to `bufSize` bytes, decoded |
| `tcpSendReceive(name, data, bufSize)` | Send then receive |
| `tcpClose(name)` | Shut down and forget the connection |

Connects and receives time out after 30 seconds, so a quiet peer cannot wedge
the interpreter. `tcpReceive` returns an empty string when the peer closes
cleanly, and `ERROR: receive timeout` / `ERROR: receive failed` otherwise.

## Host and reachability helpers

| Function | Description |
|----------|-------------|
| `getLocalIP()` | Primary local IPv4 address, falling back to the hostname's first non-loopback address and then `127.0.0.1` |
| `isPortOpen(host, port, timeoutSecs)` | `true` if a TCP connection succeeds within the timeout |
| `ping(host)` | `isPortOpen(host, 80, 3)` — the original's reachability probe |

## URL helpers

`urlScheme`, `urlHost`, `urlPath`, `urlParse` (JSON object with
`scheme`/`host`/`port`/`path`/`query`/`fragment`), `getHostname`, and
`resolveHost`.

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
