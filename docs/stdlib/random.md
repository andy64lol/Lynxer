# random

Seeded pseudo-random number and character helpers.

**Backend:** native — `stdlib/random.so`, built from `stdlib/random.cpp`. The
generator is a linear congruential sequence, so a given seed always produces the
same sequence (there is no process-global `random` state).
**Import:** `import("random")` → `random.*`

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

This is a subset of the original 17 functions; `sampleInt`,
`sampleStr`, `shuffle`, `triangular`, `gauss`, `uuid4` and `randHex` are not
ported.

## Example

```lynx
fn setup(){ import("random"); }

fn main(){
    random.seed(7);
    println(random.randint(1, 6));   // 4
    println(random.choice("abc"));   // b
    println(random.uniform(0.0, 1.0));
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
