#include <cstdint>

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

extern "C" std::int64_t add(std::int64_t left, std::int64_t right) {
    return left + right;
}

extern "C" int lynxer_module_init_v1(RegisterFunction registerFunction,
                                      RegisterConstant registerConstant,
                                      RegisterType registerType) {
    (void)registerType;
    return registerFunction("add", "add", "cdecl:int64(int64,int64)") &&
                   registerConstant("version", 6)
               ? 0
               : 1;
}
