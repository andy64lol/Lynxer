#include "interrupt.hpp"

#include <csignal>

namespace lynxer {

namespace {

volatile std::sig_atomic_t interrupted = 0;

void handleInterrupt(int) { interrupted = 1; }

} // namespace

void requestInterrupt() { interrupted = 1; }

bool interruptRequested() { return interrupted != 0; }

void throwIfInterrupted() {
    if (interruptRequested()) {
        throw InterruptError();
    }
}

void installInterruptHandler() {
#if defined(__unix__) || defined(__APPLE__)
    struct sigaction action {};
    action.sa_handler = handleInterrupt;
    sigemptyset(&action.sa_mask);
    // Deliberately no SA_RESTART: a blocking read must return EINTR so the
    // interpreter regains control and can turn it into an InterruptError.
    // Without this, Ctrl-C is ignored while the program waits for stdin.
    action.sa_flags = 0;
    sigaction(SIGINT, &action, nullptr);
#else
    std::signal(SIGINT, handleInterrupt);
#endif
}

} // namespace lynxer
