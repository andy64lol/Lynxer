#include <cstdint>
#include <cmath>
#include <limits>
#include <random>

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

extern "C" std::int64_t math_abs(std::int64_t value) {
    if (value == std::numeric_limits<std::int64_t>::min()) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return value < 0 ? -value : value;
}

extern "C" double math_absFloat(double value) {
    return std::fabs(value);
}

extern "C" std::int64_t math_max(std::int64_t left, std::int64_t right) {
    return left > right ? left : right;
}

extern "C" std::int64_t math_min(std::int64_t left, std::int64_t right) {
    return left < right ? left : right;
}

extern "C" double math_maxFloat(double left, double right) {
    return std::fmax(left, right);
}

extern "C" double math_minFloat(double left, double right) {
    return std::fmin(left, right);
}

extern "C" std::int64_t math_clamp(
    std::int64_t value,
    std::int64_t low,
    std::int64_t high
) {
    return value < low ? low : (value > high ? high : value);
}

extern "C" double math_clampFloat(
    double value,
    double low,
    double high
) {
    return value < low ? low : (value > high ? high : value);
}

extern "C" std::int64_t math_pow(
    std::int64_t base,
    std::int64_t exponent
) {
    if (exponent < 0) {
        return 0;
    }

    std::int64_t result = 1;

    while (exponent > 0) {
        if (exponent & 1) {
            result *= base;
        }

        base *= base;
        exponent >>= 1;
    }

    return result;
}

extern "C" double math_powFloat(double base, double exponent) {
    return std::pow(base, exponent);
}

extern "C" double math_sqrt(double value) {
    return std::sqrt(value);
}

extern "C" double math_cbrt(double value) {
    return std::cbrt(value);
}

extern "C" double math_hypot(double x, double y) {
    return std::hypot(x, y);
}

extern "C" std::int64_t math_sign(std::int64_t value) {
    return value < 0 ? -1 : (value > 0 ? 1 : 0);
}

extern "C" double math_signFloat(double value) {
    return value < 0.0 ? -1.0 : (value > 0.0 ? 1.0 : 0.0);
}

extern "C" std::int64_t math_factorial(std::int64_t value) {
    if (value < 0) {
        return 0;
    }

    std::int64_t result = 1;

    for (std::int64_t index = 2; index <= value; ++index) {
        result *= index;
    }

    return result;
}

extern "C" std::int64_t math_gcd(
    std::int64_t left,
    std::int64_t right
) {
    if (left < 0) {
        left = -left;
    }

    if (right < 0) {
        right = -right;
    }

    while (right != 0) {
        const std::int64_t remainder = left % right;
        left = right;
        right = remainder;
    }

    return left;
}

extern "C" std::int64_t math_lcm(
    std::int64_t left,
    std::int64_t right
) {
    if (left == 0 || right == 0) {
        return 0;
    }

    return math_abs((left / math_gcd(left, right)) * right);
}

extern "C" std::int64_t math_isPrime(std::int64_t value) {
    if (value < 2) {
        return 0;
    }

    if (value == 2) {
        return 1;
    }

    if (value % 2 == 0) {
        return 0;
    }

    for (std::int64_t divisor = 3;
         divisor <= value / divisor;
         divisor += 2) {
        if (value % divisor == 0) {
            return 0;
        }
    }

    return 1;
}

extern "C" std::int64_t math_nextPrime(std::int64_t value) {
    if (value < 2) {
        return 2;
    }

    std::int64_t candidate = value + 1;

    if (candidate <= 2) {
        return 2;
    }

    if (candidate % 2 == 0) {
        ++candidate;
    }

    while (math_isPrime(candidate) == 0) {
        candidate += 2;
    }

    return candidate;
}

extern "C" std::int64_t math_binomial(
    std::int64_t n,
    std::int64_t k
) {
    if (n < 0 || k < 0 || k > n) {
        return 0;
    }

    if (k > n - k) {
        k = n - k;
    }

    std::int64_t result = 1;

    for (std::int64_t index = 1; index <= k; ++index) {
        result = result * (n - k + index) / index;
    }

    return result;
}

extern "C" std::int64_t math_sumRange(
    std::int64_t low,
    std::int64_t high
) {
    if (low > high) {
        return 0;
    }

    std::int64_t result = 0;

    for (std::int64_t value = low; value <= high; ++value) {
        result += value;
    }

    return result;
}

