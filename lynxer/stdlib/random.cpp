// Lynxer `random` stdlib backend: a small seeded pseudo-random generator.
//
// The Python reference delegated to the `random` module. Clynxer needs explicit
// state, and Lynxer modules cannot declare module-level variables, so the state
// lives here instead of in the wrapper. The generator is a linear congruential
// sequence; seeding makes results reproducible.

#include <cmath>
#include <cstdint>
#include <string>

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

static const std::int64_t kInitialState = 881726454;

static std::int64_t& state() {
    static std::int64_t value = kInitialState;
    return value;
}

static std::int64_t nextValue() {
    state() = (state() * 1103515245 + 12345) % 2147483647;
    return state();
}

static const char* stable(std::string value) {
    thread_local std::string result;
    result = std::move(value);
    return result.c_str();
}

static std::string textOrEmpty(const char* text) {
    return text == nullptr ? std::string() : std::string(text);
}

extern "C" std::int64_t random_seed(std::int64_t value) {
    state() = value == 0 ? kInitialState : value;
    return 0;
}

extern "C" std::int64_t random_randint(std::int64_t low, std::int64_t high) {
    const std::int64_t value = nextValue();
    if (high <= low) {
        return low;
    }
    return low + value % (high - low + 1);
}

extern "C" std::int64_t random_randrange(std::int64_t start,
                                         std::int64_t stop) {
    if (stop <= start) {
        return start;
    }
    return random_randint(start, stop - 1);
}

extern "C" double random_random() {
    return static_cast<double>(nextValue()) / 2147483647.0;
}

extern "C" double random_uniform(double low, double high) {
    return low + (high - low) * random_random();
}

extern "C" std::int64_t random_coinflip() {
    return random_randint(0, 1) == 1 ? 1 : 0;
}

// Picks one character of the input, matching the reference's implementation.
extern "C" const char* random_choice(const char* items) {
    const std::string text = textOrEmpty(items);
    if (text.empty()) {
        return stable("");
    }
    const std::int64_t index =
        random_randint(0, static_cast<std::int64_t>(text.size()) - 1);
    return stable(std::string(1, text[static_cast<std::size_t>(index)]));
}

// Gaussian via Box-Muller. CPython caches a second value from one pair; the
// distribution is the same, the exact sequence is not (the generators differ).
extern "C" double random_gauss(double mu, double sigma) {
    double u1 = random_random();
    if (u1 <= 0.0) {
        u1 = 1e-12;
    }
    const double u2 = random_random();
    const double magnitude = std::sqrt(-2.0 * std::log(u1));
    return mu + sigma * magnitude * std::cos(2.0 * 3.14159265358979323846 * u2);
}

// Triangular distribution by inverse CDF, matching Python's triangular().
extern "C" double random_triangular(double low, double high, double mode) {
    if (high <= low) {
        return low;
    }
    const double u = random_random();
    const double split = (mode - low) / (high - low);
    if (u < split) {
        return low + std::sqrt(u * (high - low) * (mode - low));
    }
    return high - std::sqrt((1.0 - u) * (high - low) * (high - mode));
}

extern "C" int lynxer_module_init_v1(RegisterFunction function,
                                     RegisterConstant, RegisterType) {
    return function("seed", "random_seed", "cdecl:int64(int64)") &&
                   function("randint", "random_randint",
                            "cdecl:int64(int64,int64)") &&
                   function("randrange", "random_randrange",
                            "cdecl:int64(int64,int64)") &&
                   function("random", "random_random", "cdecl:double()") &&
                   function("uniform", "random_uniform",
                            "cdecl:double(double,double)") &&
                   function("coinflip", "random_coinflip", "cdecl:int64()") &&
                   function("gauss", "random_gauss",
                            "cdecl:double(double,double)") &&
                   function("triangular", "random_triangular",
                            "cdecl:double(double,double,double)") &&
                   function("choice", "random_choice",
                            "cdecl:cstring(cstring)")
               ? 0
               : 1;
}
