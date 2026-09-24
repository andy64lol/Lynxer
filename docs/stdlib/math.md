# math

Mathematical operations wrapping Python's `math`, `random`, and **NumPy** modules.

```c
global setup(){
    import("math");
}

global main(){
    print(global.math.sqrt(144));    // 12.0
    print(global.math.mean([1, 2, 3, 4, 5]));  // 3.0
}
```

**Requires NumPy** for all functions in the NumPy sections:

```bash
pip install numpy
```

---

## Constants

| Function | Returns | Description |
|----------|---------|-------------|
| `pi()` | `float` | π ≈ 3.141592653589793 |
| `PI()` | `float` | Alias for `pi()` (legacy uppercase) |
| `Pi()` | `float` | Alias for `pi()` |
| `e()` | `float` | Euler's number ≈ 2.718281828459045 |
| `tau()` | `float` | τ = 2π ≈ 6.283185307179586 *(NumPy)* |

---

## Basic arithmetic

| Function | Signature | Description |
|----------|-----------|-------------|
| `abs` | `abs(float n)` | Absolute value |
| `max` | `max(int a, int b)` | Larger of two values |
| `min` | `min(int a, int b)` | Smaller of two values |
| `pow` | `pow(int base, int exp)` | Integer exponentiation (loop-based) |
| `sqrt` | `sqrt(float n)` | Square root |
| `floor` | `floor(float n)` | Round down to nearest integer |
| `ceil` | `ceil(float n)` | Round up to nearest integer |
| `round` | `round(float n)` | Round to nearest integer |
| `roundNum` | `roundNum(float n)` | Alias for `round` (legacy) |
| `roundTo` | `roundTo(float n, int decimals)` | Round to `decimals` decimal places *(NumPy)* |
| `truncate` | `truncate(float n)` | Remove fractional part (toward zero) |
| `sign` | `sign(float n)` | `-1` if n < 0, `0` if n == 0, `1` if n > 0 |

---

## Clamping and mapping

| Function | Signature | Description |
|----------|-----------|-------------|
| `clamp` | `clamp(int val, int lo, int hi)` | Clamp integer to `[lo, hi]` |
| `clampFloat` | `clampFloat(float val, float lo, float hi)` | Clamp float to `[lo, hi]` |
| `lerp` | `lerp(float lo, float hi, float t)` | Linear interpolation: `lo + t*(hi - lo)` |
| `mapRange` | `mapRange(float value, float inLo, float inHi, float outLo, float outHi)` | Map value from one range to another |

---

## Logarithms

| Function | Signature | Description |
|----------|-----------|-------------|
| `log` | `log(float n)` | Natural logarithm ln(n). Returns `0.0` for n ≤ 0. |
| `log2` | `log2(float n)` | Base-2 logarithm. Returns `0.0` for n ≤ 0. |
| `log10` | `log10(float n)` | Base-10 logarithm. Returns `0.0` for n ≤ 0. |

---

## Trigonometry

All angles in **radians**.

| Function | Signature | Description |
|----------|-----------|-------------|
| `sin` | `sin(float n)` | Sine |
| `cos` | `cos(float n)` | Cosine |
| `tan` | `tan(float n)` | Tangent |
| `arcsin` | `arcsin(float n)` | Inverse sine. n must be in [-1, 1]. Returns `0.0` on error. *(NumPy)* |
| `arccos` | `arccos(float n)` | Inverse cosine. n must be in [-1, 1]. Returns `0.0` on error. *(NumPy)* |
| `arctan` | `arctan(float n)` | Inverse tangent. *(NumPy)* |
| `arctan2` | `arctan2(float y, float x)` | Four-quadrant `atan(y/x)`. Range: (-π, π]. *(NumPy)* |
| `degrees` | `degrees(float n)` | Radians → degrees |
| `radians` | `radians(float n)` | Degrees → radians |
| `hypot` | `hypot(float a, float b)` | `sqrt(a² + b²)` |

---

## Hyperbolic functions *(NumPy)*

| Function | Signature | Description |
|----------|-----------|-------------|
| `sinh` | `sinh(float n)` | Hyperbolic sine |
| `cosh` | `cosh(float n)` | Hyperbolic cosine |
| `tanh` | `tanh(float n)` | Hyperbolic tangent |

---

## Exponential *(NumPy)*

| Function | Signature | Description |
|----------|-----------|-------------|
| `exp` | `exp(float n)` | `eⁿ` — Euler's number raised to the power n |

```c
global main(){
    float r = global.math.exp(1.0);   // 2.718…
    print(r); print("\n");
}
```

---

## Number theory

| Function | Signature | Description |
|----------|-----------|-------------|
| `isEven` | `isEven(int n)` | `true` if n is even |
| `isOdd` | `isOdd(int n)` | `true` if n is odd |
| `factorial` | `factorial(int n)` | n! Returns `1` for n ≤ 0 |
| `gcd` | `gcd(int a, int b)` | Greatest common divisor |
| `lcm` | `lcm(int a, int b)` | Least common multiple |
| `isPrime` | `isPrime(int n)` | `true` if n ≥ 2 is prime |
| `nextPrime` | `nextPrime(int n)` | Smallest prime strictly greater than n |
| `binomial` | `binomial(int n, int k)` | Binomial coefficient C(n, k) — "n choose k" |
| `isqrt` | `isqrt(int n)` | Integer square root (floor of sqrt) |

---

## Integer utilities

