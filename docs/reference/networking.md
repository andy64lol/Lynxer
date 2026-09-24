# Networking API

Lynxer exposes managed TCP, UDP, and Unix-domain sockets through concise
camelCase `networking*` built-ins. Socket handles are closed explicitly with
`networkingClose` and any handles left open are cleaned up when the runtime
exits. Errors include the original operating-system errno.

```lynx
global main() {
    int client = networkingOpen("tcp");
    networkingConnect(client, "127.0.0.1", 9000);
    networkingSend(client, "hello");
    println(networkingReceive(client, 1024));
    networkingClose(client);
}
```

## Functions

| Function | Description |
|---|---|
| `networkingOpen(kind)` | Create a `tcp`, `udp`, or `unix` socket. |
| `networkingBind(handle, address, port?)` | Bind to an IPv4 host/port or Unix socket path. |
| `networkingListen(handle, backlog?)` | Begin listening on a stream socket. |
| `networkingAccept(handle)` | Accept a connection and return a new managed handle. |
| `networkingConnect(handle, address, port?)` | Connect to an IPv4 host/port or Unix socket path. |
| `networkingSend(handle, data)` | Send UTF-8 data and return the byte count. |
| `networkingReceive(handle, maxBytes)` | Receive UTF-8 data. |
| `networkingClose(handle)` | Close a socket handle. |
| `networkingShutdown(handle, how)` | Shut down `read`, `write`, or `both` directions. |
| `networkingBlocking(handle, enabled)` | Enable or disable blocking mode. |
| `networkingOption(handle, name, value)` | Set `reuseAddr`, `keepAlive`, or `broadcast` to an integer value. |
| `networkingResolve(host, port)` | Resolve a host and return a list of IPv4/IPv6 address strings. |
| `networkingAddress(handle)` | Return the local address as JSON. |

Use `networkingBind` with a Unix socket path for local IPC. UDP sockets use
the same open/bind/send/receive operations; connected UDP sockets can use
`networkingConnect` and `networkingSend`.