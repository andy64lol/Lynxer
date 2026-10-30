//! Lynxer `numutils` stdlib backend: exact decimal and rational arithmetic.
//!
//! Floating point makes `0.1 + 0.2` print as `0.30000000000000004`.
//! This module keeps values as canonical strings and does the arithmetic with
//! `rust_decimal` (base-10) and `num_rational` (exact fractions), so results
//! are exact and round-trip cleanly. Convert back to a number with the `*ToFloat`
//! helpers when a native float is needed.

use lynxer_abi::{export_float, export_int, export_string, lynxer_module};
use num_bigint::BigInt;
use num_rational::BigRational;
use num_traits::{One, Signed, Zero};
use rust_decimal::Decimal;
use std::str::FromStr;

fn decimal_from_str(s: &str) -> Option<Decimal> {
    Decimal::from_str(s.trim()).ok()
}

/// Converts a `Decimal` to an exact rational by putting its mantissa over
/// `10^scale`, so no information is lost in the conversion.
fn decimal_to_rational(d: Decimal) -> BigRational {
    let mut denom = BigInt::from(1i64);
    for _ in 0..d.scale() {
        denom *= BigInt::from(10i64);
    }
    BigRational::new(BigInt::from(d.mantissa()), denom)
}

/// Parses one side of a fraction: a decimal or an integer.
fn rational_from_scalar(s: &str) -> Option<BigRational> {
    let s = s.trim();
    if s.is_empty() {
        return None;
    }
    if let Ok(d) = Decimal::from_str(s) {
        Some(decimal_to_rational(d))
    } else {
        // Anything outside `rust_decimal`'s range still has an exact integer
        // representation worth accepting.
        let n: BigInt = s.parse().ok()?;
        Some(BigRational::from_integer(n))
    }
}

/// Parses `"a/b"`, a decimal, or an integer into an exact rational. Both sides
/// of a fraction may themselves be decimals (`"0.1/0.2"`), which is what the
/// wrapper produces for float operands. Extra slashes divide left to right, so
/// `"1/2/4"` is `1/8`.
fn fraction_from_str(s: &str) -> Option<BigRational> {
    let s = s.trim();
    if s.is_empty() {
        return None;
    }
    let mut parts = s.split('/');
    let mut acc = rational_from_scalar(parts.next()?)?;
    for part in parts {
        let rhs = rational_from_scalar(part)?;
        if rhs.is_zero() {
            return None;
        }
        acc /= rhs;
    }
    Some(acc)
}

fn canonical_decimal(d: Decimal) -> String {
    // `normalize` drops trailing fractional zeros, so "0.30" and "0.3" share
    // one representation; `Display` keeps at least one digit when needed.
    d.normalize().to_string()
}

/// Exact integer power by squaring. `checked_mul` turns overflow into `None`
/// instead of the panic `Decimal`'s `*` would raise, and a zero base with a
/// negative exponent is rejected by `checked_div`.
fn decimal_pow(x: Decimal, exponent: i64) -> Option<Decimal> {
    let mut remaining = exponent.unsigned_abs();
    let mut base = x;
    let mut acc = Decimal::ONE;
    while remaining > 0 {
        if remaining & 1 == 1 {
            acc = acc.checked_mul(base)?;
        }
        remaining >>= 1;
        if remaining > 0 {
            base = base.checked_mul(base)?;
        }
    }
    if exponent < 0 {
        Decimal::ONE.checked_div(acc)
    } else {
        Some(acc)
    }
}

fn canonical_fraction(r: BigRational) -> String {
    let n = r.numer();
    let d = r.denom();
    if *d == BigInt::one() {
        n.to_string()
    } else {
        format!("{}/{}", n, d)
    }
}

// --- decimal constructors -------------------------------------------------

export_string!(numutils_decimal_from_int, args, {
    let v = args.int(0);
    Decimal::from(v).to_string()
});

export_string!(numutils_decimal_from_float, args, {
    let v = args.num(0);
    if !v.is_finite() {
        return String::new();
    }
    // Rust's float Display is the shortest round-tripping form, so 0.1 stays
    // "0.1" rather than its full binary expansion.
    match Decimal::from_str(&format!("{}", v)) {
        Ok(d) => canonical_decimal(d),
        Err(_) => String::new(),
    }
});

