# debug

Assertions, type inspection, structured logging, timers and process metrics.

**Backend:** native + pure — `stdlib/debug.so` (`stdlib/debug.cpp`) provides
timers, timestamps, environment and memory; the assertions and logging helpers
are written in Lynxer in `stdlib/debug.lynx`.
**Import:** `import("debug")` → `global.debug.*`

## Assertions

| Function | Signature | Notes |
| --- | --- | --- |
| `assert` | `(bool cond, str msg)` | Fails with `msg` when `cond` is false |
| `assertEq` | `(str a, str b, str msg)` | Fails unless the strings are equal |
| `assertNotEq` | `(str a, str b, str msg)` | Fails when the strings are equal |
| `assertGt` | `(float a, float b, str msg)` | Fails unless `a > b` |
| `assertLt` | `(float a, float b, str msg)` | Fails unless `a < b` |
| `assertContains` | `(str haystack, str needle, str msg)` | Fails unless `needle` is a substring |

## Inspection

| Function | Signature | Notes |
| --- | --- | --- |
| `typeOf` | `(any val) -> str` | `int`, `float`, `str`, `bool`, `list`, `tuple` or `null` |
| `dump` | `(any val)` | Prints `[debug.dump] type=T  value=V` |
| `inspect` | `(any val) -> str` | JSON `{"type": ..., "value": ...}` |
| `pp` | `(any val)` | Indented JSON for lists, plain text otherwise |

`typeOf` maps the interpreter's `none` to `null`. `dump`/`pp` use `strOf`
rendering, so strings are not quoted.

## Logging

`log(str level, str msg)`, `info(str msg)`, `warn(str msg)`, `error(str msg)`,
`debug(str msg)` — each prints `[LEVEL HH:MM:SS] message`. The timestamp makes
these unsuitable for exact-output tests.

## Timers

| Function | Signature | Notes |
| --- | --- | --- |
| `startTimer` | `(str label)` | Records a monotonic start time |
| `stopTimer` | `(str label) -> float` | Milliseconds since start, removes the timer; `-1.0` if unknown |
| `elapsed` | `(str label) -> float` | Milliseconds without stopping; `-1.0` if unknown |
| `clock` | `() -> float` | Monotonic milliseconds since an arbitrary origin |

## Environment and memory

| Function | Signature | Notes |
| --- | --- | --- |
| `envGet` | `(str key) -> str` | Value, or `""` |
| `envAll` | `() -> str` | All variables as a JSON object |
| `getMemory` | `() -> float` | Peak RSS in megabytes (`getrusage`), or `-1.0` |

## Example

```lynx
global setup(){ import("debug"); }

global main(){
    global.debug.assertEq(global.debug.typeOf(1), "int", "int type");
    global.debug.startTimer("work");
    println(global.debug.stopTimer("work") >= 0.0);
    println(global.debug.inspect("text"));
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
