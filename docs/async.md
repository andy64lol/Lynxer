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