| Function | Signature | Description |
|----------|-----------|-------------|
| `sumRange` | `sumRange(int lo, int hi)` | Sum of all integers from `lo` to `hi` inclusive |

---

## Random numbers

| Function | Signature | Description |
|----------|-----------|-------------|
| `randInt` | `randInt(int lo, int hi)` | Random integer in `[lo, hi]` inclusive |
| `randFloat` | `randFloat(float lo, float hi)` | Random float in `[lo, hi)` |

---

## List statistics *(NumPy)*

These functions accept a Lynxer list of numbers and return a scalar.

| Function | Signature | Description |
|----------|-----------|-------------|
| `mean` | `mean(list lst)` | Arithmetic mean |
| `median` | `median(list lst)` | Median value |
| `std` | `std(list lst)` | Population standard deviation |
| `variance` | `variance(list lst)` | Population variance |
| `percentile` | `percentile(list lst, float p)` | p-th percentile (p in 0–100) |
| `corrcoef` | `corrcoef(any a, any b)` | Pearson correlation coefficient of two lists. Returns a value in [-1, 1]. |

```c
global main(){
    any data = seqFromTo(1, 6, 1);   // [1, 2, 3, 4, 5]
    print(global.math.mean(data));     print("\n");  // 3.0
    print(global.math.median(data));   print("\n");  // 3.0
    print(global.math.std(data));      print("\n");  // 1.4142…
    print(global.math.variance(data)); print("\n");  // 2.0
    print(global.math.percentile(data, 75.0)); print("\n");  // 4.0
}
```

---

## List aggregates *(NumPy — scalar result)*

| Function | Signature | Description |
|----------|-----------|-------------|
| `prod` | `prod(list lst)` | Product of all elements |
| `argmax` | `argmax(list lst)` | Zero-based index of the maximum value |
| `argmin` | `argmin(list lst)` | Zero-based index of the minimum value |

---

## Linear algebra *(NumPy — scalar result)*

| Function | Signature | Description |
|----------|-----------|-------------|
| `dot` | `dot(any a, any b)` | Dot product of two equal-length lists |
| `norm` | `norm(list lst)` | L2 (Euclidean) norm — magnitude of a vector |

```c
global main(){
    any v1 = seqFromTo(0, 0, 1);
    v1 = listPush(v1, 3); v1 = listPush(v1, 4);
    float mag = global.math.norm(v1);   // 5.0
    print(mag); print("\n");

    any v2 = seqFromTo(0, 0, 1);
    v2 = listPush(v2, 1); v2 = listPush(v2, 0);
    any v3 = seqFromTo(0, 0, 1);
    v3 = listPush(v3, 0); v3 = listPush(v3, 1);
    float d = global.math.dot(v2, v3);  // 0.0 (orthogonal)
    print(d); print("\n");
}
```

---

## List-returning functions *(NumPy)*

These return a **Lynxer list of strings**. Each element is the string representation of a float.  
Use `floatOf(listGet(lst, i))` to convert an element to a number for arithmetic.

| Function | Signature | Description |
|----------|-----------|-------------|
| `linspace` | `linspace(float start, float stop, int n)` | `n` evenly-spaced values between `start` and `stop` (inclusive) |
| `cumsum` | `cumsum(list lst)` | Running total at each position |
| `diff` | `diff(list lst)` | Differences between consecutive elements; result is one shorter than input |
| `clip` | `clip(list lst, float lo, float hi)` | Element-wise clamp to `[lo, hi]` |
| `normalize` | `normalize(list lst)` | Scale to unit L2 length (divides each element by the norm) |

```c
global main(){
    // linspace
    any pts = global.math.linspace(0.0, 1.0, 5);
    // pts == ["0.0", "0.25", "0.5", "0.75", "1.0"]
    float mid = floatOf(listGet(pts, 2));
    print(mid); print("\n");   // 0.5

    // cumsum
    any nums = seqFromTo(1, 5, 1);   // [1, 2, 3, 4, 5]
    any cs   = global.math.cumsum(nums);
    // cs == ["1.0", "3.0", "6.0", "10.0", "15.0"]
    print(listGet(cs, 4)); print("\n");   // 15.0

    // diff
    any ds = global.math.diff(nums);
    // ds == ["1.0", "1.0", "1.0", "1.0"]
    print(listGet(ds, 0)); print("\n");   // 1.0

    // clip
    any raw = seqFromTo(0, 0, 1);
    raw = listPush(raw, -5); raw = listPush(raw, 3); raw = listPush(raw, 12);
    any clipped = global.math.clip(raw, 0.0, 10.0);
    // clipped == ["0.0", "3.0", "10.0"]
    print(listGet(clipped, 0)); print("\n");   // 0.0
    print(listGet(clipped, 2)); print("\n");   // 10.0

    // normalize
    any vec = seqFromTo(0, 0, 1);
    vec = listPush(vec, 3); vec = listPush(vec, 4);
    any unit = global.math.normalize(vec);
    // unit ≈ ["0.6", "0.8"]  (magnitude == 1.0)
    print(global.math.norm([0.6, 0.8])); print("\n");  // ~1.0
}
```

---

## Error handling

- Functions that can receive invalid inputs (e.g. `arcsin` with |n| > 1, `log` with n ≤ 0) return `0.0` instead of raising an error.
- List-returning functions return a single-element list containing `"0"` on NumPy error.
- NumPy functions will raise a `Runtime Error` with a descriptive message if NumPy is not installed.
