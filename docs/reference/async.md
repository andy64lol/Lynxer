# Async / Await

Lynxer supports cooperative concurrency through local `async` sub-functions, built on top of Python's `asyncio`.

## Declaring a local async function

Inside any `global` function body, use `async` to define a local async sub-function:

```c
global fetchData() {
    async fetch() {
        await asyncSleep(0.5);
        println("data fetched\n");
    }
    async.fetch();
}
```

- `async funcName(params) { body }` defines a local async function scoped to the enclosing `global`.
- The sub-function can reference the enclosing function's parameters and local variables.
- A definition must appear **before** any block that calls it.
- An async function may call a top-level file-wide `func` directly, but it
  cannot declare a `func` inside its body. File-wide `func` declarations must
  remain at the source top level.

## Calling from sync context

Use `async.funcName(args)` to call and run the async function to completion:

```c
global main() {
    async greet(str name) {
        await asyncSleep(0.1);
        println("Hello, " + name + "!\n");
    }
    async.greet("World"); // runs and blocks until done
}
```

`async.funcName()` blocks until the async body finishes and returns its value.

## Return values

```c
global main() {
    async slowAdd(int a, int b) {
        await asyncSleep(0.05);
        return a + b;
    }
    any result = async.slowAdd(3, 4); // 7
    println(strOf(result) + "\n");
}
```

## Calling from inside another async body

Inside an async body, prefix with `await async.funcName()` to suspend until the inner call finishes:

```c
global main() {
    async compute() {
        await asyncSleep(0.05);
        return 42;
    }
    async run() {
        int answer = await async.compute(); // compute must be defined before run
        println(strOf(answer) + "\n");      // 42
    }
    async.run();
}
```

> The called function (`compute`) must be defined **before** the async block that calls it (`run`).

## Capturing outer parameters

The async body closes over the enclosing function's parameters:

```c
global compute(int n) {
    async doubleIt() {
        await asyncSleep(0.01);
        return n * 2; // n comes from the outer global
    }
    any result = async.doubleIt();
    println(strOf(result) + "\n");
}

global main() {
    global.compute(21); // prints 42
}
```

## Built-in async helpers

### `asyncSleep(seconds)`

Suspends the async body for `seconds` (float allowed). Must be `await`ed.

```c
global main() {
    async countdown() {
        int i = 3;
        while (i > 0) {
            println(strOf(i) + "\n");
            await asyncSleep(1.0);
            i = i - 1;
        }
        println("Go!\n");
    }
    async.countdown();
}
```

### `asyncGather(coro1, coro2, ...)`

Runs multiple coroutines concurrently and returns a list of results. Pass coroutines produced by `async.funcName(args)` inside an async body (where `async.funcName()` yields a coroutine rather than running immediately):

```c
global main() {
    async slowSquare(int n) {
        await asyncSleep(0.1);
        return n * n;
    }
    async gatherAll() {
        // all three run concurrently — total ≈ 0.1 s, not 0.3 s
        any squares = await asyncGather(
            async.slowSquare(2),
            async.slowSquare(3),
            async.slowSquare(4)
        );
        return squares; // [4, 9, 16]
    }
    any result = async.gatherAll();
    println(strOf(listGet(result, 0)) + "\n"); // 4
}
```

> Inside an async body, `async.funcName(args)` yields a coroutine (for `await` or `asyncGather`).  
> At the top level of a `global`, `async.funcName(args)` runs synchronously and returns the value.

## Event-driven I/O

`asyncPollCreate()` creates an event poller. Register a Lynxer filesystem or
networking handle, or a raw file descriptor, with a read/write interest and a
token:

```c
global main() {
    int poll = asyncPollCreate();
    int file = filesystemOpen("input.txt", "r");

    async readFile() {
        asyncPollRegister(poll, file, "read", "input");
        any events = await asyncPollWait(poll, 1000, 64);
        println(listGet(events, 0)); // JSON event record
        asyncPollRemove(poll, file);
    }

    async.readFile();
    filesystemClose(file);
    asyncPollClose(poll);
}
```

The event functions are:

| Function | Description |
|---|---|
| `asyncPollRegister(poll, resource, events, token)` | Register `read`, `write`, or `readwrite` readiness. |
| `asyncPollModify(poll, resource, events, token)` | Change an existing registration. |
| `asyncPollRemove(poll, resource)` | Remove a registration. |
| `asyncPollWait(poll, timeout_ms?, max_events?)` | Await one bounded batch of JSON event records; `-1` waits forever. |
| `asyncPollDispatch(poll, callback, timeout_ms?, max_events?)` | Await one batch and call the callback once per event. |
| `asyncPollClose(poll)` | Close the poller after pending waits finish. |

Event records contain `kind`, `token`, and readiness details. I/O records use
`kind: "io"` and include `fd` and an `events` array. The other event kinds are
`"timer"` and `"wakeup"`. `max_events` provides an explicit batch limit for
backpressure.

### Timers and wakeups

Timers are attached to a poller and can be one-shot or repeating:

```c
int timer = asyncTimerCreate(poll, 500, "heartbeat", 500);
any events = await asyncPollWait(poll, 1000);
asyncTimerCancel(timer);
```

`asyncWakeupCreate(poll, token)` creates a thread-safe wakeup source.
`asyncWakeupSignal(wakeup)` interrupts a pending poll wait, and
`asyncWakeupClose(wakeup)` releases it. Wakeups are useful for cooperative
shutdown and cancellation of an event loop. Poll waits are cancellable by
closing the poller after the waiting task has returned; resource cancellation
is provided by `asyncPollRemove`, `asyncTimerCancel`, and
`asyncWakeupClose`.

## `await` expression

`await` is only valid inside an `async` body:

```c
async run() {
    int answer = await async.compute(); // suspends until compute finishes
}
```

> **Error**: using `await` outside an `async` body is a runtime error.

## Error handling

`try / catch` works normally inside async functions:

```c
global main() {
    async risky() {
        try {
            any x = 1 / 0;
        } catch(str err) {
            println("caught: " + err + "\n");
        }
    }
    async.risky();
}
```

## Rules

- `async` definitions are **local** to the enclosing `global` function — not visible outside it.
- A definition must appear before any async block that calls it with `await async.funcName()`.
- `async` at the top level (outside a function body) is a syntax error.
- `await` is only valid inside an `async` body.
- `rawPy` and `rawPyx` blocks inside async functions run synchronously; avoid blocking I/O inside them.
- `vargroup` field initializers do not support `await` expressions.
