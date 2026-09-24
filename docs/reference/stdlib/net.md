# net

Networking utilities: WebSocket client, TCP client, hostname/IP lookup, and URL parsing.

**Requires:** `pip install websockets`

```c
global setup(){
    import("net");
}
```

---

## WebSocket client

WebSocket connections are identified by a **name** string. Open a connection, then
send/receive messages, then close it when done.

| Function | Signature | Description |
|----------|-----------|-------------|
| `wsConnect` | `wsConnect(str name, str uri)` | Open a WebSocket connection to `uri` and store it under `name`. Returns `"ok"` or `"ERROR: ..."`. |
| `wsSend` | `wsSend(str name, str message)` | Send a text message. Returns `"ok"` or `"ERROR: ..."`. |
| `wsReceive` | `wsReceive(str name)` | Wait for and return the next message, or `"ERROR: ..."`. |
| `wsSendReceive` | `wsSendReceive(str name, str message)` | Send a message and return the next reply (round-trip). |
| `wsClose` | `wsClose(str name)` | Close and remove the connection. |
| `wsConnected` | `wsConnected(str name)` | `true` if the connection is currently open. |

### Example — echo server round-trip

```c
global setup(){
    import("net");
}

global main(){
    // connect to a public echo WebSocket
    str r = global.net.wsConnect("echo", "wss://echo.websocket.org");
    print(r); print("\n");                   // ok

    str reply = global.net.wsSendReceive("echo", "Hello, Lynxer!");
    print(reply); print("\n");               // Hello, Lynxer!

    global.net.wsClose("echo");
}
```

### Example — separate send and receive

```c
global main(){
    global.net.wsConnect("chat", "ws://localhost:8765");

    global.net.wsSend("chat", "join|alice");
    str msg = global.net.wsReceive("chat");
    print(msg); print("\n");

    global.net.wsClose("chat");
}
```

---

## TCP client

Raw TCP connections (no TLS). Identified by a **name** string, just like WebSockets.

| Function | Signature | Description |
|----------|-----------|-------------|
| `tcpConnect` | `tcpConnect(str name, str host, int port)` | Connect to `host:port`. Returns `"ok"` or `"ERROR: ..."`. |
| `tcpSend` | `tcpSend(str name, str data)` | Send UTF-8 text. Returns `"ok"` or `"ERROR: ..."`. |
| `tcpReceive` | `tcpReceive(str name, int bufSize)` | Receive up to `bufSize` bytes. Returns decoded string. |
| `tcpSendReceive` | `tcpSendReceive(str name, str data, int bufSize)` | Send and receive in one call. |
| `tcpClose` | `tcpClose(str name)` | Close and remove the connection. |

### Example — send and receive over TCP

```c
global main(){
    str status = global.net.tcpConnect("srv", "example.com", 80);
    print(status); print("\n");              // ok

    global.net.tcpSend("srv", "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n");
    str resp = global.net.tcpReceive("srv", 4096);
    print(resp); print("\n");

    global.net.tcpClose("srv");
}
```

---

## Hostname and IP utilities

| Function | Signature | Description |
|----------|-----------|-------------|
| `getHostname` | `getHostname()` | Local machine hostname. |
| `getLocalIP` | `getLocalIP()` | Primary local IPv4 address (e.g. `"192.168.1.42"`). |
| `resolveHost` | `resolveHost(str hostname)` | Resolve hostname → IP string. `"ERROR: ..."` on failure. |
| `isPortOpen` | `isPortOpen(str host, int port, int timeoutSecs)` | `true` if the port accepts a TCP connection within `timeoutSecs`. |

### Example

```c
global main(){
    print(global.net.getHostname()); print("\n");
    print(global.net.getLocalIP());  print("\n");
    print(global.net.resolveHost("example.com")); print("\n");  // 93.184.216.34

    if(global.net.isPortOpen("example.com", 80, 3)){
        print("port 80 open\n");
    }
}
```

---

## URL parsing

| Function | Signature | Description |
|----------|-----------|-------------|
| `urlScheme` | `urlScheme(str url)` | Scheme part: `"https"`, `"ws"`, etc. |
| `urlHost` | `urlHost(str url)` | Host (and port) part: `"example.com:8080"`. |
| `urlPath` | `urlPath(str url)` | Path part: `"/api/v1/data"`. |
| `urlParse` | `urlParse(str url)` | Full breakdown as a JSON object: `{scheme, host, port, path, query, fragment}`. |

### Example

```c
global setup(){
    import("net");
    import("json");
}

global main(){
    str url = "https://api.example.com:443/v2/users?page=1#top";
    str parsed = global.net.urlParse(url);
    print(global.json.jsonGet(parsed, "host")); print("\n");    // api.example.com
    print(global.json.jsonGet(parsed, "path")); print("\n");    // /v2/users
    print(global.json.jsonGet(parsed, "query")); print("\n");   // page=1
}
```

---

## HTTP utilities

| Function | Signature | Description |
|----------|-----------|-------------|
| `httpHead` | `httpHead(str url)` | Send `HEAD` request; returns status code (`int`) or `-1`. |
| `ping` | `ping(str host)` | `true` if TCP port 80 on `host` responds within 3 seconds. |

### Example

```c
global main(){
    int code = global.net.httpHead("https://example.com");
    print(code); print("\n");            // 200

    if(global.net.ping("example.com")){
        print("reachable\n");
    }
}
```

---

## Error handling convention

All connection functions return a string starting with `"ERROR: "` on failure.
Check return values before using them:

```c
global main(){
    str r = global.net.wsConnect("demo", "ws://localhost:9999");
    if(global.typing.startsWith(r, "ERROR:")){
        print("connection failed: "); print(r); print("\n");
    }
}
```
