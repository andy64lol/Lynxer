# Built-in functions

Functions implemented directly by the Clynxer interpreter. They can be called
by their bare name or with a `global.` prefix (`print(...)` and
`global.print(...)` are the same call).

Inside a module, `global.name(...)` always resolves to a *core builtin*, never to
the module's own function of that name — see
[limitations](limitations.md#module-self-calls).

Names that are known but not implemented (for example `rawPy`, `ffiCall`,
`nativeModuleLoad`, `async*`, `sound*`, the `process*`/`filesystem*`/
`networking*` families, and mutual-exclusion primitives) fail with
`<name>() is not supported in CLynxer yet`. The `syscall*` family is routed to a
generic syscall dispatcher instead.

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
