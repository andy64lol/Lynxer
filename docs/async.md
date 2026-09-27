# Async and threads

Lynxer runs Lynxer code under a single global interpreter lock (the GIL): exactly
one thread evaluates code at a time. Within that model, `async*` now provides
**real concurrency** — `asyncRun` starts a function on a worker thread, `await`
joins it, and every blocking or joining operation releases the GIL so the other
work makes progress.

## Tasks

`asyncRun(function, arguments?)` starts `function(arguments...)` on a worker
thread and returns an integer **task handle** immediately:

```lynx
global setup(){}

global fetch(int n){
    asyncSleep(0.2);       // releases the GIL while it sleeps
    return n + 1;
}

global main(){
    int a = asyncRun(global.fetch, [1]);   // returns at once
    int b = asyncRun(global.fetch, [2]);
    println(await a);                      // 2, after joining a
    println(await b);                      // 3
}
```

`await expr` evaluates `expr`: if the result is a task handle it joins that task
and yields its value (re-raising the task's error if it failed); any other value
passes through unchanged, so `await asyncPollWait(...)` still works and
`await 41 + 1` is `42`.

A task cannot await itself (that is reported, not deadlocked). Two tasks that
await each other still deadlock, the same as two threads that join each other.

## `asyncGather`

`asyncGather(a, b, ...)` joins every argument that is a task handle and returns
the results as a list. Because arguments are evaluated before the call and
`asyncRun` returns after spawning, the tasks start together:

```lynx
any results = asyncGather(asyncRun(global.fetch, [1]), asyncRun(global.fetch, [2]));
// two 200 ms sleeps finish in a little over 200 ms, not 400 ms
```

## Local async sub-functions

Inside a `global` function you may declare a local sub-function with `async` and
call it as `async.name(args)`:

```lynx
async greet(str name) {
    await asyncSleep(0.01);
    return "hi " + name;
}
println(async.greet("Ada"));   // hi Ada
```

- The definition is local to the enclosing function and must appear before it is
  called.
- `async.name(args)` runs the body **immediately**, like a normal call; use
  `asyncRun` when you want a task. This keeps the polling helpers usable inside
  an `async` block.

## Built-in helpers

| Function | Notes |
| --- | --- |
| `asyncRun(function, arguments?)` | Starts a task, returns its handle. |
| `asyncSleep(seconds)` | Sleeps, releasing the GIL so other work runs. |
| `asyncGather(a, b, ...)` | Joins task handles; returns the results as a list. |
| `asyncPollCreate` / `asyncPollRegister` / `asyncPollModify` / `asyncPollRemove` / `asyncPollWait` / `asyncPollDispatch` / `asyncPollClose` | Poller over a filesystem/networking handle or a raw descriptor. `asyncPollWait` releases the GIL while it waits. |
| `asyncTimerCreate` / `asyncTimerCancel` | One-shot or repeating timers attached to a poller. |
| `asyncWakeupCreate` / `asyncWakeupSignal` / `asyncWakeupClose` | Thread-safe wakeup sources. |

`asyncPollWait` returns a JSON array of event records; each has a `kind`
(`io`, `timer`, `wakeup`) and a `token`, and `io` records carry `fd` and an
`events` array.

## Threads and interleaving

`nativeThreadStart` runs a function on a worker thread (see
[builtins.md](builtins.md)). Because of the GIL, a worker evaluates only while
the thread that holds the GIL is not evaluating — which is exactly what happens
during joins, blocking sync waits, `asyncSleep`, `asyncPollWait`, and the
explicit `nativeThreadYield(seconds?)`. So a program that wants a worker to make
progress while the main body is doing other work can yield:

```lynx
int handle = nativeThreadStart(global.worker, []);
while (!done) { nativeThreadYield(0.01); }
nativeThreadJoin(handle);
```

The GIL is deliberate: global state is not synchronized, so letting two threads
evaluate at once would be a data race. The trade-off is that CPU-bound tasks do
not run in parallel; use `nativeThreadYield` to interleave them.

## Differences from the original

The original reference described `asyncio`-backed coroutines. Lynxer has no
coroutines: `asyncRun` is a thread-backed task, `await` is a join, and the
syntax and builtin names are otherwise the same. `make testLynxer` pins the
behaviour in `lynxer/examples/builtin_async.lynx`.

## See also

- [limitations.md](limitations.md) — the divergence register.
- [builtins.md](builtins.md) — the `async*` and `nativeThread*` names.