export_string!(numutils_decimal_from_str, args, {
    let s = args.string(0);
    match decimal_from_str(s) {
        Some(d) => canonical_decimal(d),
        None => String::new(),
    }
});

// --- decimal arithmetic ---------------------------------------------------

export_string!(numutils_decimal_add, args, {
    let a = args.string(0);
    let b = args.string(1);
    match (decimal_from_str(a), decimal_from_str(b)) {
        (Some(x), Some(y)) => x.checked_add(y).map(canonical_decimal).unwrap_or_default(),
        _ => String::new(),
    }
});

export_string!(numutils_decimal_sub, args, {
    let a = args.string(0);
    let b = args.string(1);
    match (decimal_from_str(a), decimal_from_str(b)) {
        (Some(x), Some(y)) => x.checked_sub(y).map(canonical_decimal).unwrap_or_default(),
        _ => String::new(),
    }
});

export_string!(numutils_decimal_mul, args, {
    let a = args.string(0);
    let b = args.string(1);
    match (decimal_from_str(a), decimal_from_str(b)) {
        (Some(x), Some(y)) => x.checked_mul(y).map(canonical_decimal).unwrap_or_default(),
        _ => String::new(),
    }
});

export_string!(numutils_decimal_div, args, {
    let a = args.string(0);
    let b = args.string(1);
    match (decimal_from_str(a), decimal_from_str(b)) {
        (Some(x), Some(y)) => x.checked_div(y).map(canonical_decimal).unwrap_or_default(),
        _ => String::new(),
    }
});

export_string!(numutils_decimal_div_scale, args, {
    let a = args.string(0);
    let b = args.string(1);
    let scale = args.int(0).max(0) as u32;
    match (decimal_from_str(a), decimal_from_str(b)) {
        (Some(x), Some(y)) => x
            .checked_div(y)
            .map(|q| canonical_decimal(q.round_dp(scale)))
            .unwrap_or_default(),
        _ => String::new(),
    }
});

export_string!(numutils_decimal_neg, args, {
    let a = args.string(0);
    match decimal_from_str(a) {
        Some(x) => Decimal::ZERO
            .checked_sub(x)
            .map(canonical_decimal)
            .unwrap_or_default(),
        None => String::new(),
    }
});

export_string!(numutils_decimal_abs, args, {
    let a = args.string(0);
    match decimal_from_str(a) {
        Some(x) => canonical_decimal(x.abs()),
        None => String::new(),
    }
});

export_string!(numutils_decimal_round, args, {
    let a = args.string(0);
    let places = args.int(0);
    match decimal_from_str(a) {
        Some(x) => canonical_decimal(x.round_dp(places.max(0) as u32)),
        None => String::new(),
    }
});

export_int!(numutils_decimal_cmp, args, {
    let a = args.string(0);
    let b = args.string(1);
    match (decimal_from_str(a), decimal_from_str(b)) {
        (Some(x), Some(y)) => {
            if x < y {
                -1
            } else if x > y {
                1
            } else {
                0
            }
        }
        _ => 0,
    }
});

export_int!(numutils_decimal_eq, args, {
    let a = args.string(0);
    let b = args.string(1);
    match (decimal_from_str(a), decimal_from_str(b)) {
        (Some(x), Some(y)) => {
            if x == y {
                1
            } else {
                0
            }
        }
        _ => 0,
    }
});

export_float!(numutils_decimal_to_float, args, {
    let a = args.string(0);
    decimal_from_str(a)
        .map(|d| d.to_string().parse::<f64>().unwrap_or(0.0))
        .unwrap_or(0.0)
});

export_int!(numutils_decimal_to_int, args, {
    let a = args.string(0);
    decimal_from_str(a)
        .map(|d| d.trunc().to_string().parse::<i64>().unwrap_or(0))
        .unwrap_or(0)
});

