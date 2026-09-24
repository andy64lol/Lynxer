#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <random>
#include <string>
#include <vector>

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

static const char* stable(std::string value) {
    thread_local std::string result;
    result = std::move(value);
    return result.c_str();
}

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

/* ---------- Statistics and vectors ---------- */

// List arguments arrive as tab-separated numbers (the separator the `.lynx`
// wrapper uses), and list results are returned the same way. Tab is unambiguous
// because every element is a formatted number.

static const char kListSeparator = '\t';

static std::vector<double> parseValues(const std::string& text) {
    std::vector<double> values;
    if (text.empty()) {
        return values;
    }
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t separator = text.find(kListSeparator, start);
        const std::string token = text.substr(
            start, separator == std::string::npos ? std::string::npos
                                                  : separator - start);
        if (!token.empty()) {
            values.push_back(std::strtod(token.c_str(), nullptr));
        }
        if (separator == std::string::npos) {
            break;
        }
        start = separator + 1;
    }
    return values;
}

static std::string formatValues(const std::vector<double>& values) {
    std::string output;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            output += kListSeparator;
        }
        char buffer[64];
        const auto converted =
            std::to_chars(buffer, buffer + sizeof(buffer), values[index]);
        std::string text(buffer, converted.ptr);
        if (text.find_first_of(".eEni") == std::string::npos) {
            text += ".0";
        }
        output += text;
    }
    return output;
}

static double meanOfValues(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    double total = 0.0;
    for (const double value : values) {
        total += value;
    }
    return total / static_cast<double>(values.size());
}

extern "C" double math_median(const char* text) {
    std::vector<double> values = parseValues(text == nullptr ? "" : text);
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values[middle];
    }
    return (values[middle - 1] + values[middle]) / 2.0;
}

extern "C" double math_std(const char* text) {
    const std::vector<double> values = parseValues(text == nullptr ? "" : text);
    if (values.empty()) {
        return 0.0;
    }
    const double mean = meanOfValues(values);
    double total = 0.0;
    for (const double value : values) {
        total += (value - mean) * (value - mean);
    }
    return std::sqrt(total / static_cast<double>(values.size()));
}

extern "C" double math_variance(const char* text) {
    const std::vector<double> values = parseValues(text == nullptr ? "" : text);
    if (values.empty()) {
        return 0.0;
    }
    const double mean = meanOfValues(values);
    double total = 0.0;
    for (const double value : values) {
        total += (value - mean) * (value - mean);
    }
    return total / static_cast<double>(values.size());
}

// NumPy's default linear interpolation between order statistics.
extern "C" double math_percentile(const char* text, double percent) {
    std::vector<double> values = parseValues(text == nullptr ? "" : text);
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    if (values.size() == 1) {
        return values.front();
    }
    const double rank =
        (percent / 100.0) * static_cast<double>(values.size() - 1);
    const double lower = std::floor(rank);
    const double fraction = rank - lower;
    const std::size_t index = static_cast<std::size_t>(lower);
    if (index + 1 >= values.size()) {
        return values.back();
    }
    return values[index] + fraction * (values[index + 1] - values[index]);
}

extern "C" double math_corrcoef(const char* first, const char* second) {
    const std::vector<double> left = parseValues(first == nullptr ? "" : first);
    const std::vector<double> right =
        parseValues(second == nullptr ? "" : second);
    if (left.empty() || left.size() != right.size()) {
        return 0.0;
    }
    const double leftMean = meanOfValues(left);
    const double rightMean = meanOfValues(right);
    double covariance = 0.0;
    double leftVariance = 0.0;
    double rightVariance = 0.0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const double leftDelta = left[index] - leftMean;
        const double rightDelta = right[index] - rightMean;
        covariance += leftDelta * rightDelta;
        leftVariance += leftDelta * leftDelta;
        rightVariance += rightDelta * rightDelta;
    }
    const double denominator = std::sqrt(leftVariance * rightVariance);
    return denominator == 0.0 ? 0.0 : covariance / denominator;
}

extern "C" double math_dot(const char* first, const char* second) {
    const std::vector<double> left = parseValues(first == nullptr ? "" : first);
    const std::vector<double> right =
        parseValues(second == nullptr ? "" : second);
    if (left.size() != right.size()) {
        return 0.0;
    }
    double total = 0.0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        total += left[index] * right[index];
    }
    return total;
}

extern "C" const char* math_linspace(double start, double stop,
                                     std::int64_t count) {
    if (count <= 0) {
        return stable("");
    }
    std::vector<double> values;
    if (count == 1) {
        values.push_back(start);
    } else {
        const double step = (stop - start) / static_cast<double>(count - 1);
        for (std::int64_t index = 0; index < count; ++index) {
            values.push_back(start + step * static_cast<double>(index));
        }
    }
    return stable(formatValues(values));
}

extern "C" const char* math_cumsum(const char* text) {
    const std::vector<double> values = parseValues(text == nullptr ? "" : text);
    if (values.empty()) {
        return stable("");
    }
    std::vector<double> result;
    double running = 0.0;
    for (const double value : values) {
        running += value;
        result.push_back(running);
    }
    return stable(formatValues(result));
}

extern "C" const char* math_diff(const char* text) {
    const std::vector<double> values = parseValues(text == nullptr ? "" : text);
    if (values.size() < 2) {
        return stable("");
    }
    std::vector<double> result;
    for (std::size_t index = 1; index < values.size(); ++index) {
        result.push_back(values[index] - values[index - 1]);
    }
    return stable(formatValues(result));
}

extern "C" const char* math_clip(const char* text, double low, double high) {
    const std::vector<double> values = parseValues(text == nullptr ? "" : text);
    if (values.empty()) {
        return stable("");
    }
    std::vector<double> result;
    for (const double value : values) {
        result.push_back(value < low ? low : (value > high ? high : value));
    }
    return stable(formatValues(result));
}

extern "C" const char* math_normalize(const char* text) {
    const std::vector<double> values = parseValues(text == nullptr ? "" : text);
    if (values.empty()) {
        return stable("");
    }
    double total = 0.0;
    for (const double value : values) {
        total += value * value;
    }
    const double magnitude = std::sqrt(total);
    if (magnitude == 0.0) {
        return stable(formatValues(values));
    }
    std::vector<double> result;
    for (const double value : values) {
        result.push_back(value / magnitude);
    }
    return stable(formatValues(result));
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
        function("median", "math_median", "cdecl:double(cstring)") &&
        function("std", "math_std", "cdecl:double(cstring)") &&
        function("variance", "math_variance", "cdecl:double(cstring)") &&
        function("percentile", "math_percentile",
                 "cdecl:double(cstring,double)") &&
        function("corrcoef", "math_corrcoef",
                 "cdecl:double(cstring,cstring)") &&
        function("dot", "math_dot", "cdecl:double(cstring,cstring)") &&
        function("linspace", "math_linspace",
                 "cdecl:cstring(double,double,int64)") &&
        function("cumsum", "math_cumsum", "cdecl:cstring(cstring)") &&
        function("diff", "math_diff", "cdecl:cstring(cstring)") &&
        function("clip", "math_clip", "cdecl:cstring(cstring,double,double)") &&
        function("normalize", "math_normalize", "cdecl:cstring(cstring)") &&

        constant("maxInt", std::numeric_limits<std::int64_t>::max()) &&
        constant("minInt", std::numeric_limits<std::int64_t>::min())
        ? 0
        : 1;
}