/* ---------- Rounding ---------- */

extern "C" double math_floor(double value) {
    return std::floor(value);
}

extern "C" double math_ceil(double value) {
    return std::ceil(value);
}

extern "C" double math_round(double value) {
    return std::round(value);
}

extern "C" double math_trunc(double value) {
    return std::trunc(value);
}

/* ---------- Exponential / logarithmic ---------- */

extern "C" double math_exp(double value) {
    return std::exp(value);
}

extern "C" double math_exp2(double value) {
    return std::exp2(value);
}

extern "C" double math_log(double value) {
    return std::log(value);
}

extern "C" double math_log10(double value) {
    return std::log10(value);
}

extern "C" double math_log2(double value) {
    return std::log2(value);
}

/* ---------- Trigonometry ---------- */

extern "C" double math_sin(double value) {
    return std::sin(value);
}

extern "C" double math_cos(double value) {
    return std::cos(value);
}

extern "C" double math_tan(double value) {
    return std::tan(value);
}

extern "C" double math_asin(double value) {
    return std::asin(value);
}

extern "C" double math_acos(double value) {
    return std::acos(value);
}

extern "C" double math_atan(double value) {
    return std::atan(value);
}

extern "C" double math_atan2(double y, double x) {
    return std::atan2(y, x);
}

/* ---------- Hyperbolic ---------- */

extern "C" double math_sinh(double value) {
    return std::sinh(value);
}

extern "C" double math_cosh(double value) {
    return std::cosh(value);
}

extern "C" double math_tanh(double value) {
    return std::tanh(value);
}

/* ---------- Floating-point utilities ---------- */

extern "C" std::int64_t math_isFinite(double value) {
    return std::isfinite(value) ? 1 : 0;
}

extern "C" std::int64_t math_isNaN(double value) {
    return std::isnan(value) ? 1 : 0;
}

extern "C" std::int64_t math_isInfinite(double value) {
    return std::isinf(value) ? 1 : 0;
}

extern "C" double math_fmod(double left, double right) {
    return std::fmod(left, right);
}

extern "C" double math_remainder(double left, double right) {
    return std::remainder(left, right);
}

extern "C" double math_pi() { return std::acos(-1.0); }
extern "C" double math_e() { return std::exp(1.0); }
extern "C" double math_degrees(double value) {
    return value * 180.0 / math_pi();
}
extern "C" double math_radians(double value) {
    return value * math_pi() / 180.0;
}
extern "C" double math_tau() { return 2.0 * math_pi(); }
extern "C" std::int64_t math_isqrt(std::int64_t value) {
    return value < 0 ? 0 : static_cast<std::int64_t>(std::sqrt(value));
}
extern "C" double math_lerp(double low, double high, double t) {
    return low + (high - low) * t;
}
extern "C" double math_roundTo(double value, std::int64_t decimals) {
    const double scale = std::pow(10.0, static_cast<double>(decimals));
    return std::round(value * scale) / scale;
}
extern "C" std::int64_t math_truncate(double value) {
    return static_cast<std::int64_t>(std::trunc(value));
}
extern "C" double math_randFloat(double low, double high) {
    static thread_local std::mt19937_64 generator(std::random_device{}());
    return std::uniform_real_distribution<double>(low, high)(generator);
}
extern "C" std::int64_t math_randInt(std::int64_t low, std::int64_t high) {
    static thread_local std::mt19937_64 generator(std::random_device{}());
    return std::uniform_int_distribution<std::int64_t>(low, high)(generator);
}

/* ---------- Registration ---------- */