export_string!(numutils_decimal_pow, args, {
    let a = args.string(0);
    let exp = args.int(0);
    match decimal_from_str(a) {
        Some(x) => decimal_pow(x, exp)
            .map(canonical_decimal)
            .unwrap_or_default(),
        None => String::new(),
    }
});

// --- fraction constructors ------------------------------------------------

export_string!(numutils_fraction_from_decimal, args, {
    let s = args.string(0);
    match decimal_from_str(s) {
        Some(d) => canonical_fraction(decimal_to_rational(d)),
        None => String::new(),
    }
});

export_string!(numutils_fraction_from_ints, args, {
    let n = args.int(0);
    let d = args.int(1);
    if d == 0 {
        return String::new();
    }
    let r = BigRational::new(BigInt::from(n), BigInt::from(d));
    canonical_fraction(r)
});

export_string!(numutils_fraction_from_str, args, {
    let s = args.string(0);
    match fraction_from_str(s) {
        Some(r) => canonical_fraction(r),
        None => String::new(),
    }
});

// --- fraction arithmetic --------------------------------------------------

export_string!(numutils_fraction_add, args, {
    let a = args.string(0);
    let b = args.string(1);
    match (fraction_from_str(a), fraction_from_str(b)) {
        (Some(x), Some(y)) => canonical_fraction(x + y),
        _ => String::new(),
    }
});

export_string!(numutils_fraction_sub, args, {
    let a = args.string(0);
    let b = args.string(1);
    match (fraction_from_str(a), fraction_from_str(b)) {
        (Some(x), Some(y)) => canonical_fraction(x - y),
        _ => String::new(),
    }
});

export_string!(numutils_fraction_mul, args, {
    let a = args.string(0);
    let b = args.string(1);
    match (fraction_from_str(a), fraction_from_str(b)) {
        (Some(x), Some(y)) => canonical_fraction(x * y),
        _ => String::new(),
    }
});

export_string!(numutils_fraction_div, args, {
    let a = args.string(0);
    let b = args.string(1);
    match (fraction_from_str(a), fraction_from_str(b)) {
        (Some(x), Some(y)) => {
            if y.is_zero() {
                return String::new();
            }
            canonical_fraction(x / y)
        }
        _ => String::new(),
    }
});

export_string!(numutils_fraction_neg, args, {
    let a = args.string(0);
    match fraction_from_str(a) {
        Some(x) => canonical_fraction(-x),
        None => String::new(),
    }
});

export_string!(numutils_fraction_abs, args, {
    let a = args.string(0);
    match fraction_from_str(a) {
        Some(x) => canonical_fraction(x.abs()),
        None => String::new(),
    }
});

export_int!(numutils_fraction_cmp, args, {
    let a = args.string(0);
    let b = args.string(1);
    match (fraction_from_str(a), fraction_from_str(b)) {
        (Some(x), Some(y)) => {
            if x < y {
                -1
            } else if x > y {
                1
            } else {
                0
            }
        }
        _ => 0,
    }
});

export_int!(numutils_fraction_eq, args, {
    let a = args.string(0);
    let b = args.string(1);
    match (fraction_from_str(a), fraction_from_str(b)) {
        (Some(x), Some(y)) => {
            if x == y {
                1
            } else {
                0
            }
        }
        _ => 0,
    }
});

export_float!(numutils_fraction_to_float, args, {
    let a = args.string(0);
    match fraction_from_str(a) {
        Some(r) => {
            let n: f64 = r.numer().to_string().parse().unwrap_or(0.0);
            let d: f64 = r.denom().to_string().parse().unwrap_or(1.0);
            if d == 0.0 {
                0.0
            } else {
                n / d
            }
        }
        None => 0.0,
    }
});

export_string!(numutils_fraction_to_decimal, args, {
    let a = args.string(0);
    let places = args.int(0).max(0) as u32;
    match fraction_from_str(a) {
        Some(r) => {
            let n = r.numer().to_string();
            let d = r.denom().to_string();
            match (Decimal::from_str(&n), Decimal::from_str(&d)) {
                (Ok(xn), Ok(xd)) => xn
                    .checked_div(xd)
                    .map(|q| canonical_decimal(q.round_dp(places)))
                    .unwrap_or_default(),
                _ => String::new(),
            }
        }
        None => String::new(),
    }
});

