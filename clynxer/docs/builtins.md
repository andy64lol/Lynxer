# Built-in functions

Functions implemented directly by the Clynxer interpreter. They can be called
by their bare name or with a `global.` prefix (`print(...)` and
`global.print(...)` are the same call).

Inside a module, `global.name(...)` always resolves to a *core builtin*, never to
the module's own function of that name — see
[limitations](limitations.md#module-self-calls).

Names that are known but not implemented (for example `rawPy`, `ffiCall`,
`nativeModuleLoad`, `async*`, `sound*`, and mutual-exclusion primitives) fail
with `<name>() is not supported in CLynxer yet`. The `syscall*` family is routed
to a generic syscall dispatcher instead.

## Input, output and conversion

| Builtin | Notes |
| --- | --- |
| `print(value...)` | Writes each argument with no separator |
| `println(value...)` | Like `print` plus a trailing newline |
| `input([prompt])` | Prints the prompt, reads one line from stdin |
| `inputln([prompt])` | As `input`, but keeps the newline |
| `strOf(value)` | Value rendered as text |
| `intOf(value)` | Value converted to an integer |
| `floatOf(value)` | Value converted to a float |
| `sentinel([name])` | Creates a unique sentinel value |
| `object()` | Creates an empty object value |
| `returnType(value)` | Type name, e.g. `int`, `float`, `str`, `bool`, `list`, `tuple`, `none` |
| `returnLength(value)` | Length of a `str`, `list` or `tuple` |

## Strings

| Builtin | Notes |
| --- | --- |
| `charAt(text, index)` | One-character string at `index` |
| `substring(text, start, end)` | Half-open slice `[start, end)` |
| `trim(text)` | Removes leading and trailing whitespace |
| `upper(text)` / `lower(text)` | Case conversion |
| `replace(text, old, new)` | Replaces every occurrence |
| `splitStr(text, separator)` | Splits into a list |
| `contains(container, value)` | Membership test for a **list or tuple** only |

## Sequences

| Builtin | Notes |
| --- | --- |
| `range(stop)` / `range(start, stop)` / `range(start, stop, step)` | Integer sequence |
| `seqFromTo(start, stop, step)` | Integer sequence, `stop` exclusive |
| `listJsonArray(list)` | Encodes a list as a JSON array string |
| `listJsonObject(flatList)` | Encodes alternating key/value pairs as a JSON object |
| `listFlatten(list)` / `listUnique(list)` | Flatten / de-duplicate |
| `listPush(list, value)` | Appends, returns the list |
| `listPop(list)` | Removes and returns the last element |
| `listGet(list, index)` | Element access |
| `listSet(list, index, value)` | Element assignment |
| `listSlice(list, start, stop)` | Half-open slice |
| `listContains(list, value)` | Membership |
| `listJoin(list, separator)` | Joins elements into a string |
| `listIndex(list, value)` | Index of a value |
| `listRemove(list, index)` | Removes at an index |
| `anyOf(list)` / `allOf(list)` | Truthiness reductions |
| `sumOf(list)` | Numeric sum |
| `sortList(list[, reverse])` | Sorts in place / returns the list |
| `reverseList(list)` | Reverses |
| `listMin(list)` / `listMax(list)` | Extremes |
| `listFirst(list)` / `listLast(list)` | Ends |
| `listHead(list, count)` / `listTail(list, count)` | Prefix / suffix |
| `listCount(list, value)` | Number of occurrences |
| `listExtend(list, other)` | Concatenates in place |
| `listInsert(list, index, value)` | Inserts |
| `listClear(list)` | Empties |
| `listRepeat(value, count)` | List of `count` copies |
| `listAvg(list)` | Arithmetic mean |
| `listZip(first, second)` | Pairs elements |

## Tuples

`tupleCreate(...)`, `tupleGet(t, i)`, `tupleLen(t)`, `tupleContains(t, v)`,
`tupleIndex(t, v)`, `tupleSlice(t, start, stop)`, `tupleToList(t)`,
`listToTuple(list)`, `tupleConcat(a, b)`, `tupleCount(t, v)`, `tupleFirst(t)`,
`tupleLast(t)`, `tupleJsonArray(t)`, `tupleReverse(t)`, `tupleSort(t)`,
`tupleSortDesc(t)`, `tupleMin(t)`, `tupleMax(t)`, `tupleSum(t)`, `tupleAny(t)`,
`tupleAll(t)`, `tupleUnique(t)`, `tupleMean(t)`, `tupleFlatten(t)`,
`tupleZip(a, b)`, `tupleJoin(t, separator)`.

## Control, runtime and assertions

| Builtin | Notes |
| --- | --- |
| `assert(condition[, message])` | Fails with the message when the condition is false |
| `sleep(seconds)` | Suspends the current process |
| `foreverDelay(seconds)` | Sets the `forever` loop delay (only in `setup()`) |
| `suppressForeverWarning()` | Silences the empty-`forever` warning (only in `setup()`) |
| `suppressDeprecationWarning()` | Silences legacy-syntax warnings (only in `setup()`) |
| `overrideMain(name)` | Runs `name` instead of `main` (only in `setup()`) |

## Bundled files

Available to programs run from a compiled executable; a normal interpreted run
returns empty results.

| Builtin | Notes |
| --- | --- |
| `bundledFile(name)` | Filesystem path of a file included with `--include`, or `""` when the program is not compiled or does not carry that file |
| `bundledFiles()` | List of the bare names of every included file |

Included files are written to a private temporary directory when the executable
starts, and that directory is removed when the process exits.

```lynx
global setup(){ import("fileIO"); }

global main(){
    println(bundledFiles());
    println(global.fileIO.readFile(bundledFile("message.txt")));
}
```

## Native memory

| Builtin | Notes |
| --- | --- |
| `memoryAllocate(size)` | Allocates raw bytes |
| `memoryAllocateZeroed(count, size)` | Allocates zeroed elements |
| `memoryReallocate(address, size)` | Resizes an allocation |
| `memoryFree(address)` | Releases an allocation |
| `memorySet(address, value, size)` | Fills memory |
| `memoryCopy(destination, source, size)` | Copies memory |
| `memoryRead<Type>(address, offset)` | Typed read; `<Type>` is one of `byte`, `int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32`, `int64`, `uint64`, `float32`, `float64` |
| `memoryWrite<Type>(address, offset, value)` | Typed write |
| `memoryReadEndian(address, offset, type, order)` | Read with explicit byte order (`little`/`le`, `big`/`be`) |
| `memoryWriteEndian(address, offset, type, order, value)` | Write with explicit byte order |
| `memoryTypeSize(type)` / `memoryTypeAlignment(type)` | Layout queries |
| `sizeOf(typeName)` | Size of a named type |

## Syscalls

`syscall*(...)` entries take integer arguments only (at most six) and dispatch
through the platform syscall layer. `syscallPollFileDescriptors` takes three
arguments; `syscallPpollFileDescriptors`, `syscallWaitForEvents` and
`syscallWaitForEventsWithSignalMask` take five.

## Managed filesystem

A small handle-based filesystem API, separate from the `fileIO`, `os` and `path`
stdlib modules. Handles are non-negative integers; the interpreter owns the
descriptor behind one, and an unknown or already-closed handle is a runtime
error rather than a silent failure. Failures preserve the operation and the
original errno in the message, for example
`filesystemOpen() failed: [2] No such file or directory`. Descriptors a program
leaves open are closed when the process exits.

| Builtin | Notes |
| --- | --- |
| `filesystemOpen(path, mode, permissions?)` | Returns a file handle. Modes are `r`, `w`, `a`, `r+`, `w+`, `a+`; permissions default to `0666` |
| `filesystemRead(handle, maxBytes)` | Reads and returns UTF-8 text; bytes that are not valid UTF-8 become U+FFFD |
| `filesystemWrite(handle, data)` | Writes UTF-8 text and returns the byte count |
| `filesystemClose(handle)` | Closes the descriptor and releases the handle |
| `filesystemStat(path)` | JSON `{type, size, mode, modifiedTime, accessTime, changeTime}`. Does not follow a symlink, so `type` can be `symlink`, `file`, `dir` or `other` |
| `filesystemList(path)` | Sorted direct child names |
| `filesystemMkdir(path, parents?)` | Creates a directory; `parents` also creates missing parents and tolerates an existing directory |
| `filesystemRemove(path)` | Removes a file, symlink or empty directory |
| `filesystemRename(source, target)` | Renames an entry |
| `filesystemLink(source, target, symbolic?)` | Hard link, or a symbolic link when `symbolic` is true |
| `filesystemReadLink(path)` | Reads a symbolic link target |
| `filesystemChmod(path, mode)` | Sets numeric permission bits |

The family follows the Python reference exactly, including its error text. One
consequence is worth noting: the reference's `Number.null` is `0`, so the
operations that yield "no value" (`filesystemClose`, `filesystemRemove`,
`filesystemRename`, `filesystemLink`, `filesystemChmod`) return `0` rather than
Clynxer's own `none`.

On a host without POSIX `open`/`stat`/`dirent`, the whole family stays in the
unsupported set.

## Managed processes

A managed subprocess abstraction: each child gets one pipe per standard stream
and a handle the program owns. Commands are **not** shell-parsed — the first
argument names an executable and the second is its argv, so shell syntax needs
an explicit shell. Closing the handle closes every pipe and terminates a child
that is still running.

| Builtin | Notes |
| --- | --- |
| `processSpawn(command, arguments, environment?)` | Returns a process handle. `arguments` is a list of strings; `environment` is an optional list of `KEY=VALUE` strings that overrides those keys and inherits the rest |
| `processWrite(handle, data)` | Writes UTF-8 data to stdin and returns the byte count |
| `processCloseInput(handle)` | Closes stdin so the child sees end-of-file |
| `processRead(handle, stream, maxBytes)` | Reads up to `maxBytes` from `"stdout"` or `"stderr"`, blocking until that many bytes or end-of-file. Bytes that are not valid UTF-8 become U+FFFD |
| `processPoll(handle)` | Returns `-1` while the child runs, otherwise its exit status |
| `processWait(handle, timeoutSeconds)` | Waits up to the timeout; `-1` on timeout, otherwise the exit status |
| `processSendSignal(handle, signal)` | Sends an operating-system signal |
| `processClose(handle)` | Closes the pipes, terminates a running child, and releases the handle |

A child killed by a signal reports the **negative** signal number, matching
POSIX convention. Poll and wait report `-1` both for "still running" and for a
timeout, which is why the reference pairs a timeout with a following poll.

Unknown, already closed or invalid handles are runtime errors rather than silent
failures, as are writes to a stream the program has already closed.

SIGPIPE is ignored for the whole process, as it is in the reference: writing to
a pipe whose reader has exited must report `EPIPE`, not kill the interpreter.

The family follows the Python reference exactly, including its error text, and
the value-less operations return `0` for the same `Number.null` reason as the
filesystem family.

On a host without POSIX `fork`/`pipe`/`waitpid`, the whole family stays in the
unsupported set.

## Managed networking

Managed TCP, UDP and Unix-domain sockets. Addresses stay as host/path strings
plus an integer port, so no native address structure crosses the API. Sockets
are closed explicitly; anything left open is closed when the process exits.

| Builtin | Notes |
| --- | --- |
| `networkingOpen(kind)` | Creates a socket and returns a handle. `kind` is `tcp`, `udp` or `unix` (case-insensitive) |
| `networkingBind(handle, address, port?)` | Binds to an IPv4 host and port, or to a Unix socket path when the port is omitted |
| `networkingListen(handle, backlog?)` | Listens on a stream socket; the backlog defaults to 128 |
| `networkingAccept(handle)` | Accepts a connection and returns a **new** handle |
| `networkingConnect(handle, address, port?)` | Connects to an IPv4 host and port, or to a Unix socket path |
| `networkingSend(handle, data)` | Sends UTF-8 data and returns the byte count |
| `networkingReceive(handle, maxBytes)` | Receives and returns UTF-8 text; invalid bytes become U+FFFD |
| `networkingClose(handle)` | Closes the socket and releases the handle |
| `networkingShutdown(handle, how)` | Shuts down `read`, `write` or `both` |
| `networkingBlocking(handle, enabled)` | Enables or disables blocking mode |
| `networkingOption(handle, name, value)` | Sets `reuseAddr`, `keepAlive` or `broadcast` to an integer value |
| `networkingResolve(host, port)` | Resolves a host over `SOCK_STREAM` and returns the **sorted, de-duplicated** list of address strings |
| `networkingAddress(handle)` | JSON for the local address: `["127.0.0.1",41234]` for IPv4, or the path string for a Unix socket |

`networkingAccept` and `networkingOpen` both allocate handles from the same
registry, so handles are not reusable indices into a fixed table.

`networkingSend` performs a single `send`, so a short write is possible on a
stream socket; the returned count is what actually left. `networkingReceive`
performs a single `recv`.

The family follows the Python reference exactly, including its error text, and
the value-less operations return `0` for the same `Number.null` reason as the
filesystem family.

On a host without POSIX sockets, the whole family stays in the unsupported set.