extern "C" int lynxer_module_init_v1(
    RegisterFunction function,
    RegisterConstant constant,
    RegisterType
) {
    return
        function("abs", "math_abs",
                 "cdecl:int64(int64)") &&

        function("absFloat", "math_absFloat",
                 "cdecl:double(double)") &&

        function("max", "math_max",
                 "cdecl:int64(int64,int64)") &&

        function("min", "math_min",
                 "cdecl:int64(int64,int64)") &&

        function("maxFloat", "math_maxFloat",
                 "cdecl:double(double,double)") &&

        function("minFloat", "math_minFloat",
                 "cdecl:double(double,double)") &&

        function("clamp", "math_clamp",
                 "cdecl:int64(int64,int64,int64)") &&

        function("clampFloat", "math_clampFloat",
                 "cdecl:double(double,double,double)") &&

        function("pow", "math_pow",
                 "cdecl:int64(int64,int64)") &&

        function("powFloat", "math_powFloat",
                 "cdecl:double(double,double)") &&

        function("sqrt", "math_sqrt",
                 "cdecl:double(double)") &&

        function("cbrt", "math_cbrt",
                 "cdecl:double(double)") &&

        function("hypot", "math_hypot",
                 "cdecl:double(double,double)") &&

        function("sign", "math_sign",
                 "cdecl:int64(int64)") &&

        function("signFloat", "math_signFloat",
                 "cdecl:double(double)") &&

        function("factorial", "math_factorial",
                 "cdecl:int64(int64)") &&

        function("gcd", "math_gcd",
                 "cdecl:int64(int64,int64)") &&

        function("lcm", "math_lcm",
                 "cdecl:int64(int64,int64)") &&

        function("isPrime", "math_isPrime",
                 "cdecl:int64(int64)") &&

        function("nextPrime", "math_nextPrime",
                 "cdecl:int64(int64)") &&

        function("binomial", "math_binomial",
                 "cdecl:int64(int64,int64)") &&

        function("sumRange", "math_sumRange",
                 "cdecl:int64(int64,int64)") &&

        function("floor", "math_floor",
                 "cdecl:double(double)") &&

        function("ceil", "math_ceil",
                 "cdecl:double(double)") &&

        function("round", "math_round",
                 "cdecl:double(double)") &&

        function("trunc", "math_trunc",
                 "cdecl:double(double)") &&

        function("exp", "math_exp",
                 "cdecl:double(double)") &&

        function("exp2", "math_exp2",
                 "cdecl:double(double)") &&

        function("log", "math_log",
                 "cdecl:double(double)") &&

        function("log10", "math_log10",
                 "cdecl:double(double)") &&

        function("log2", "math_log2",
                 "cdecl:double(double)") &&

        function("sin", "math_sin",
                 "cdecl:double(double)") &&

        function("cos", "math_cos",
                 "cdecl:double(double)") &&

        function("tan", "math_tan",
                 "cdecl:double(double)") &&

        function("asin", "math_asin",
                 "cdecl:double(double)") &&

        function("acos", "math_acos",
                 "cdecl:double(double)") &&

        function("atan", "math_atan",
                 "cdecl:double(double)") &&

        function("atan2", "math_atan2",
                 "cdecl:double(double,double)") &&

        function("sinh", "math_sinh",
                 "cdecl:double(double)") &&

        function("cosh", "math_cosh",
                 "cdecl:double(double)") &&

        function("tanh", "math_tanh",
                 "cdecl:double(double)") &&

        function("isFinite", "math_isFinite",
                 "cdecl:int64(double)") &&

        function("isNaN", "math_isNaN",
                 "cdecl:int64(double)") &&

        function("isInfinite", "math_isInfinite",
                 "cdecl:int64(double)") &&

        function("fmod", "math_fmod",
                 "cdecl:double(double,double)") &&

        function("remainder", "math_remainder",
                 "cdecl:double(double,double)") &&
        function("pi", "math_pi", "cdecl:double()") &&
        function("e", "math_e", "cdecl:double()") &&
        function("degrees", "math_degrees", "cdecl:double(double)") &&
        function("radians", "math_radians", "cdecl:double(double)") &&
        function("truncate", "math_truncate", "cdecl:int64(double)") &&
        function("randInt", "math_randInt", "cdecl:int64(int64,int64)") &&
        function("randFloat", "math_randFloat",
                 "cdecl:double(double,double)") &&
        function("tau", "math_tau", "cdecl:double()") &&
        function("isqrt", "math_isqrt", "cdecl:int64(int64)") &&
        function("lerp", "math_lerp", "cdecl:double(double,double,double)") &&
        function("roundTo", "math_roundTo",
                 "cdecl:double(double,int64)") &&

        constant("maxInt", std::numeric_limits<std::int64_t>::max()) &&
        constant("minInt", std::numeric_limits<std::int64_t>::min())
        ? 0
        : 1;
}
