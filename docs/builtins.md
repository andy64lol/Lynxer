# Built-in functions

Functions implemented directly by the Lynxer interpreter. They can be called
by their bare name or with a `global.` prefix (`print(...)` and
`global.print(...)` are the same call).

Inside a module, `global.name(...)` always resolves to a *core builtin*, never to
the module's own function of that name — see
[limitations](limitations.md#language-and-toolchain).

Names that Lynxer recognises but does not implement fail with a source-located
`<name>() is not supported in Lynxer yet`. On a Linux/POSIX build the
**supported** families include the `ffi*`, `async*`, `sound*`, `filesystem*`,
`process*`, `networking*`, `nativeThread*` and `syscall*` builtins; the
unsupported set is listed under [Unsupported names](#unsupported-names) and is
defined by `unsupportedTable()` in `lynxer/builtins.cpp`. On a build without
POSIX support, the `filesystem*`/`process*`/`networking*`/`sound*` families join
the unsupported set. The `syscall*` family is routed to a generic syscall
dispatcher rather than to a per-name handler.

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
| `charCode(value)` | Byte value of a `char` or of the first byte of a `str`; `-1` for an empty string |
| `charOf(code)` | One-byte `char` for a code in `0..255` |
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
| `listFlatten(list)` / `listUnique(list)` | Flatten / de-duplicate, returning a new list |
| `listPush(list, value)` | Appends, **returning a new list** — the argument is unchanged |
| `listPop(list)` | Returns the **last element**; the list is unchanged |
| `listGet(list, index)` | Element access (negative indices count from the end) |
| `listSet(list, index, value)` | Returns a **new list** with the element replaced |
| `listSlice(list, start, stop)` | Half-open slice |
| `listContains(list, value)` | Membership |
| `listJoin(list, separator)` | Joins elements into a string |
| `listIndex(list, value)` | Index of a value, or `-1` |
| `listRemove(list, index)` | Returns a new list without that element |
| `anyOf(list)` / `allOf(list)` | Truthiness reductions |
| `sumOf(list)` | Numeric sum |
| `sortList(list[, reverse])` | Returns a **sorted copy**; the argument is unchanged |
| `reverseList(list)` | Returns a reversed copy |
| `listMin(list)` / `listMax(list)` | Extremes |
| `listFirst(list)` / `listLast(list)` | Ends |
| `listHead(list, count)` / `listTail(list, count)` | Prefix / suffix |
| `listCount(list, value)` | Number of occurrences |
| `listExtend(list, other)` | Returns a new list with `other` appended |
| `listInsert(list, index, value)` | Returns a new list with `value` inserted |
| `listClear(list)` | Returns an empty list |
| `listRepeat(value, count)` | List of `count` copies |
| `listAvg(list)` | Arithmetic mean |
| `listZip(first, second)` | Pairs elements |

## Tuples

| Builtin | Notes |
| --- | --- |
| `tupleCreate(v1, v2, ...)` | Creates a tuple from any number of arguments |
| `tupleGet(tuple t, int idx)` | Gets element at `idx` (negative indices supported) |
| `tupleLen(tuple t)` | Number of elements |
| `tupleContains(tuple t, any val)` | `true` if `val` is in the tuple |
| `tupleIndex(tuple t, any val)` | First index of `val`, or `-1` |
| `tupleSlice(tuple t, int start, int stop)` | Sub-tuple `[start, stop)` |
| `tupleToList(tuple t)` | Converts to a mutable list |
| `listToTuple(list l)` | Converts a list to a tuple |
| `tupleConcat(tuple t1, tuple t2)` | Concatenates two tuples |
| `tupleCount(tuple t, any val)` | Counts occurrences of `val` |
| `tupleFirst(tuple t)` | First element (error on empty) |
| `tupleLast(tuple t)` | Last element (error on empty) |
| `tupleJsonArray(tuple t)` | JSON array string, e.g. `"[1,2,3]"` |
| `tupleReverse(tuple t)` | New tuple with elements in reversed order |
| `tupleSort(tuple t)` | New tuple sorted ascending |
| `tupleSortDesc(tuple t)` | New tuple sorted descending |
| `tupleMin(tuple t)` | Minimum element |
| `tupleMax(tuple t)` | Maximum element |
| `tupleSum(tuple t)` | Sum of all numeric elements |
| `tupleAny(tuple t)` | `true` if any element is truthy |
| `tupleAll(tuple t)` | `true` if all elements are truthy |
| `tupleUnique(tuple t)` | New tuple with duplicates removed (order preserved) |
| `tupleMean(tuple t)` | Arithmetic mean of numeric elements |
| `tupleFlatten(tuple t)` | One-level flatten: concatenates nested tuple elements |
| `tupleZip(tuple t1, tuple t2)` | **Tuple** of JSON pair strings `{"a": 1, "b": 2}` |
| `tupleJoin(tuple t, str sep)` | All elements joined as a string with separator |
| `contains(tuple t, any val)` | Common membership test shared with lists |

### Notes
- Tuples are **immutable**; operations like `tupleConcat` and `tupleSlice` return a **new** tuple.
- Use `tupleToList()` to iterate over a tuple with a `for` loop or index manually.

## Control, runtime and assertions

| Builtin | Notes |
| --- | --- |
| `assert(condition[, message])` | Fails with the message when the condition is false |
| `sleep(seconds)` | Suspends the current process |
| `foreverDelay(seconds)` | Sets the `forever` loop delay (only in `setup()`) |
| `suppressForeverWarning()` | Silences the empty-`forever` warning (only in `setup()`) |
| `suppressDeprecationWarning()` | Silences legacy-syntax warnings (only in `setup()`) |
| `overrideMain(name)` | Runs `name` instead of `main` (only in `setup()`) |

## Ownership and borrowing

The arguments are **variable names**, not values; see
[language.md](language.md#shared-variables).

| Builtin | Notes |
| --- | --- |
| `varTransfer(source, destination)` | Moves a value into an already-declared destination |
| `varTransferMutate(source, destination)` | Move form for an `any`/`num` destination |
| `varBorrow(source, borrower)` | Read-only tracked alias |
| `varBorrowMutate(source, borrower)` | Exclusive mutable alias sharing one storage |
| `varEndBorrow(borrower)` | Ends a borrow; the borrower keeps an independent copy |
| `unshare(borrower)` | Alias of `varEndBorrow`; detaches a `shared` variable |
| `borrowing(variable)` | Whether the variable is a borrower |
| `beingBorrowed(variable)` | Whether another variable borrows from it |
| `varSwapAll(first, second)` | Exchanges values and declared types |
| `varSwapVal(first, second)` | Exchanges values, keeping declared types |

`shared <type> name = variable;` is the declaration form of
`varBorrowMutate`; the source may still be written while the alias is active.

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
    list files = bundledFiles();
    println(files);                 // [] in an interpreted run
    if (returnLength(files) > 0) {
        println(global.fileIO.readFile(bundledFile("message.txt")));
    }
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
| `memoryReadByte(address, offset)` / `memoryWriteByte(address, offset, value)` | 1-byte unsigned access |
| `memoryReadInt8` / `memoryWriteInt8`, `memoryReadUInt8` / `memoryWriteUInt8` | 8-bit access |
| `memoryReadInt16` / `memoryWriteInt16`, `memoryReadUInt16` / `memoryWriteUInt16` | 16-bit access |
| `memoryReadInt32` / `memoryWriteInt32`, `memoryReadUInt32` / `memoryWriteUInt32` | 32-bit access |
| `memoryReadInt64` / `memoryWriteInt64`, `memoryReadUInt64` / `memoryWriteUInt64` | 64-bit access |
| `memoryReadFloat32` / `memoryWriteFloat32`, `memoryReadFloat64` / `memoryWriteFloat64` | floating-point access |
| `memoryReadEndian(address, offset, type, order)` | Read with explicit byte order (`little`/`le`, `big`/`be`). `type` is a **lowercase string**: `byte`, `int8`…`uint64`, `float32`, `float64` |
| `memoryWriteEndian(address, offset, type, order, value)` | Write with explicit byte order |
| `memoryTypeSize(type)` / `memoryTypeAlignment(type)` | Layout queries for a lowercase type string |
| `sizeOf(typeName)` | Size of a C type name (`char`, `int`, `long`, `void*`, `size_t`, `int64`, …) |

### Typed blocks, structs and owned handles

The original used integer addresses instead of pointers; these built-ins are
the typed, bounds-checked form. A **block** remembers an element type and count,
a **struct** remembers its field layout, and an owned **handle** shares one
liveness flag across every copy of the handle value.

| Builtin | Notes |
| --- | --- |
| `memoryBlockAllocate(type, count)` | Allocates `count` elements of a lowercase type |
| `memoryBlockView(address, type, count)` | Registers an existing allocation as a typed view over `count` elements |
| `memoryBlockGet(address, index)` / `memoryBlockSet(address, index, value)` | Bounds-checked element access |
| `memoryBlockLength(address)` | Element count |
| `memoryArrayAllocate/View/Get/Set/Length` | Aliases of the block API |
| `memoryViewGet/Set/Length` | Aliases of the block API |
| `nativeStructSize(layout)` | Total size, including native alignment |
| `nativeStructAlignment(layout)` | Maximum field alignment in bytes |
| `nativeStructFieldCount(layout)` | Number of fields |
| `nativeStructFieldOffset(layout, field)` | Byte offset of a field |
| `nativeStructFieldSize(layout, field)` / `nativeStructFieldType(layout, field)` | Field size / declared type |
| `nativeStructAllocate(layout)` | Allocates a struct for a layout |
| `nativeStructGet(address, field)` / `nativeStructSet(address, field, value)` | Type-checked field access |
| `memoryStructSize/Alignment/FieldCount/FieldOffset/FieldSize/FieldType/Allocate/Get/Set` | Aliases of the `nativeStruct*` API |
| `nativeTypeAlignment(type)` | Alias of `memoryTypeAlignment` |
| `nativeHandleAllocate(size)` | Owns an allocation; returns an integer handle |
| `nativeHandleAddress(handle)` | The underlying address; errors once the handle is freed |
| `nativeHandleIsAlive(handle)` | `true` until the handle is freed |
| `nativeHandleFree(handle)` | Frees the allocation; every copy of the handle then reports freed |

A layout is a comma-separated list of `type name` fields, where the type is one
of the lowercase memory types above:

```lynx
str layout = "int32 id, float64 score";
println(nativeStructSize(layout));                  // 16
println(nativeStructFieldOffset(layout, "score"));  // 8

int record = nativeStructAllocate(layout);
nativeStructSet(record, "id", 7);
nativeStructSet(record, "score", 12.5);
println(nativeStructGet(record, "score"));          // 12.5
memoryFree(record);
```

### Raw addresses and native calls

An address is a Lynxer integer. These built-ins validate a data address against
the allocation registry and wrap a raw function address for `nativeCall`.

| Builtin | Notes |
| --- | --- |
| `getAddress(value)` | Validates an integer as a live native allocation and returns it |
| `getAddressValue(address)` | Reads an `int64` from the address |
| `modifyAddressValue(address, value)` | Writes an `int64` to the address |
| `functionAddress(address)` / `nativeFunctionAddress(address)` | Wraps a non-zero function address (a declared type name too) |
| `nativeCall(address, signature, arguments)` | Calls a native function pointer; `signature` is `returnType(paramType,...)` and `arguments` is a list |

`nativeCall` uses the interpreter's deliberately small integer ABI: only the
fixed shapes the runtime supports are accepted, and an unsupported signature is
a source-located error. The caller must supply a valid address with a
compatible ABI — an invalid address can crash the process.

```lynx
any lib = ffiLoadLibrary("libc.so.6");
functionAddress strlen = ffiLookup(lib, "strlen");
println(nativeCall(strlen, "uint64(cstring)", ["hello"]));  // 5
ffiCloseLibrary(lib);
```

Every typed read/write validates the address against the allocation registry and
checks the access against the allocation's length, so an unknown address, a
freed address, and an out-of-bounds access are source-located errors rather than
crashes. 64-bit writes preserve the full signed and unsigned range (see
[types.md](types.md)).

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

The family is a faithful native implementation, including its error text. One
consequence is worth noting: the reference's `Number.null` is `0`, so the
operations that yield "no value" (`filesystemClose`, `filesystemRemove`,
`filesystemRename`, `filesystemLink`, `filesystemChmod`) return `0` rather than
Lynxer's own `none`.

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

The family is a faithful native implementation, including its error text, and
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

The family is a faithful native implementation, including its error text, and
the value-less operations return `0` for the same `Number.null` reason as the
filesystem family.

On a host without POSIX sockets, the whole family stays in the unsupported set.

## Managed sound

Audio playback for programs that do not want to import a module. These built-ins
are a thin layer over the bundled Rust **`sound` stdlib module** — the module
owns the audio backend (`rodio`/`cpal`/`symphonia`), so there is one audio
implementation rather than one per interface, and the interpreter binary keeps
no audio dependency. The built-in layer owns its own handle registry: a handle
is valid only if `soundLoad` returned it, and `soundRelease` invalidates it.

| Builtin | Notes |
| --- | --- |
| `soundLoad(path)` | Returns a handle. The file must exist and end in `.wav`, `.ogg`, `.mp3` or `.flac` |
| `soundPlay(handle)` | Plays once |
| `soundLoop(handle)` | Plays, looping |
| `soundStop(handle)` | Stops playback |
| `soundPause(handle)` / `soundResume(handle)` | Pause and resume an active player |
| `soundSetVolume(handle, volume)` | Volume in `[0, 1]` |
| `soundIsPlaying(handle)` | Whether a player is currently running |
| `soundRelease(handle)` | Releases the handle |

Playback needs an audio device; without one, `soundPlay` and `soundLoop` report
that playback did not start. Loading, `soundStop`, `soundSetVolume`,
`soundIsPlaying` and `soundRelease` are device-independent.

`stdlib/sound.so` must be reachable — next to the interpreter, in `stdlib/`, or
under `lynxer/stdlib`. A **compiled executable** only carries it if the program
also has `import("sound")`, because bundling follows imports.

Three deliberate constraints, all recorded in
[limitations.md](limitations.md): the backend's own failure text is not Arcade's;
`soundPause`/`soundResume` work, where the reference fails by design; and
`soundStop` works, where the reference's Arcade version has no `Player.stop`.

## Native threads

`nativeThreadStart(function, arguments)` runs a Lynxer function on a
`std::thread`. The function is named as a value — `nativeThreadStart(global.worker, [int 42])`
— which is what makes `global.<name>` resolve to a callable when no variable has
that name.

| Builtin | Notes |
| --- | --- |
| `nativeThreadStart(function, arguments)` | Returns a thread handle. `function` is a named global function; `arguments` is a list |
| `nativeThreadJoin(handle)` | Waits for the thread and returns `completed`, or the callback's error text. Joining releases the handle |
| `nativeThreadJoinAll()` | Joins every thread that has neither been joined nor detached |
| `nativeThreadIsAlive(handle)` | Whether the thread is still running |
| `nativeThreadStatus(handle)` | `running` while it is, then `completed` or the error text |
| `nativeThreadDetach(handle)` | Gives up the handle; the thread leaves the registry when it finishes |

**Threads are cooperative.** The interpreter evaluates Lynxer code on one thread
at a time, and a worker takes that lock before calling back in, so a thread runs
while the thread that started it is blocked in `nativeThreadJoin` or
`nativeThreadJoinAll`, which release the lock before waiting. Two threads never
evaluate at once — that is why no data race is possible — and it is why a
worker's own output appears at the join rather than during the main body.
Programs that leave a thread running have it joined when the program finishes.

## FFI

The `ffi*` family loads a native shared library and calls a symbol by signature
at run time, with no build step. It is implemented in C++ over `dlopen`/`dlsym`
(there is no libffi), so the same call site can call any symbol whose signature
is in the supported table.

| Builtin | Notes |
| --- | --- |
| `ffiLoadLibrary(path)` | Loads a shared library and returns a handle |
| `ffiLookup(handle, symbol)` | Resolves a symbol to a `functionAddress` |
| `ffiCall(address, signature, arguments)` | Calls the symbol. `signature` is a packed string such as `"cdecl:int32(int32,int32)"`; `arguments` is a list |
| `ffiCallback(signature, function)` | Wraps a Lynxer function as a C callback the native code can call |
| `ffiFreeCallback(callback)` | Releases a callback created by `ffiCallback` |
| `ffiCloseLibrary(handle)` | Unloads the library and invalidates its symbols |

The signatures use the same grammar as native modules; see
[native-module-abi.md](native-module-abi.md#signature-grammar).
`lynxer/examples/builtin_ffi.lynx` demonstrates the full round trip (calling
`strlen` and passing a Lynxer function back as a C callback).

## Async

The `async*` family performs I/O without a language-level event loop.
`asyncRun(function, arguments?)` starts a Lynxer function in the async runtime,
and `await` in the caller yields until the operation completes. Timers, wakeups
and file/IO readiness sources are registered on a poll set and awaited with
`asyncPollWait`; `asyncPollDispatch` awaits them and invokes a Lynxer callback
for each ready event. Evaluation stays cooperative — the interpreter runs one
Lynxer frame at a time.

| Builtin | Notes |
| --- | --- |
| `asyncRun(function, arguments?)` | Starts a function; returns a handle |
| `asyncGather(values...)` | Collects its arguments into a list |
| `asyncSleep(seconds)` | Suspends the current task for a non-negative duration |
| `asyncPollCreate()` | Creates a poll set and returns a handle |
| `asyncPollRegister(poll, resource, events, token)` | Registers a resource for `read`, `write` or `readwrite` |
| `asyncPollModify(poll, resource, events, token)` | Changes the interest and token |
| `asyncPollRemove(poll, resource)` | Removes a resource |
| `asyncPollWait(poll, timeoutMs?, maxEvents?)` | Awaits ready events |
| `asyncPollDispatch(poll, callback, timeoutMs?, maxEvents?)` | Awaits events and calls a Lynxer callback for each |
| `asyncPollClose(poll)` | Releases the poll set |
| `asyncTimerCreate(poll, milliseconds, token, repeatMs?)` | Schedules a timer |
| `asyncTimerCancel(timer)` | Cancels a timer |
| `asyncWakeupCreate(poll, token)` | Creates a wakeup handle |
| `asyncWakeupSignal(wakeup)` | Signals a wakeup |
| `asyncWakeupClose(wakeup)` | Releases a wakeup |

See `lynxer/examples/builtin_async.lynx` for a runnable example, and
[language.md](language.md) for the `async`/`await` syntax.

## Unsupported names

Names Lynxer recognises but does not implement on Linux/POSIX. Each fails with
`<name>() is not supported in Lynxer yet`, except `embedPy`, whose message is
`Python bridging (embedPy) is not supported in Lynxer`.

| Family | Names |
| --- | --- |
| Python bridging | `rawPy`, `rawPyx`, `cleanRawPyxCache`, `embedPy` |
| Native module introspection | `nativeModuleLoad`, `nativeModuleName`, `nativeModuleFunction`, `nativeModuleConstant`, `nativeModuleType`, `nativeModuleError`, `nativeModuleDependencies`, `nativeModuleClose` |
| Native sync primitives | `nativeMutex*`, `nativeCondition*`, `nativeSemaphore*` |
| Atomics | `atomicLoad`, `atomicStore`, `atomicAdd`, `volatileRead`, `volatileWrite` |
| Memory protection | `memoryProtect` |

The authoritative list is `unsupportedTable()` in `lynxer/builtins.cpp`; this
table is the POSIX-visible subset. A build without POSIX support adds the
`filesystem*`, `process*`, `networking*`, `sound*`, `async*` and `nativeThread*`
families. See [limitations.md](limitations.md) for the rationale.

`sound*`, `ffi*`, `async*` and `nativeThread*` are the families that reach
outside the interpreter (a device, a shared library, a runtime, or a worker
thread); everything else on this page is self-contained.
