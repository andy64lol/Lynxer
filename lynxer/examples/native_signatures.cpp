// Test-only native module used by examples/native_signatures.lynx.
//
// It registers exactly one function per native-call signature shape supported
// by lynxer's `callNative` dispatcher, so the fixture fails loudly if any
// shape regresses. Every function returns a deterministic value.

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

static const char* stable(std::string value) {
    thread_local std::string result;
    result = std::move(value);
    return result.c_str();
}

extern "C" std::int64_t sig_v() { return 7; }
extern "C" double sig_d() { return 2.5; }
extern "C" const char* sig_s() { return "zero"; }

extern "C" std::int64_t sig_ii(std::int64_t a, std::int64_t b) { return a + b; }
extern "C" std::int64_t sig_i(std::int64_t a) { return a; }
extern "C" const char* sig_ss_arg(const char* a) { return stable(std::string(a)); }
extern "C" std::int64_t sig_si(const char* a) {
    return static_cast<std::int64_t>(std::strlen(a));
}
extern "C" std::int64_t sig_sss2(const char* a, const char* b) {
    return std::strcmp(a, b) == 0 ? 1 : 0;
}
extern "C" const char* sig_sn(const char* a, std::int64_t n) {
    return stable(std::string(n < 0 ? 0 : static_cast<std::size_t>(n), a[0]));
}
extern "C" std::int64_t sig_iii(std::int64_t a, std::int64_t b, std::int64_t c) {
    return a + b + c;
}
extern "C" double sig_dd_arg(double x) { return x; }
extern "C" double sig_dd(double x, double y) { return x + y; }
extern "C" double sig_ddd(double x, double y, double z) { return x + y + z; }
extern "C" std::int64_t sig_di(double x) { return static_cast<std::int64_t>(x); }
extern "C" double sig_dd_i(double x, std::int64_t n) {
    return x + static_cast<double>(n);
}

extern "C" const char* sig_ds(double x) {
    return stable(std::to_string(x));
}
extern "C" double sig_sd(const char* a) {
    return std::strtod(a, nullptr);
}
extern "C" const char* sig_ss(const char* a, const char* b) {
    return stable(std::string(a) + b);
}
extern "C" double sig_sdd(const char* a, const char* b) {
    return std::strtod(a, nullptr) + std::strtod(b, nullptr);
}
extern "C" const char* sig_sss(const char* a, const char* b, const char* c) {
    return stable(std::string(a) + b + c);
}
extern "C" const char* sig_ssi(const char* a, const char* b, std::int64_t n) {
    return stable(std::string(a) + b + std::to_string(n));
}
extern "C" const char* sig_sssi(const char* a, const char* b, const char* c,
                                std::int64_t n) {
    return stable(std::string(a) + b + c + std::to_string(n));
}
extern "C" std::int64_t sig_si_n(const char* a, std::int64_t n) {
    return static_cast<std::int64_t>(std::strlen(a)) + n;
}
extern "C" const char* sig_is(std::int64_t n) {
    return stable("n" + std::to_string(n));
}
extern "C" double sig_id(std::int64_t n) {
    return static_cast<double>(n);
}
extern "C" std::int64_t sig_sii(const char* a, std::int64_t n, std::int64_t m) {
    return static_cast<std::int64_t>(std::strlen(a)) + n + m;
}

extern "C" int lynxer_module_init_v1(RegisterFunction f, RegisterConstant,
                                     RegisterType) {
    return f("v", "sig_v", "cdecl:int64()") &&
                   f("d", "sig_d", "cdecl:double()") &&
                   f("s", "sig_s", "cdecl:cstring()") &&
                   f("ii", "sig_ii", "cdecl:int64(int64,int64)") &&
                   f("i", "sig_i", "cdecl:int64(int64)") &&
                   f("ssArg", "sig_ss_arg", "cdecl:cstring(cstring)") &&
                   f("si", "sig_si", "cdecl:int64(cstring)") &&
                   f("ssEq", "sig_sss2", "cdecl:int64(cstring,cstring)") &&
                   f("sn", "sig_sn", "cdecl:cstring(cstring,int64)") &&
                   f("iii", "sig_iii", "cdecl:int64(int64,int64,int64)") &&
                   f("ddArg", "sig_dd_arg", "cdecl:double(double)") &&
                   f("dd", "sig_dd", "cdecl:double(double,double)") &&
                   f("ddd", "sig_ddd", "cdecl:double(double,double,double)") &&
                   f("di", "sig_di", "cdecl:int64(double)") &&
                   f("ddI", "sig_dd_i", "cdecl:double(double,int64)") &&
                   f("ds", "sig_ds", "cdecl:cstring(double)") &&
                   f("sd", "sig_sd", "cdecl:double(cstring)") &&
                   f("ss", "sig_ss", "cdecl:cstring(cstring,cstring)") &&
                   f("sdd", "sig_sdd", "cdecl:double(cstring,cstring)") &&
                   f("sss", "sig_sss", "cdecl:cstring(cstring,cstring,cstring)") &&
                   f("ssi", "sig_ssi", "cdecl:cstring(cstring,cstring,int64)") &&
                   f("sssi", "sig_sssi",
                     "cdecl:cstring(cstring,cstring,cstring,int64)") &&
                   f("siN", "sig_si_n", "cdecl:int64(cstring,int64)") &&
                   f("is", "sig_is", "cdecl:cstring(int64)") &&
                   f("id", "sig_id", "cdecl:double(int64)") &&
                   f("sii", "sig_sii", "cdecl:int64(cstring,int64,int64)")
               ? 0
               : 1;
}