export_string!(numutils_fraction_numerator, args, {
    let a = args.string(0);
    match fraction_from_str(a) {
        Some(r) => r.numer().to_string(),
        None => String::new(),
    }
});

export_string!(numutils_fraction_denominator, args, {
    let a = args.string(0);
    match fraction_from_str(a) {
        Some(r) => r.denom().to_string(),
        None => String::new(),
    }
});

// --- misc -----------------------------------------------------------------

export_string!(numutils_clean_float, args, {
    let v = args.num(0);
    if !v.is_finite() {
        return String::new();
    }
    format!("{}", v)
});

const OPS: &[(&str, &str, &str)] = &[
    (
        "decimalFromInt",
        "numutils_decimal_from_int",
        "cdecl:cstring(...)",
    ),
    (
        "decimalFromFloat",
        "numutils_decimal_from_float",
        "cdecl:cstring(...)",
    ),
    (
        "decimalFromStr",
        "numutils_decimal_from_str",
        "cdecl:cstring(...)",
    ),
    ("decimalAdd", "numutils_decimal_add", "cdecl:cstring(...)"),
    ("decimalSub", "numutils_decimal_sub", "cdecl:cstring(...)"),
    ("decimalMul", "numutils_decimal_mul", "cdecl:cstring(...)"),
    ("decimalDiv", "numutils_decimal_div", "cdecl:cstring(...)"),
    (
        "decimalDivScale",
        "numutils_decimal_div_scale",
        "cdecl:cstring(...)",
    ),
    ("decimalNeg", "numutils_decimal_neg", "cdecl:cstring(...)"),
    ("decimalAbs", "numutils_decimal_abs", "cdecl:cstring(...)"),
    (
        "decimalRound",
        "numutils_decimal_round",
        "cdecl:cstring(...)",
    ),
    ("decimalCmp", "numutils_decimal_cmp", "cdecl:int64(...)"),
    ("decimalEq", "numutils_decimal_eq", "cdecl:int64(...)"),
    (
        "decimalToFloat",
        "numutils_decimal_to_float",
        "cdecl:float64(...)",
    ),
    (
        "decimalToInt",
        "numutils_decimal_to_int",
        "cdecl:int64(...)",
    ),
    ("decimalPow", "numutils_decimal_pow", "cdecl:cstring(...)"),
    (
        "fractionFromInts",
        "numutils_fraction_from_ints",
        "cdecl:cstring(...)",
    ),
    (
        "fractionFromStr",
        "numutils_fraction_from_str",
        "cdecl:cstring(...)",
    ),
    (
        "fractionFromDecimal",
        "numutils_fraction_from_decimal",
        "cdecl:cstring(...)",
    ),
    ("fractionAdd", "numutils_fraction_add", "cdecl:cstring(...)"),
    ("fractionSub", "numutils_fraction_sub", "cdecl:cstring(...)"),
    ("fractionMul", "numutils_fraction_mul", "cdecl:cstring(...)"),
    ("fractionDiv", "numutils_fraction_div", "cdecl:cstring(...)"),
    ("fractionNeg", "numutils_fraction_neg", "cdecl:cstring(...)"),
    ("fractionAbs", "numutils_fraction_abs", "cdecl:cstring(...)"),
    ("fractionCmp", "numutils_fraction_cmp", "cdecl:int64(...)"),
    ("fractionEq", "numutils_fraction_eq", "cdecl:int64(...)"),
    (
        "fractionToFloat",
        "numutils_fraction_to_float",
        "cdecl:float64(...)",
    ),
    (
        "fractionToDecimal",
        "numutils_fraction_to_decimal",
        "cdecl:cstring(...)",
    ),
    (
        "fractionNumerator",
        "numutils_fraction_numerator",
        "cdecl:cstring(...)",
    ),
    (
        "fractionDenominator",
        "numutils_fraction_denominator",
        "cdecl:cstring(...)",
    ),
    ("cleanFloat", "numutils_clean_float", "cdecl:cstring(...)"),
];

lynxer_module!(OPS);
