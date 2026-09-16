# mathPlus

Extended math plus statistics and vector helpers that the Python reference
implemented with NumPy.

**Backend:** native — `stdlib/mathPlus.so` (`stdlib/mathPlus.cpp`) for the
statistics/vector functions, plus `stdlib/math.so` for the re-exported scalar
math. **Import:** `import("mathPlus")` → `global.mathPlus.*`

## Re-exported from `math`

`abs`, `max`, `min`, `clamp`, `pow`, `floor`, `ceil`, `sqrt`, `round`,
`roundNum`, `pi`, `PI`, `Pi`, `e`, `tau`, `log`, `log2`, `log10`, `exp`, `sin`,
`cos`, `tan`, `arcsin`, `arccos`, `arctan`, `arctan2`, `sinh`, `cosh`, `tanh`,
`degrees`, `radians`, `sign`, `isEven`, `isOdd`, `factorial`, `gcd`, `lcm`,
`isPrime`, `nextPrime`, `binomial`, `isqrt`, `sumRange`, `hypot`, `truncate`,
`roundTo`, `lerp`, `clampFloat`, `mapRange`, `randInt`, `randFloat`, `mean`,
`prod`, `norm`, `argmax`, `argmin`. See [math](math.md) for signatures.

`mathPlus.sign` accepts a float and returns `-1`, `0` or `1`.

## Native statistics and vector helpers

All list arguments are Lynxer `list` values; list results are returned as lists.
Internally the wrapper joins elements with a tab, which is unambiguous because
every element is a formatted number.

| Function | Signature | Notes |
| --- | --- | --- |
| `median` | `(list values) -> float` | Averages the two middle values for even lengths |
| `std` | `(list values) -> float` | Population standard deviation (NumPy's default, `ddof = 0`) |
| `variance` | `(list values) -> float` | Population variance |
| `percentile` | `(list values, float p) -> float` | NumPy's default linear interpolation |
| `corrcoef` | `(list a, list b) -> float` | Pearson correlation; `0.0` when lengths differ or a series is constant |
| `dot` | `(list a, list b) -> float` | Dot product; `0.0` when lengths differ |
| `linspace` | `(float start, float stop, int n) -> list` | `n` evenly spaced values including both ends; empty for `n <= 0` |
| `cumsum` | `(list values) -> list` | Running sums |
| `diff` | `(list values) -> list` | Consecutive differences (one shorter) |
| `clip` | `(list values, float lo, float hi) -> list` | Element-wise clamp |
| `normalize` | `(list values) -> list` | Scales to unit norm; returns the input when the norm is `0` |

Empty input yields `0.0` for the scalar helpers and an empty list for the
list-returning helpers.

## Example

```lynx
global setup(){ import("mathPlus"); }

global main(){
    println(global.mathPlus.mean(seqFromTo(1, 5, 1)));
    println(global.mathPlus.std(seqFromTo(1, 5, 1)));
    println(global.mathPlus.linspace(0.0, 1.0, 5));
    println(global.mathPlus.cumsum(seqFromTo(1, 4, 1)));
}
```

`seqFromTo` excludes its `stop` value, so `seqFromTo(1, 5, 1)` is `[1, 2, 3, 4]`.
