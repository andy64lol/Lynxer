#pragma once

#include <chrono>
#include <algorithm>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include <cwchar>
#else
#include <cerrno>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace lynxer_subprocess {

struct Result {
    std::string output;
    int status = -1;
    bool timedOut = false;
};

inline Result run(const std::string& command, int timeoutSeconds) {
    Result result;
#if defined(_WIN32)
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!::CreatePipe(&readPipe, &writePipe, &security, 0)) return result;
    ::SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe; startup.hStdError = writePipe;
    startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job == nullptr || !::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        if (job) ::CloseHandle(job);
        ::CloseHandle(readPipe); ::CloseHandle(writePipe); return result;
    }
    wchar_t comspec[MAX_PATH + 1]{};
    DWORD comspecLength = ::GetEnvironmentVariableW(L"COMSPEC", comspec, MAX_PATH);
    if (comspecLength == 0 || comspecLength > MAX_PATH) wcscpy(comspec, L"cmd.exe");
    std::wstring wideCommand(static_cast<std::size_t>(::MultiByteToWideChar(CP_UTF8, 0, command.data(), static_cast<int>(command.size()), nullptr, 0)), L'\0');
    if (!wideCommand.empty()) ::MultiByteToWideChar(CP_UTF8, 0, command.data(), static_cast<int>(command.size()), wideCommand.data(), static_cast<int>(wideCommand.size()));
    std::wstring line = L"\"" + std::wstring(comspec) + L"\" /D /S /C \"" + wideCommand + L"\"";
    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(comspec, line.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
                          nullptr, nullptr, &startup, &process)) {
        ::CloseHandle(job); ::CloseHandle(readPipe); ::CloseHandle(writePipe); return result;
    }
    ::CloseHandle(writePipe);
    if (!::AssignProcessToJobObject(job, process.hProcess)) {
        ::TerminateProcess(process.hProcess, 127);
        ::CloseHandle(readPipe); ::CloseHandle(process.hThread); ::CloseHandle(process.hProcess); ::CloseHandle(job); return result;
    }
    ::ResumeThread(process.hThread);
    const auto started = std::chrono::steady_clock::now();
    bool reaped = false, eof = false;
    DWORD exitCode = 1;
    char buffer[4096];
    while (!reaped || !eof) {
        if (timeoutSeconds > 0 && !result.timedOut &&
            std::chrono::steady_clock::now() - started >= std::chrono::seconds(timeoutSeconds)) {
            result.timedOut = true;
            ::TerminateJobObject(job, 124);
        }
        DWORD available = 0;
        if (!eof && ::PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            DWORD count = 0;
            if (::ReadFile(readPipe, buffer, static_cast<DWORD>(std::min<std::size_t>(sizeof(buffer), available)), &count, nullptr) && count > 0)
                result.output.append(buffer, count);
        }
        if (!reaped && ::WaitForSingleObject(process.hProcess, 25) == WAIT_OBJECT_0) {
            reaped = true;
            ::GetExitCodeProcess(process.hProcess, &exitCode);
        }
        if (reaped && !eof) {
            DWORD count = 0;
            if (!::PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr)) {
                eof = ::GetLastError() == ERROR_BROKEN_PIPE;
            } else if (available > 0 && ::ReadFile(readPipe, buffer, static_cast<DWORD>(std::min<std::size_t>(sizeof(buffer), available)), &count, nullptr) && count > 0) {
                result.output.append(buffer, count);
            } else if (available == 0) {
                ::Sleep(10);
            }
        }
    }
    result.status = result.timedOut ? 124 : static_cast<int>(exitCode);
    ::CloseHandle(readPipe); ::CloseHandle(process.hThread); ::CloseHandle(process.hProcess); ::CloseHandle(job);
    return result;
#else
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
#endif
}

} // namespace lynxer_subprocess
