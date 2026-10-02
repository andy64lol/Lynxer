// C consumer for the library built from `lynxer/examples/export_basic.lynx`.
// Links the emitted .so and checks every exported C prototype. Exits non-zero
// on the first mismatch so `make testLynxerEmit` fails loudly.
#include <cstdint>
#include <cstdio>
#include <cstring>

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
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "export consumer FAIL: %s\n", name);
    }
}

std::int64_t framedLength(const std::uint8_t* framed) {
    std::uint64_t length = 0;
    for (int index = 0; index < 8; ++index) {
        length |= static_cast<std::uint64_t>(framed[index]) << (8 * index);
    }
    return static_cast<std::int64_t>(length);
}

} // namespace

int main() {
    check(add(2, 3) == 5, "add");
    check(negate(7) == -7, "negate (int32 alias)");
    check(scale(1.5) == 3.0, "scale");

    const char* greeting = greet("bob");
    check(greeting != nullptr && std::strcmp(greeting, "hi bob") == 0, "greet");

    const std::uint8_t payload[] = {0x00, 0x01, 0xff, 'h', 'i'};
    const std::uint8_t* framed = echoBytes(payload, sizeof(payload));
    check(framed != nullptr, "echoBytes returns a buffer");
    if (framed != nullptr) {
        check(framedLength(framed) == static_cast<std::int64_t>(sizeof(payload)),
              "echoBytes length prefix");
        check(std::memcmp(framed + 8, payload, sizeof(payload)) == 0,
              "echoBytes payload");
    }

    sink(9);

    if (failures == 0) {
        std::printf("export_consumer: ok\n");
    }
    return failures == 0 ? 0 : 1;
}
