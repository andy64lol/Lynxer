# `try` / `catch` / `finally`

Runtime errors have stable diagnostic codes and human-readable messages.
`try` can select handlers by error code, retain the legacy string catch-all, and
run cleanup in an optional `finally` block.

## Syntax

```lynx
try {
    int value = 1 /% 0;
} catch (divisionByZero) {
    println("matched code: ", exceptionCode());
} catch (str error) {
    println("other failure: ", error);
} finally {
    println("cleanup");
}
```

`catch(errorCode)` matches only the named diagnostic code and does not declare a
variable. `exceptionCode()` returns the active code, and `exceptionInfo()`
returns the formatted traceback. `catch(str error)` remains a catch-all for
compatibility and binds the error message to `error`; a bare `catch` is also a
catch-all without a binding. Multiple code-specific handlers are allowed. A
catch-all may occur only once and must be the final handler.

A `try` must have at least one `catch` or a `finally`. A `finally` without any
catch handlers is valid:

```lynx
try {
    performWork();
} finally {
    releaseResources();
}
```

## Handler selection

Only catchable runtime errors are considered. Catch clauses are checked in
source order; the first matching code-specific clause or catch-all runs. If no
clause matches, the error continues propagating after `finally` runs.

Examples of diagnostic codes include `divisionByZero`,
`numericValueExpected`, `stringUnterminated`, `macroDuplicated`,
`nativeValueResultNull`, and `structHasNoField`. The complete message and code
reference is [errorlist.md](errorlist.md).

Errors raised inside an [imported module](modules.md) retain their code and
source location when caught by the importer. `exceptionInfo()` identifies the
module file where the error originated.

A `catch` does not intercept:

- `sys.exit` / `sys.exitCode`, which exit after running applicable `finally`
  blocks;
- an interrupt (Ctrl-C), which also runs applicable `finally` blocks;
- a host/interpreter failure that is not represented as a `SourceError`.

## `finally` and control flow

`finally` runs after normal completion, a matched catch, an uncaught error, or
control flow leaving the protected body through `return`, `break`, or
`continue`. If `finally` itself raises an error or transfers control, that
outcome takes precedence over the pending return, loop control, or error.

```lynx
fn value() -> int {
    try {
        return 42;
    } finally {
        println("runs before returning");
    }
}
```

## Legacy string catch binding

The existing `catch(str error)` syntax remains a catch-all. The bound variable
contains the message, not the traceback:

- if the name does not exist, Lynxer declares it as `str` in the current scope;
- if it already exists, it must be a mutable `str` or `any` variable and is
  reassigned;
- otherwise the catch clause raises a runtime error, which propagates after
  `finally` executes.

## `raise`

```lynx
raise(message)
```

Raises a catchable runtime error with code `userRaised` and the supplied string
message. The argument must be a string; otherwise Lynxer raises
`raise(message) expects one string`. Custom error codes and user-defined error
types are not currently supported.

## `exceptionCode()`

```lynx
exceptionCode() -> str
```

Returns the stable code for the active catch handler, or `""` outside a catch
handler. A code-specific handler can use it without declaring a local:

```lynx
try {
    int value = 1 /% 0;
} catch (divisionByZero) {
    println(exceptionCode()); // divisionByZero
}
```

## `exceptionInfo()`

```lynx
exceptionInfo() -> str
```

Returns the formatted traceback for the active catch handler, or `""` outside
one. The `File` line names the module path when the error originated in an
imported module, and `<program>` otherwise. Nested handlers restore the outer
handler's information when they finish.

## Uncaught errors

An error with no matching handler runs applicable `finally` blocks, then ends
the program. The interpreter prints the source location, error code, and
message to standard error and exits with status `1`. An interrupt exits `130`.
