// C++ test for the C ABI that Lynxer exposes when a program is built with
// `lynxer --emit-library`. It includes the public `lynxer.h` header (so the
// shipped header is exercised under C++), declares the exported prototypes the
// way any C++ consumer must, and checks every one. Exits non-zero on failure so
// `make testLynxerEmit` fails loudly.
#include "lynxer.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

// The C symbols exported by the library built from export_basic.lynx.
extern "C" {
std::int64_t add(std::int64_t left, std::int64_t right);
std::int64_t negate(std::int64_t value);
double scale(double value);
const char* greet(const char* who);
const std::uint8_t* echoBytes(const std::uint8_t* data, std::int64_t length);
void sink(std::int64_t value);
}

namespace {

int failures = 0;

void check(bool ok, const char* name) {
    if (ok) {
        std::printf("ok   %s\n", name);
    } else {
        ++failures;
        std::fprintf(stderr, "FAIL %s\n", name);
    }
}

// A `bytes` return is `[int64 little-endian length][payload]`.
std::int64_t framedLength(const std::uint8_t* framed) {
    std::uint64_t length = 0;
    for (int index = 0; index < 8; ++index) {
        length |= static_cast<std::uint64_t>(framed[index]) << (8 * index);
    }
    return static_cast<std::int64_t>(length);
}

} // namespace

int main() {
    check(add(2, 3) == 5, "add(int64,int64)");
    check(negate(7) == -7, "negate(int64) via int32 alias");
    check(scale(1.5) == 3.0, "scale(double)");

    const char* greeting = greet("bob");
    check(greeting != nullptr && std::strcmp(greeting, "hi bob") == 0,
          "greet(cstring)");

    const std::uint8_t payload[] = {0x00, 0x01, 0xff, 'h', 'i'};
    const std::uint8_t* framed = echoBytes(payload, sizeof(payload));
    check(framed != nullptr, "echoBytes(bytes) returns a buffer");
    if (framed != nullptr) {
        check(framedLength(framed) == static_cast<std::int64_t>(sizeof(payload)),
              "echoBytes length prefix");
        check(std::memcmp(framed + 8, payload, sizeof(payload)) == 0,
              "echoBytes payload");
    }

    sink(9);
    check(true, "sink(void)");

    if (failures == 0) {
        std::printf("export_test: all C ABI checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
