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
