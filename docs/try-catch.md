# `try` / `catch`

Lynxer handles runtime errors with a `try` / `catch` statement. There is no
`except`, `finally` or `throw` keyword, and no hierarchy of exception types:
every handler catches every catchable runtime error, and the caught value is
always a `str` message.

## Syntax

```lynx
try {
    int value = intOf("not a number");   // raises a runtime error
} catch (str error) {
    println("caught ", error);
}
```

The binding clause is optional — `catch` may take no variable:

```lynx
try {
    raise("something went wrong");
} catch {
    println("failed");
}
```

A `try` must be followed by exactly one `catch`; there is no `finally` and no
second `catch` clause.

## What a handler catches

A `catch` block runs only for a **catchable runtime error**: the failures the
interpreter and its builtins raise (a division by zero, an out-of-range index, a
bad argument shape, a missing file where the builtin raises rather than returns
a sentinel, …), and any error you raise yourself with [`raise`](#raise). Native
modules may also raise a catchable error; this is the *Catchable runtime error*
family in [stdlib-contracts.md](stdlib-contracts.md), used by `sqldb`.

An error raised inside an [imported module](modules.md) is caught by the
importer's handler. `exceptionInfo()` then names the module file, so the
diagnostic is not misattributed to the importing program.

A `catch` does **not** intercept:

- `sys.exit` / `sys.exitCode` — the process exit runs to completion;
- an interrupt (Ctrl-C);
- an internal interpreter failure that is not a Lynxer runtime error — the run
  aborts with `lynxer: interpreter failure in '<file>': …`;
- control flow. `return`, `break`, `continue` and `restart` inside a `try` body
  behave exactly as they do outside one and are never routed to `catch`.

## The caught value

The bound variable receives the error **message**, not the traceback. For the
example above it is `division by zero`. The variable is a `str`:

- if the name does not exist yet, it is declared as `str` in the current scope;
- if it already exists, it must be an existing `str` (or `any`) variable and not
  `const`, and is reassigned;
- otherwise the `catch` clause itself fails with a runtime error explaining the
  conflict, which propagates to an enclosing handler.

## `raise`

```lynx
raise(message)
```

Raises a catchable runtime error with the given string message. The argument
must be a string; anything else is itself reported as
`raise(message) expects one string`. Because only the message is carried,
re-raising a caught error is written `raise(error)`.

## `exceptionInfo()`

```lynx
exceptionInfo() -> str
```

Returns the formatted traceback for the **active** handler, or `""` outside a
`catch` block:

```text
Traceback (most recent call last):
  File "<program>", line 4, column 18
division by zero
```

The `File` line names the module path when the error was raised inside an
imported module, and `<program>` otherwise. `exceptionInfo()` is only meaningful
inside the matching `catch`: a handler that calls another handler restores the
outer value when the inner one finishes.

## Nesting

Handlers nest. The inner handler's `exceptionInfo()` is scoped to the inner
error, and the outer handler's information is restored afterwards — an outer
`catch` sees `""` again if nothing is propagating:

```lynx
try {
    try {
        raise("inner");
    } catch (str error) {
        println(error);                 // inner
    }
    println(exceptionInfo() is "");      // true
} catch (str error) {
    println("not reached");
}
```

To let an error continue past a handler, raise it again with
`raise(error)`; the enclosing `catch` then receives the new message.

## Uncaught errors

An error with no matching handler ends the run: the interpreter prints

```text
lynxer: <file>:<line>:<column>: <message>
```

to standard error and exits with status `1`. (An interrupt exits `130`.)

## Example

```lynx
global setup(){ }

global divide(int a, int b) -> int {
    if(b is 0){ raise("cannot divide by zero"); }
    return a /% b;
}

global main(){
    try {
        println(global.divide(10, 2));   // 5
        println(global.divide(10, 0));   // raises
    } catch (str error) {
        println("error: ", error);
        println(exceptionInfo());
    }
}
```

## Limitations

- **No `finally`.** Put cleanup in the handler and after the statement, or wrap
  it in a helper function.
- **One untyped handler.** There is no way to catch only some errors, to declare
  several `catch` clauses, or to define your own error types — a `catch` catches
  every catchable runtime error.
- **String messages only.** The caught variable is a `str`; there is no
  structured error object.
- **Catching everything hides bugs.** A handler around a large block also
  catches mistakes such as an unknown variable or a wrong argument type. Keep
  the `try` body around the specific operation you expect to fail.
