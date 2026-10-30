# numutils

Exact decimal and rational (fraction) arithmetic.

**Backend:** native — `stdlib/numutils.so`, built from the Rust crate
`rust/numutils` over [`rust_decimal`](https://docs.rs/rust_decimal) and
[`num-rational`](https://docs.rs/num-rational).
**Import:** `import("numutils")` → `global.numutils.*`

## Why

Lynxer's `float` is an IEEE-754 double, so `0.1 + 0.2` prints as
`0.30000000000000004`. This module keeps values as canonical strings and does
the arithmetic exactly, so results round-trip cleanly. Convert back to a number
with `decimalToFloat` / `fractionToFloat` (or `int()`) when a native number is
needed.

Pass numeric operands straight to the arithmetic helpers: `decimalAdd(0.1, 0.2)`
is `"0.3"`. The conversion happens *before* any float arithmetic only if you
avoid doing the float arithmetic yourself — `decimal(0.1 + 0.2)` still reports
`"0.30000000000000004"`, because the sum is already a rounded double by the time
the module sees it.

## Decimal

A decimal value is a canonical base-10 string such as `"0.3"`, `"-1.5"` or
`"42"`. `decimal(value)` accepts an `int`, `float` or `str` and normalizes it:
trailing fractional zeros are dropped, so `decimal("0.30")` is `"0.3"`. Floats
are converted through their shortest round-tripping text first, so
`decimal(0.1)` is `"0.1"` rather than its full binary expansion.

| Function | Returns | Notes |
| --- | --- | --- |
| `decimal(value)` | str | canonical decimal string |
| `decimalAdd(a, b)` | str | |
| `decimalSub(a, b)` | str | |
| `decimalMul(a, b)` | str | |
| `decimalDiv(a, b)` | str | empty string on division by zero |
| `decimalDivScale(a, b, places)` | str | division rounded to `places` places |
| `decimalNeg(value)` | str | |
| `decimalAbs(value)` | str | |
| `decimalRound(value, places)` | str | |
| `decimalPow(value, exponent)` | str | integer exponent; negative inverts |
| `decimalCmp(a, b)` | int | -1 / 0 / 1 |
| `decimalEq(a, b)` | bool | |
| `decimalToFloat(value)` | float | |
| `decimalToInt(value)` | int | truncates toward zero |

Operands may be `int`, `float`, `str` or decimal strings; they are normalized
through `decimal()` first.

## Fraction

A fraction is a canonical reduced string `"num/den"` such as `"1/3"` or `"-2/5"`;
a whole number is `"4"`. `fraction(num, den)` builds one from a numerator and
denominator; `fractionParse(str)` parses `"a/b"`, a decimal or an integer. A
decimal string is converted exactly (mantissa over `10^scale`), so
`fraction(0.1, 0.2)` is `"1/2"` and `fractionParse("1.5")` is `"3/2"`.
`fractionToFraction(value)` is the underlying normalizer for any supported
operand.

| Function | Returns | Notes |
| --- | --- | --- |
| `fraction(num, den)` | str | reduced; denominators may be decimals |
| `fractionParse(value)` | str | `"a/b"`, decimal or integer |
| `fractionAdd(a, b)` | str | |
| `fractionSub(a, b)` | str | |
| `fractionMul(a, b)` | str | |
| `fractionDiv(a, b)` | str | empty string on division by zero |
| `fractionNeg(value)` | str | |
| `fractionAbs(value)` | str | |
| `fractionCmp(a, b)` | int | -1 / 0 / 1 |
| `fractionEq(a, b)` | bool | |
| `fractionToFloat(value)` | float | |
| `fractionToDecimal(value, places)` | str | rounded to `places` places |
| `fractionNumerator(value)` | str | |
| `fractionDenominator(value)` | str | |

Operands may be `int`, `float`, `str`, decimal or fraction strings; they are
normalized through `fractionToFraction` first.

## Limits

- `rust_decimal` is a 96-bit fixed-point value: it covers magnitudes up to about
  `7.9e28` with 28–29 significant digits. A value outside that range yields `""`
  rather than a wrong answer. Division keeps the default 28-digit precision, so
  `decimalDiv(1, 3)` is `0.3333333333333333333333333333`; use
  `decimalDivScale` / `decimalRound` to cut it down.
- Fractions are arbitrary-precision rationals, so they never round — but a
  denominator with a prime factor other than 2 or 5 has no finite decimal form,
  which is why `fractionToDecimal` takes a `places` argument.
- Failures (malformed input, division by zero) are the scalar sentinel `""` /
  `0` rather than an exception; there is no error channel across the native ABI.

## Misc

- `cleanFloat(value)` — the shortest round-tripping decimal text for a float.

## Example

```lynx
global setup(){ import("numutils"); }

global main(){
    println(global.numutils.decimalAdd(0.1, 0.2));        // 0.3
    println(global.numutils.decimalMul("0.1", "0.2"));    // 0.02
    println(global.numutils.fraction(1, 3));              // 1/3
    println(global.numutils.fractionAdd("1/3", "1/6"));   // 1/2
    println(global.numutils.fractionToDecimal("1/3", 6)); // 0.333333
}
```

## See also

- [math.md](math.md) — floating-point and integer math.
- [stdlib-contracts](../stdlib-contracts.md) — the contract this module implements.
