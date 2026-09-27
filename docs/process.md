# Process API

A managed subprocess abstraction exposed through the `process*` built-ins over
the platform's process facilities.

## Spawn

### `processSpawn(command, arguments[, environment])`

Starts `command` **without a shell**. `arguments` is a list of strings passed as
argv after the command. The optional `environment` is a list of `KEY=VALUE`
strings; those keys override the inherited parent environment while the rest is
inherited. It returns a numeric process handle.

```lynx
global setup(){}

global main(){
    int child = processSpawn(
        "/bin/sh",
        [str "-c", str "printf hi"],
        [str "APP_MODE=test"]
    );
    processCloseInput(child);
    println(processRead(child, "stdout", 64));   // hi
    println(processWait(child, 5));              // 0
    processClose(child);
}
```

Commands are not shell-parsed; use an explicit shell executable when shell
syntax is intended.

## Pipes

Every spawned process has separate stdin, stdout and stderr pipes.

| Function | Notes |
| --- | --- |
| `processWrite(handle, data)` | Writes UTF-8 data to stdin and returns the byte count. |
| `processCloseInput(handle)` | Closes stdin so the child sees end-of-file. |
| `processRead(handle, stream, maxBytes)` | Reads up to `maxBytes` UTF-8 bytes from `"stdout"` or `"stderr"`. |

Reads block until data or end-of-file is available. Close stdin with
`processCloseInput` when no more input will be written, so a child waiting for
input does not hang.

## Waiting and signals

| Function | Notes |
| --- | --- |
| `processPoll(handle)` | Returns `-1` while running, otherwise the exit status. |
| `processWait(handle, timeoutSeconds)` | Waits up to the timeout; returns `-1` on timeout, otherwise the exit status. |
| `processSendSignal(handle, signal)` | Sends a numeric operating-system signal. |
| `processClose(handle)` | Closes all pipes, terminates a running child, and releases the handle. |

A command's exit code is reported directly (a child that exits `3` gives `3`).
Negative statuses generally indicate termination by a signal on POSIX. A timeout
result of `-1` is indistinguishable from a running process unless checked with
`processPoll` afterwards.

Handles are owned by the program: close every handle after collecting the output
and status. Unknown, already closed, invalid, or failed operations raise
runtime errors rather than failing silently.
`lynxer/examples/builtin_process.lynx` exercises every function above.
