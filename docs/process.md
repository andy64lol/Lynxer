# Process API

Process built-ins provide a managed subprocess abstraction. They use the
platform process facilities while keeping the interface available directly in
Lynxer programs.

## Spawn

### `processSpawn(command, arguments[, environment])`

Starts `command` without a shell. `arguments` is a list of strings passed as
argv after the command. The optional `environment` is a list of `KEY=VALUE`
strings; it overrides those keys while inheriting the rest of the parent
environment. The function returns a numeric process handle.

```lynx
int child = processSpawn(
    "/usr/bin/python3",
    [str "-c", str "print('hello')"],
    [str "APP_MODE=test"]
);
```

Commands are not shell-parsed. Use an explicit shell executable if shell
syntax is intentionally required.

## Pipes

Every spawned process has separate stdin, stdout, and stderr pipes.

| Function | Description |
| --- | --- |
| `processWrite(handle, data)` | Writes UTF-8 data to stdin and returns the byte count |
| `processCloseInput(handle)` | Closes stdin so the child can observe end-of-file |
| `processRead(handle, stream, maxBytes)` | Reads up to `maxBytes` UTF-8 bytes from `stdout` or `stderr` |

Reads are blocking until data or end-of-file is available. To avoid a child
waiting forever for input, close stdin with `processCloseInput` when no more
input will be written.

## Waiting and signals

| Function | Description |
| --- | --- |
| `processPoll(handle)` | Returns `-1` while running, otherwise the exit status |
| `processWait(handle, timeoutSeconds)` | Waits up to the timeout; returns `-1` on timeout or the exit status |
| `processSendSignal(handle, signal)` | Sends a numeric operating-system signal |
| `processClose(handle)` | Closes all pipes, terminates a running child, and releases the handle |

Negative exit statuses generally indicate termination by a signal on POSIX
systems. A timeout result of `-1` is distinct from a completed process only
when checked with `processPoll` afterward.

Process handles are owned by the program. Close every handle after collecting
the output and exit status. Unknown, already closed, invalid, or failed
process operations return Lynxer runtime errors rather than silently failing.