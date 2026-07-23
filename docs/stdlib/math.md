# math

Common math utilities, constants and numeric helpers.

Functions (selected):

- Basic: `abs(n)`, `max(a,b)`, `min(a,b)`, `clamp(val, lo, hi)`.
- Powers and roots: `pow(base, exp)`, `sqrt(n)`, `floor(n)`, `ceil(n)`, `round(n)`.
- Constants: `pi()`, `PI()`, `Pi()`, `e()`.
- Logarithms: `log(n)`, `log2(n)`, `log10(n)`.
- Trig: `sin(n)`, `cos(n)`, `tan(n)`, `degrees(n)`, `radians(n)`.
- Number properties: `sign(n)`, `isEven(n)`, `isOdd(n)`, `factorial(n)`, `gcd(a,b)`, `lcm(a,b)`, `isPrime(n)`, `nextPrime(n)`.
- Combinatorics: `binomial(n,k)`.
- Integer utilities: `isqrt(n)`, `sumRange(lo,hi)`.
- Interpolation and mapping: `lerp(lo,hi,t)`, `mapRange(value,inLo,inHi,outLo,outHi)`, `clampFloat(val,lo,hi)`.
- Random helpers: `randInt(lo,hi)`, `randFloat(lo,hi)` (note: also available in `random` module).

Notes:
- Functions that rely on Python `math` return safe defaults on error.