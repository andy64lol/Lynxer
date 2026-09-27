# Async helpers

Lynxer is single-threaded. The `async` keyword and the `async*` builtins exist,
but they do **not** provide concurrency: every call runs to completion on the
interpreter's one thread, and the event helpers are a cooperative polling layer
rather than an event loop driving coroutines.

## Local async sub-functions

Inside a `global` function you may declare a local sub-function with `async` and
call it as `async.name(args)`:

```lynx
global setup(){}

global main(){
    async greet(str name) {
        await asyncSleep(0.01);
        return "hi " + name;
    }
    println(async.greet("Ada"));   // hi Ada
}
```

- The definition is local to the enclosing function and must appear before it is
  called.
- `async.name(args)` runs the body **immediately** and returns its value; it is
  not a coroutine.
- `async` at the top level (outside a function body) is a syntax error.

## `await`

`await expr` is an expression that evaluates to `expr`. It does not suspend, and
it is **not** restricted to an async body:

```lynx
global setup(){}

global main(){
    println(await 41 + 1);   // 42
}
```

## Built-in helpers

| Function | Notes |
| --- | --- |
| `asyncSleep(seconds)` | Sleeps the current thread for `seconds`. |
| `asyncGather(a, b, ...)` | Returns its arguments as a list, `[a, b, ...]`. |
| `asyncRun(fn)` | Runs a callable; `asyncRun()` expects a function. |
| `asyncPollCreate` / `asyncPollRegister` / `asyncPollModify` / `asyncPollRemove` / `asyncPollWait` / `asyncPollDispatch` / `asyncPollClose` | Poller over a filesystem/networking handle or a raw descriptor. |
| `asyncTimerCreate` / `asyncTimerCancel` | One-shot or repeating timers attached to a poller. |
| `asyncWakeupCreate` / `asyncWakeupSignal` / `asyncWakeupClose` | Thread-safe wakeup sources. |

`asyncPollWait` returns a JSON array of event records; each has a `kind`
(`io`, `timer`, `wakeup`) and a `token`, and `io` records carry `fd` and an
`events` array.

## Differences from the original

The original reference described `asyncio`-backed cooperative concurrency —
coroutines that yield, a concurrent `asyncGather`, and an `await` that suspends.
None of that is implemented here: the syntax and the builtin names are present,
`asyncGather` returns its arguments unchanged, and `await` is a pass-through. `make testLynxer` pins the real behaviour in
`lynxer/examples/builtin_async.lynx`.

## See also

- [limitations.md](limitations.md) — the divergence register.
- [builtins.md](builtins.md) — the `async*` names in the builtin index.
