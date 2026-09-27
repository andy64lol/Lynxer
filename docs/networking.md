# Networking API

Managed TCP, UDP and Unix-domain sockets exposed through the `networking*`
built-ins. Sockets are opened with `networkingOpen` and are released when the
program closes them or when the process exits. Errors include the original
operating-system errno.

```lynx
global setup(){}

global main(){
    int client = networkingOpen("tcp");
    networkingConnect(client, "127.0.0.1", 9000);
    networkingSend(client, "hello");
    println(networkingReceive(client, 1024));
    networkingClose(client);
}
```

## Functions

| Function | Notes |
| --- | --- |
| `networkingOpen(kind)` | Creates a `tcp`, `udp` or `unix` socket. Any other kind is an error. |
| `networkingBind(handle, address, port?)` | Binds to an IPv4 host/port or a Unix socket path. Port `0` asks the kernel for an ephemeral port. |
| `networkingListen(handle, backlog?)` | Listens on a stream socket (default backlog `128`). |
| `networkingAccept(handle)` | Accepts a connection and returns a new managed handle. |
| `networkingConnect(handle, address, port?)` | Connects to an IPv4 host/port or a Unix socket path. |
| `networkingSend(handle, data)` | Sends UTF-8 data and returns the byte count. |
| `networkingReceive(handle, maxBytes)` | Receives UTF-8 data. |
| `networkingClose(handle)` | Closes a socket handle. |
| `networkingShutdown(handle, how)` | Shuts down `read`, `write` or `both`. |
| `networkingBlocking(handle, enabled)` | Enables or disables blocking mode. |
| `networkingOption(handle, name, value)` | Sets `reuseAddr`, `keepAlive` or `broadcast` to an integer value. |
| `networkingResolve(host, port)` | Resolves a host and returns a list of address strings, e.g. `[127.0.0.1, ::1]`. |
| `networkingAddress(handle)` | Returns the local address as JSON. |

## Address shapes

`networkingAddress` returns different JSON depending on the socket family:

- IPv4: a JSON array `["host", port]`, e.g. `["127.0.0.1",55319]`.
- Unix domain: a JSON string holding the path.
- IPv6 local addresses are not reported.

## Notes

Use `networkingBind` with a Unix socket path for local IPC. UDP sockets use the
same open/bind/send/receive operations; a connected UDP socket can use
`networkingConnect` then `networkingSend`. `lynxer/examples/builtin_networking.lynx`
exercises every function above.
