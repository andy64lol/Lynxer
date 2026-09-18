#pragma once

#include <stdexcept>

namespace clynxer {

struct InterruptError : std::runtime_error {
    InterruptError() : std::runtime_error("interrupted") {}
};

void requestInterrupt();
bool interruptRequested();
void throwIfInterrupted();
void installInterruptHandler();

} // namespace clynxer
