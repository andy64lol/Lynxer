# winpipe

Blocking Windows named pipes.

**Backend:** native — `stdlib/winpipe.so`, built from `stdlib/winpipe.cpp`.
Windows-only; the module is not built or available on Linux or macOS.

**Import:** `import("winpipe")` → `winpipe.*`

Names are relative to `\\.\pipe\`; a full `\\.\pipe\name` path is accepted too.
The server creates a handle with `create` and blocks in `connect` until a client
arrives. The client calls `open`, which retries for five seconds while the
server is still creating the pipe. Reads and writes are blocking. Every handle
must be released with `close`.

## Tearing the pipe down

`disconnect` ends the connection and **discards the data still in the pipe**,
so a server that replies and then disconnects right away can lose the reply:
the client is left with a broken pipe and `read` returns `""` instead of the
answer. Wait for the client to close its end before disconnecting — a blocking
`read` returns as soon as the client closes the pipe.

```lynx
winpipe.write(server, "pong");
// Wait for the client to close; the read returns on the broken pipe.
winpipe.read(server, 16);
winpipe.disconnect(server);
```

The same rule applies to `close`: a client that reads after the server closed
the pipe gets `""`.

## Functions

| Function | Signature | Returns |
| --- | --- | --- |
| `create` | `(str name)` | a server handle, or `-1` |
| `connect` | `(int handle)` | `true` / `false` |
| `open` | `(str name)` | a client handle, or `-1` |
| `read` | `(int handle, int length)` | up to `length` bytes, or `""` |
| `write` | `(int handle, str text)` | bytes written, or `-1` |
| `disconnect` | `(int handle)` | `true` / `false` |
| `peek` | `(int handle)` | bytes available, or `-1` |
| `close` | `(int handle)` | `true` / `false` |

## Example

```lynx
fn setup(){ import("winpipe"); }

fn main(){
    int server = winpipe.create("lynxer-demo");
    // In another process:
    //   int client = winpipe.open("lynxer-demo");
    winpipe.connect(server);
    println(winpipe.read(server, 64));
    winpipe.write(server, "pong");
    // Wait for the client to close before the pipe goes away.
    winpipe.read(server, 16);
    winpipe.disconnect(server);
    winpipe.close(server);
}
```
