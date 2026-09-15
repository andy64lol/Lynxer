#include <cstdint>

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

extern "C" std::int64_t math_abs(std::int64_t value) {
    return value < 0 ? -value : value;
}

extern "C" std::int64_t math_max(std::int64_t left, std::int64_t right) {
    return left > right ? left : right;
}

extern "C" std::int64_t math_min(std::int64_t left, std::int64_t right) {
    return left < right ? left : right;
}

extern "C" std::int64_t math_clamp(std::int64_t value, std::int64_t low,
                                    std::int64_t high) {
    return value < low ? low : (value > high ? high : value);
}

extern "C" std::int64_t math_pow(std::int64_t base, std::int64_t exponent) {
    std::int64_t result = 1;
    for (std::int64_t index = 0; index < exponent; ++index) {
        result *= base;
    }
    return result;
}

extern "C" std::int64_t math_sign(std::int64_t value) {
    return value < 0 ? -1 : (value > 0 ? 1 : 0);
}

extern "C" std::int64_t math_factorial(std::int64_t value) {
    std::int64_t result = 1;
    for (std::int64_t index = 2; index <= value; ++index) {
        result *= index;
    }
    return result;
}

extern "C" std::int64_t math_gcd(std::int64_t left, std::int64_t right) {
    while (right != 0) {
        const std::int64_t remainder = left % right;
        left = right;
        right = remainder;
    }
    return math_abs(left);
}

extern "C" std::int64_t math_lcm(std::int64_t left, std::int64_t right) {
    if (left == 0 || right == 0) {
        return 0;
    }
    return math_abs((left / math_gcd(left, right)) * right);
}

extern "C" std::int64_t math_is_prime(std::int64_t value) {
    if (value < 2) {
        return 0;
    }
    for (std::int64_t divisor = 2; divisor * divisor <= value; ++divisor) {
        if (value % divisor == 0) {
            return 0;
        }
    }
    return 1;
}

extern "C" std::int64_t math_next_prime(std::int64_t value) {
    std::int64_t candidate = value + 1;
    while (math_is_prime(candidate) == 0) {
        ++candidate;
    }
    return candidate;
}

extern "C" std::int64_t math_binomial(std::int64_t n, std::int64_t k) {
    if (k < 0 || k > n) {
        return 0;
    }
    return math_factorial(n) /
           (math_factorial(k) * math_factorial(n - k));
}

extern "C" std::int64_t math_sum_range(std::int64_t low, std::int64_t high) {
    std::int64_t result = 0;
    for (std::int64_t value = low; value <= high; ++value) {
        result += value;
    }
    return result;
}

extern "C" int lynxer_module_init_v1(RegisterFunction function,
                                      RegisterConstant,
                                      RegisterType) {
    return function("abs", "math_abs", "cdecl:int64(int64)") &&
                   function("max", "math_max", "cdecl:int64(int64,int64)") &&
                   function("min", "math_min", "cdecl:int64(int64,int64)") &&
                   function("clamp", "math_clamp",
                            "cdecl:int64(int64,int64,int64)") &&
                   function("pow", "math_pow", "cdecl:int64(int64,int64)") &&
                   function("sign", "math_sign", "cdecl:int64(int64)") &&
                   function("factorial", "math_factorial",
                            "cdecl:int64(int64)") &&
                   function("gcd", "math_gcd", "cdecl:int64(int64,int64)") &&
                   function("lcm", "math_lcm", "cdecl:int64(int64,int64)") &&
                   function("isPrime", "math_is_prime",
                            "cdecl:int64(int64)") &&
                   function("nextPrime", "math_next_prime",
                            "cdecl:int64(int64)") &&
                   function("binomial", "math_binomial",
                            "cdecl:int64(int64,int64)") &&
                   function("sumRange", "math_sum_range",
                            "cdecl:int64(int64,int64)")
               ? 0
               : 1;
}
