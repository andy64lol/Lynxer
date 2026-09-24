// Clynxer `multiprocessing` stdlib backend: run shell commands in parallel.
//
// Results are returned through an integer handle registry rather than a joined
// string, because command output can contain any character (including the
// separators a string bridge would need). The `.lynx` wrapper reads results back
// with resultCount/resultAt/codeAt.

#include "native_json.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

static const char* stable(std::string value) {
    thread_local std::string result;
    result = std::move(value);
    return result.c_str();
}

static std::string textOrEmpty(const char* text) {
    return text == nullptr ? std::string() : std::string(text);
}

struct Job {
    std::vector<std::string> outputs;
    std::vector<std::int64_t> codes;
};

static std::map<std::int64_t, Job>& jobs() {
    static std::map<std::int64_t, Job> instance;
    return instance;
}

static std::int64_t nextHandle() {
    static std::int64_t counter = 0;
    return ++counter;
}

static std::mutex& jobsMutex() {
    static std::mutex instance;
    return instance;
}

static std::string captureCommand(const std::string& command,
                                  std::int64_t& exitCode) {
    std::string output;
    std::array<char, 512> buffer {};
    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        exitCode = -1;
        return output;
    }
    while (::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) !=
           nullptr) {
        output += buffer.data();
    }
    const int raw = ::pclose(pipe);
    if (raw == -1) {
        exitCode = -1;
    } else if (WIFEXITED(raw)) {
        exitCode = WEXITSTATUS(raw);
    } else {
        exitCode = -1;
    }
    return output;
}

// Runs every command, capturing stdout and the exit status. Results are stored
// in input order regardless of completion order.
static Job runCommands(const std::vector<std::string>& commands) {
    Job job;
    job.outputs.resize(commands.size());
    job.codes.assign(commands.size(), 0);
    if (commands.empty()) {
        return job;
    }
    unsigned int workers = std::thread::hardware_concurrency();
    if (workers == 0) {
        workers = 1;
    }
    if (workers > commands.size()) {
        workers = static_cast<unsigned int>(commands.size());
    }
    std::atomic<std::size_t> next {0};
    const auto work = [&]() {
        while (true) {
            const std::size_t index = next.fetch_add(1);
            if (index >= commands.size()) {
                return;
            }
            std::int64_t code = 0;
            job.outputs[index] = captureCommand(commands[index], code);
            job.codes[index] = code;
        }
    };
    std::vector<std::thread> pool;
    pool.reserve(workers);
    for (unsigned int index = 0; index < workers; ++index) {
        pool.emplace_back(work);
    }
    for (auto& thread : pool) {
        thread.join();
    }
    return job;
}

static std::vector<std::string> parseStringArray(const std::string& json) {
    std::vector<std::string> values;
    native_json::Value parsed;
    if (!native_json::parse(json, parsed) ||
        parsed.type != native_json::Type::Array) {
        return values;
    }
    for (const auto& item : parsed.items) {
        if (item.type == native_json::Type::String) {
            values.push_back(item.text);
        } else {
            values.push_back(native_json::dump(item, false));
        }
    }
    return values;
}

static std::int64_t store(const Job& job) {
    std::lock_guard<std::mutex> guard(jobsMutex());
    const std::int64_t handle = nextHandle();
    jobs()[handle] = job;
    return handle;
}

extern "C" std::int64_t multiprocessing_workerCount() {
    const long count = ::sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? static_cast<std::int64_t>(count) : 1;
}

extern "C" std::int64_t multiprocessing_runParallel(const char* commands) {
    return store(runCommands(parseStringArray(textOrEmpty(commands))));
}

extern "C" std::int64_t multiprocessing_runParallelSilent(
    const char* commands) {
    return store(runCommands(parseStringArray(textOrEmpty(commands))));
}

// Command execution is always process-based here; this is an alias kept for API
// compatibility with the Python reference's process pool.
extern "C" std::int64_t multiprocessing_runParallelProcess(
    const char* commands) {
    return store(runCommands(parseStringArray(textOrEmpty(commands))));
}

extern "C" std::int64_t multiprocessing_mapShell(const char* commandTemplate,
                                                 const char* items) {
    const std::string pattern = textOrEmpty(commandTemplate);
    std::vector<std::string> commands;
    for (const auto& item : parseStringArray(textOrEmpty(items))) {
        const std::size_t marker = pattern.find("{}");
        std::string command = pattern;
        if (marker != std::string::npos) {
            command = pattern.substr(0, marker) + item +
                      pattern.substr(marker + 2);
        }
        commands.push_back(command);
    }
    return store(runCommands(commands));
}

static const Job* findJob(std::int64_t handle) {
    const auto found = jobs().find(handle);
    return found == jobs().end() ? nullptr : &found->second;
}

extern "C" std::int64_t multiprocessing_resultCount(std::int64_t handle) {
    std::lock_guard<std::mutex> guard(jobsMutex());
    const Job* job = findJob(handle);
    return job == nullptr ? 0 : static_cast<std::int64_t>(job->outputs.size());
}

extern "C" const char* multiprocessing_resultAt(std::int64_t handle,
                                                std::int64_t index) {
    std::lock_guard<std::mutex> guard(jobsMutex());
    const Job* job = findJob(handle);
    if (job == nullptr || index < 0 ||
        static_cast<std::size_t>(index) >= job->outputs.size()) {
        return stable("");
    }
    return stable(job->outputs[static_cast<std::size_t>(index)]);
}

extern "C" std::int64_t multiprocessing_codeAt(std::int64_t handle,
                                               std::int64_t index) {
    std::lock_guard<std::mutex> guard(jobsMutex());
    const Job* job = findJob(handle);
    if (job == nullptr || index < 0 ||
        static_cast<std::size_t>(index) >= job->codes.size()) {
        return -1;
    }
    return job->codes[static_cast<std::size_t>(index)];
}

extern "C" std::int64_t multiprocessing_release(std::int64_t handle) {
    std::lock_guard<std::mutex> guard(jobsMutex());
    return jobs().erase(handle) > 0 ? 1 : 0;
}

extern "C" int clynxer_module_init_v1(RegisterFunction function,
                                     RegisterConstant, RegisterType) {
    return function("workerCount", "multiprocessing_workerCount",
                            "cdecl:int64()") &&
                   function("runParallel", "multiprocessing_runParallel",
                            "cdecl:int64(cstring)") &&
                   function("runParallelSilent",
                            "multiprocessing_runParallelSilent",
                            "cdecl:int64(cstring)") &&
                   function("runParallelProcess",
                            "multiprocessing_runParallelProcess",
                            "cdecl:int64(cstring)") &&
                   function("mapShell", "multiprocessing_mapShell",
                            "cdecl:int64(cstring,cstring)") &&
                   function("resultCount", "multiprocessing_resultCount",
                            "cdecl:int64(int64)") &&
                   function("resultAt", "multiprocessing_resultAt",
                            "cdecl:cstring(int64,int64)") &&
                   function("codeAt", "multiprocessing_codeAt",
                            "cdecl:int64(int64,int64)") &&
                   function("release", "multiprocessing_release",
                            "cdecl:int64(int64)")
               ? 0
               : 1;
}
