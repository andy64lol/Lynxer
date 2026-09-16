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

`maxInt` and `minInt` are registered as integer constants and read as
`global.math.maxInt` / `global.math.minInt`.

## List helpers

`mean(list) -> float`, `prod(list) -> float`, `norm(list) -> float`,
`argmax(list) -> int`, `argmin(list) -> int`.
