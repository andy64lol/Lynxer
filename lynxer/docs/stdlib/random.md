# random

Seeded pseudo-random number and character helpers.

**Backend:** native — `stdlib/random.so`, built from `stdlib/random.cpp`. The
generator is a linear congruential sequence, so a given seed always produces the
same sequence (unlike the Python reference, which used the global `random`
module state).
**Import:** `import("random")` → `global.random.*`

| Function | Signature | Notes |
| --- | --- | --- |
| `seed` | `(int value)` | Reseeds the generator; `0` restores the default seed |
| `randint` | `(int low, int high) -> int` | Uniform integer in `[low, high]`; returns `low` when `high <= low` |
| `randrange` | `(int start, int stop) -> int` | Uniform integer in `[start, stop)` |
| `random` | `() -> float` | Float in `[0.0, 1.0)` |
| `uniform` | `(float low, float high) -> float` | Float in `[low, high]` |
| `coinflip` | `() -> bool` | Equal-probability boolean |
| `randBool` | `() -> bool` | Alias of `coinflip` |
| `choice` | `(str items) -> str` | Picks one **character** of the input, or `""` when empty |

This is a subset of the Python reference's 17 functions; `sampleInt`,
`sampleStr`, `shuffle`, `triangular`, `gauss`, `uuid4` and `randHex` are not
ported.

## Example

```lynx
global setup(){ import("random"); }

global main(){
    global.random.seed(7);
    println(global.random.randint(1, 6));   // 4
    println(global.random.choice("abc"));   // b
    println(global.random.uniform(0.0, 1.0));
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [parity.md](../parity.md) — the parity scope with Python Clynxer.
- [limitations.md](../limitations.md) — the full divergence register.
