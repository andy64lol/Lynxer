# winpipe

Blocking Windows named pipes.

**Backend:** native — `stdlib/winpipe.so`, built from `stdlib/winpipe.cpp`.
Windows-only; the module is not built or available on Linux or macOS.

**Import:** `import("winpipe")` → `global.winpipe.*`

Names are relative to `\\.\pipe\`; a full `\\.\pipe\name` path is accepted too.
The server creates a handle with `create` and blocks in `connect` until a client
arrives. The client calls `open`, which retries for five seconds while the
server is still creating the pipe. Reads and writes are blocking. Every handle
must be released with `close`.

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
global setup(){ import("winpipe"); }

global main(){
    int server = global.winpipe.create("lynxer-demo");
    // In another process:
    //   int client = global.winpipe.open("lynxer-demo");
    global.winpipe.connect(server);
    println(global.winpipe.read(server, 64));
    global.winpipe.write(server, "pong");
    global.winpipe.disconnect(server);
    global.winpipe.close(server);
}
```
