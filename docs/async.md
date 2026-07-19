# Async / Await

Lynxer supports cooperative concurrency through `async` functions and `await` expressions, built on top of Python's `asyncio`.

## Declaring an async function

Add the `async` keyword before `void` or `def`:

```c
async void fetchData() {
    await asyncSleep(0.5);
    println("data fetched\n");
}

async def int slowAdd(int a, int b) {
    await asyncSleep(0.1);
    return a + b;
}
```

- An `async void` function can be used at the top level (like a regular `void` function).
- An `async def` function can be defined locally inside another function with `allow_local_funcs`.

## Calling an async function

Calling an `async` function **does not run it immediately**. It returns a *coroutine* that can be:

1. **Run to completion** with `asyncRun()`
2. **Awaited** with `await` inside another `async` function
3. **Run concurrently** with `asyncGather()`

## Built-in async helpers

### `asyncRun(coro)`

Runs a coroutine synchronously (blocks until complete). Use this in `main()` to drive async code.

```c
void main() {
    asyncRun(fetchData());
}
```

### `asyncGather(coro1, coro2, ...)`

Returns a new coroutine that runs all supplied coroutines **concurrently** and resolves to a list of their return values.

```c
async def int task(int n) {
    await asyncSleep(0.1);
    return n * 2;
}

void main() {
    any results = asyncRun(asyncGather(task(1), task(2), task(3)));
    // results is a list: [2, 4, 6]
    println(strOf(listGet(results, 0)) + "\n");  // 2
}
```

### `asyncSleep(seconds)`

Returns a coroutine that sleeps for `seconds` (float allowed). Must be `await`ed inside an async function.

```c
async void countdown() {
    int i = 3;
    while (i > 0) {
        println(strOf(i) + "\n");
        await asyncSleep(1.0);
        i = i - 1;
    }
    println("Go!\n");
}

void main() {
    asyncRun(countdown());
}
```

## `await` expression

`await` can only appear inside an `async` function body. It suspends execution until the coroutine completes and evaluates to its return value.

```c
async def int compute() {
    await asyncSleep(0.05);
    return 42;
}

async void run() {
    int answer = await compute();
    println(strOf(answer) + "\n");  // 42
}

void main() {
    asyncRun(run());
}
```

> **Error**: using `await` outside an `async` function is a runtime error:
> `'await' can only be used inside an 'async' function`

## Error handling inside async functions

`try / catch` works normally inside async functions:

```c
async void risky() {
    try {
        int x = 1 / 0;
    } catch(str err) {
        println("caught: " + err + "\n");
    }
}

void main() {
    asyncRun(risky());
}
```

## Chaining async functions

```c
async def str greet(str name) {
    await asyncSleep(0.01);
    return "Hello, " + name + "!";
}

async void main_async() {
    str msg = await greet("World");
    println(msg + "\n");
}

void main() {
    asyncRun(main_async());
}
```

## Concurrent tasks with `asyncGather`

```c
async def int slowSquare(int n) {
    await asyncSleep(0.1);
    return n * n;
}

void main() {
    // All three run concurrently — total time ≈ 0.1 s, not 0.3 s
    any squares = asyncRun(asyncGather(
        slowSquare(2),
        slowSquare(3),
        slowSquare(4)
    ));
    // squares == [4, 9, 16]
}
```

## Scoping rules

Async functions use the same scoping rules as regular functions: each call gets a fresh execution context. Variables declared inside an async function are local to that call.

## Limitations

- `await` is only valid inside `async def` or `async void` function bodies. Using it elsewhere raises a runtime error.
- `rawPy` and `rawPyx` blocks inside async functions run synchronously; avoid blocking I/O inside them.
- `vargroup` field initializers do not support `await` expressions.
- `asyncRun` cannot be used while another event loop is already running (e.g. inside a Jupyter notebook that already uses `asyncio`).
