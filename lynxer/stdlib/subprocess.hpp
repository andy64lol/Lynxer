#pragma once

#include <cerrno>
#include <chrono>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace lynxer_subprocess {

struct Result {
    std::string output;
    int status = -1;
    bool timedOut = false;
};

inline Result run(const std::string& command, int timeoutSeconds) {
    Result result;
    int pipes[2];
    if (::pipe(pipes) != 0) {
        return result;
    }

    const pid_t child = ::fork();
    if (child < 0) {
        ::close(pipes[0]);
        ::close(pipes[1]);
        return result;
    }
    if (child == 0) {
        ::close(pipes[0]);
        ::setpgid(0, 0);
        if (::dup2(pipes[1], STDOUT_FILENO) < 0 ||
            ::dup2(pipes[1], STDERR_FILENO) < 0) {
            _exit(126);
        }
        ::close(pipes[1]);
        ::execl("/bin/sh", "sh", "-c", command.c_str(),
                static_cast<char*>(nullptr));
        _exit(127);
    }

    ::close(pipes[1]);
    ::setpgid(child, child);
    const int flags = ::fcntl(pipes[0], F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(pipes[0], F_SETFL, flags | O_NONBLOCK);
    }

    const auto started = std::chrono::steady_clock::now();
    bool reaped = false;
    bool eof = false;
    int waitStatus = 0;
    char buffer[4096];
    while (!reaped || !eof) {
        if (timeoutSeconds > 0 &&
            std::chrono::steady_clock::now() - started >=
                std::chrono::seconds(timeoutSeconds)) {
            result.timedOut = true;
            ::kill(-child, SIGKILL);
            ::kill(child, SIGKILL);
            timeoutSeconds = 0;
        }

        pollfd descriptor{pipes[0], POLLIN | POLLHUP, 0};
        const int ready = ::poll(&descriptor, 1, 25);
        if (ready > 0 &&
            (descriptor.revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            while (true) {
                const ssize_t count = ::read(pipes[0], buffer, sizeof(buffer));
                if (count > 0) {
                    result.output.append(buffer, static_cast<std::size_t>(count));
                } else if (count == 0) {
                    eof = true;
                    break;
                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                } else if (errno != EINTR) {
                    eof = true;
                    break;
                }
            }
        }
        if (!reaped) {
            const pid_t waited = ::waitpid(child, &waitStatus, WNOHANG);
            if (waited == child) {
                reaped = true;
            } else if (waited < 0 && errno != EINTR) {
                reaped = true;
            }
        }
        if (reaped && !eof && ready == 0) {
            // An exited shell may leave a grandchild holding the pipe open.
            // Keep enforcing the command deadline against the entire group.
            if (timeoutSeconds == 0 && result.timedOut) {
                ::kill(-child, SIGKILL);
            }
        }
    }
    ::close(pipes[0]);

    if (result.timedOut) {
        result.status = 124;
    } else if (WIFEXITED(waitStatus)) {
        result.status = WEXITSTATUS(waitStatus);
    } else if (WIFSIGNALED(waitStatus)) {
        result.status = 128 + WTERMSIG(waitStatus);
    }
    return result;
}

} // namespace lynxer_subprocess
