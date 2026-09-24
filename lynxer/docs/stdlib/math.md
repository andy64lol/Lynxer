# math

Integer, floating-point, number-theory, rounding, logarithmic and
trigonometric helpers.

**Backend:** native — `stdlib/math.so`, built from `stdlib/math.cpp` over
`<cmath>`. **Import:** `import("math")` → `global.math.*`

## Integers and number theory

| Function | Signature | Notes |
| --- | --- | --- |
| `abs` | `(int value) -> int` | Saturates at `maxInt` for the minimum integer |
| `max` / `min` | `(int, int) -> int` | Larger / smaller of two integers |
| `clamp` | `(int value, int low, int high) -> int` | Inclusive clamp |
| `pow` | `(int base, int exponent) -> int` | Integer exponentiation; `0` for negative exponents |
| `sign` | `(int value) -> int` | `-1`, `0` or `1` |
| `isEven` / `isOdd` | `(int value) -> bool` | Parity |
| `factorial` | `(int value) -> int` | `0` for negative input |
| `gcd` / `lcm` | `(int, int) -> int` | Greatest common divisor / least common multiple |
| `isPrime` | `(int value) -> bool` | Values below two are not prime |
| `nextPrime` | `(int value) -> int` | First prime strictly greater than `value` |
| `binomial` | `(int n, int k) -> int` | `n` choose `k`; `0` for invalid `k` |
| `sumRange` | `(int low, int high) -> int` | Inclusive sum; `0` when `low > high` |
| `isqrt` | `(int value) -> int` | Integer square root; `0` for negatives |

## Floating point

| Function | Signature | Notes |
| --- | --- | --- |
| `absFloat` | `(float value) -> float` | Absolute value |
| `maxFloat` / `minFloat` | `(float, float) -> float` | Larger / smaller (NaN-aware) |
| `clampFloat` | `(float value, float low, float high) -> float` | Inclusive clamp |
| `powFloat` | `(float base, float exponent) -> float` | `std::pow` |
| `sqrt`, `cbrt` | `(float value) -> float` | Square / cube root |
| `hypot` | `(float x, float y) -> float` | `sqrt(x² + y²)` |
| `signFloat` | `(float value) -> float` | `-1.0`, `0.0` or `1.0` |
| `fmod` / `remainder` | `(float, float) -> float` | Remainder operations |
| `truncate` | `(float value) -> int` | Truncates toward zero |
| `floor`, `ceil`, `round`, `trunc` | `(float value) -> float` | Rounding |
| `roundNum` | `(float value) -> float` | Alias of `round` |
| `roundTo` | `(float value, int decimals) -> float` | Rounds to `decimals` places |
| `lerp` | `(float low, float high, float t) -> float` | Linear interpolation |
| `mapRange` | `(float value, float inLo, float inHi, float outLo, float outHi) -> float` | Re-maps between ranges; returns `outLo` when the input range is empty |
| `degrees` / `radians` | `(float value) -> float` | Angle conversion |
| `isFinite`, `isNaN`, `isInfinite` | `(float value) -> bool` | Floating-point classification |

## Exponential, logarithmic and trigonometric

`exp`, `exp2`, `log`, `log10`, `log2`, `sin`, `cos`, `tan`, `asin`, `acos`,
`atan`, `atan2(y, x)`, `sinh`, `cosh`, `tanh` — all `(float) -> float` (plus
`atan2(float y, float x)`), and the aliases `arcsin`, `arccos`, `arctan`,
`arctan2`.

## Constants and randomness

| Function | Signature |
| --- | --- |
| `pi()` / `PI()` / `Pi()` | `-> float` |
| `e()` | `-> float` |
| `tau()` | `-> float` |
| `randInt(int low, int high) -> int` | Uniform integer in `[low, high]` |
| `randFloat(float low, float high) -> float` | Uniform float in `[low, high]` |

`maxInt` and `minInt` are exposed as functions returning the largest and
smallest 64-bit integers: `global.math.maxInt()` / `global.math.minInt()`.

## Statistics and vector helpers

These were previously a separate `mathPlus` module; they now live in `math`.
The Python reference implemented them with NumPy, which Clynxer does not need —
the results match NumPy's defaults. List arguments cross the native ABI as
tab-separated numbers, which is unambiguous because every element is a formatted
number; list results are returned as Lynxer lists.

| Function | Signature | Notes |
| --- | --- | --- |
| `mean` | `(list values) -> float` | Arithmetic mean |
| `median` | `(list values) -> float` | Averages the two middle values for even lengths |
| `std` | `(list values) -> float` | Population standard deviation (`ddof = 0`) |
| `variance` | `(list values) -> float` | Population variance |
| `percentile` | `(list values, float p) -> float` | Linear interpolation between order statistics |
| `prod` | `(list values) -> float` | Product of the elements |
| `norm` | `(list values) -> float` | Euclidean norm |
| `dot` | `(list a, list b) -> float` | Dot product; `0.0` when lengths differ |
| `corrcoef` | `(list a, list b) -> float` | Pearson correlation; `0.0` on a length mismatch or a constant series |
| `argmax` / `argmin` | `(list values) -> int` | Index of the largest / smallest element |
| `linspace` | `(float start, float stop, int n) -> list` | `n` evenly spaced values including both ends; empty for `n <= 0` |
| `cumsum` | `(list values) -> list` | Running sums |
| `diff` | `(list values) -> list` | Consecutive differences (one shorter) |
| `clip` | `(list values, float lo, float hi) -> list` | Element-wise clamp |
| `normalize` | `(list values) -> list` | Scales to unit norm; returns the input when the norm is `0` |

Empty input yields `0.0` for the scalar helpers and an empty list for the
list-returning helpers.

## Example

```lynx
global setup(){ import("math"); }

global main(){
    println(global.math.pi());
    println(global.math.mean(seqFromTo(1, 5, 1)));   // [1,2,3,4] -> 2.5
    println(global.math.linspace(0.0, 1.0, 5));
    println(global.math.cumsum(seqFromTo(1, 4, 1)));
}
```

`seqFromTo` and `range` exclude their `stop` value, so `seqFromTo(1, 5, 1)` is
`[1, 2, 3, 4]`.

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [parity.md](../parity.md) — the parity scope with Python Lynxer.
- [limitations.md](../limitations.md) — the full divergence register.
