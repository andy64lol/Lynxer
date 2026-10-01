#pragma once

#include <exception>
#include <stdexcept>

namespace lynxer {

struct InterruptError : std::runtime_error {
    InterruptError() : std::runtime_error("interrupted") {}
};

struct ExitControl : std::exception {
    explicit ExitControl(int code) : code(code) {}

    const char* what() const noexcept override { return "program requested exit"; }

    int code;
};

void requestInterrupt();
bool interruptRequested();
void throwIfInterrupted();
void clearExitRequest(int code);
void throwIfExitRequested();
void installInterruptHandler();

} // namespace lynxer
