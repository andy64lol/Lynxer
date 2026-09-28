# Network Module

A robust HTTP, WebSocket, and TCP client built with Rust, using [`ureq`](https://docs.rs/ureq) with [`rustls`](https://docs.rs/rustls) and [`tungstenite`](https://docs.rs/tungstenite).
This module replaces the older `http` and `net` modules, eliminating the need for a system OpenSSL dependency.

```lynx
global setup() {
    import("network");
}
```

## Features

- **HTTPS/WSS**: Uses `rustls` for secure connections.
- **WebSocket**: Real-time communication with `ws://` and `wss://`.
- **Raw TCP**: Plaintext TCP connections for custom protocols.
- **URL Parsing**: Extract and manipulate URL components.

Built from the Rust crate `rust/network`.

---

## HTTP Client

Perform HTTP requests with support for all standard methods.

| Function | Description |
|----------|-------------|
| **`get(url)`** | Fetches the response body or returns an error. |
| **`getStatus(url)`** | Returns the HTTP status code or `-1` on failure. |
| **`getHeaders(url)`** | Returns newline-separated headers or an error. |
| **`post(url, body, contentType)`** | Sends a POST request with a body and content type. |
| **`put(url, body, contentType)`** | Sends a PUT request with a body and content type. |
| **`delete(url)`** | Sends a DELETE request. |
| **`patch(url, body, contentType)`** | Sends a PATCH request with a body and content type. |
| **`getJson(url)`** | Fetches JSON data with `Accept: application/json`. |
| **`postJson(url, jsonBody)`** | Sends a JSON body in a POST request. |
| **`download(url, filepath)`** | Downloads a file to disk (`"ok"` or `ERROR:`). |
| **`urlencode(text)`** | Encodes text for use in URLs (e.g., spaces become `+`). |
| **`httpHead(url)`** | Returns the HTTP HEAD status or `-1` on failure. |

---

## WebSocket Client

Establish and manage WebSocket connections using a name-based registry.

| Function | Description |
|----------|-------------|
| **`wsConnect(name, uri)`** | Opens a WebSocket connection (`ws://` or `wss://`). Returns `"ok"` or `ERROR:`. |
| **`wsSend(name, message)`** | Sends a text message over the WebSocket. |
| **`wsReceive(name)`** | Waits for and returns the next message. |
| **`wsSendReceive(name, message)`** | Sends a message and waits for a response. |
| **`wsClose(name)`** | Closes the WebSocket connection. |
| **`wsConnected(name)`** | Returns `true` if the connection is active. |

---

## Raw TCP Client

Plaintext TCP connections for custom protocols. Connections are managed using a name-based registry.

| Function | Description |
|----------|-------------|
| **`tcpConnect(name, host, port)`** | Connects to `host:port`. Returns `"ok"` or `ERROR:`. |
| **`tcpSend(name, data)`** | Sends UTF-8 text over the connection. |
| **`tcpReceive(name, bufSize)`** | Receives up to `bufSize` bytes, decoded as UTF-8. |
| **`tcpSendReceive(name, data, bufSize)`** | Sends data and waits for a response. |
| **`tcpClose(name)`** | Closes the TCP connection. |

**Timeouts**: Connections and receives time out after 30 seconds to prevent hanging.

---

## Host and Reachability Helpers

Check network reachability and retrieve local IP information.

| Function | Description |
|----------|-------------|
| **`getLocalIP()`** | Returns the primary local IPv4 address. Falls back to the hostname's first non-loopback address and then `127.0.0.1`. |
| **`isPortOpen(host, port, timeoutSecs)`** | Returns `true` if a TCP connection succeeds within the timeout. |
| **`ping(host)`** | Checks if `host:80` is reachable within 3 seconds (original behavior). |

---

## URL Helpers

Parse, manipulate, and encode URLs for web requests.

| Function | Description |
|----------|-------------|
| **`urlScheme(url)`** | Extracts the URL scheme (e.g., `https`). |
| **`urlHost(url)`** | Extracts the host and port (e.g., `example.com:8080`). Default ports are omitted. |
| **`urlPath(url)`** | Extracts the path (e.g., `/api/v1`). |
| **`urlParse(url)`** | Returns a JSON object with `scheme`, `host`, `port`, `path`, `query`, and `fragment`. |
| **`getHostname()`** | Returns the local hostname. |
| **`resolveHost(hostname)`** | Resolves a hostname to an IP address. |
| **`urlencode(text)`** | Encodes text for use in URLs (e.g., spaces become `+`). |

---

### Example Output of `urlParse`

For the URL `https://example.com:8443/v1/x?q=1#top`, `urlParse` returns:

```json
{
  "scheme": "https",
  "host": "example.com",
  "port": 8443,
  "path": "/v1/x",
  "query": "q=1",
  "fragment": "top"
}
```
| Function | Description |
|----------|-------------|
| `urlScheme(url)` | Extracts the URL scheme (e.g., `https`). |
| `urlHost(url)` | Extracts the host and port (e.g., `example.com:8080`). Default ports are omitted. |
| `urlPath(url)` | Extracts the path (e.g., `/api/v1`). |
| `urlParse(url)` | Returns a JSON object with `scheme`, `host`, `port`, `path`, `query`, and `fragment`. |
| `getHostname()` | Returns the local hostname. |
| `resolveHost(hostname)` | Resolves a hostname to an IP address. |
| `urlencode(text)` | Encodes text for use in URLs (e.g., spaces become `+`).

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
