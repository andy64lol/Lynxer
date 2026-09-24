#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <array>
#include <memory>
#include <string>
#include <unistd.h>
#include <utility>
using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);
static const char* stable(std::string value) { thread_local std::string r; r=std::move(value); return r.c_str(); }
extern "C" std::int64_t shell_runShell(const char* command) { return std::system(command); }
extern "C" const char* shell_runShellCapture(const char* command) {
    std::array<char,256> buffer{}; std::string output;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command,"r"), pclose);
    if (!pipe) return stable("");
    while (fgets(buffer.data(), buffer.size(), pipe.get())) output += buffer.data();
    return stable(output);
}
extern "C" std::int64_t shell_runShellSilent(const char* command) {
    return std::system((std::string(command) + " >/dev/null 2>&1").c_str());
}
extern "C" const char* shell_runShellErr(const char* command) {
    std::array<char,256> buffer{}; std::string output;
    const std::string full = std::string(command) + " 2>&1 1>/dev/null";
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(full.c_str(),"r"), pclose);
    if (!pipe) return stable("");
    while (fgets(buffer.data(), buffer.size(), pipe.get())) output += buffer.data();
    return stable(output);
}
extern "C" std::int64_t shell_commandExists(const char* command) {
    std::string query = "command -v " + std::string(command) + " >/dev/null 2>&1";
    return std::system(query.c_str()) == 0;
}
extern "C" std::int64_t shell_checkShell(const char* command) {
    return shell_commandExists(command);
}
extern "C" std::int64_t shell_runShellCode(const char* command) {
    return shell_runShell(command);
}
extern "C" std::int64_t shell_runShellAs(const char* shell, const char* command) {
    std::string full = std::string(shell) + " -c '" + command + "'";
    return std::system(full.c_str());
}
extern "C" const char* shell_runShellCaptureAs(const char* shell, const char* command) {
    std::string full = std::string(shell) + " -c '" + command + "'";
    return shell_runShellCapture(full.c_str());
}
extern "C" const char* shell_currentShell() {
    const char* shell = std::getenv("SHELL");
    return stable(shell ? shell : "");
}
extern "C" const char* shell_shellPath(const char* command) {
    static thread_local std::string path;
    std::string query = "command -v " + std::string(command);
    FILE* pipe = popen(query.c_str(),"r"); if (!pipe) return stable("");
    char buffer[512]{}; path = fgets(buffer,sizeof(buffer),pipe) ? buffer : ""; pclose(pipe);
    while (!path.empty() && (path.back()=='\n'||path.back()=='\r')) path.pop_back();
    return path.c_str();
}
extern "C" const char* shell_shellVersion(const char* shell) {
    const std::string query = std::string(shell) + " --version 2>/dev/null";
    FILE* pipe = popen(query.c_str(), "r");
    if (!pipe) return stable("");
    char buffer[512]{};
    std::string output;
    if (fgets(buffer, sizeof(buffer), pipe)) output = buffer;
    pclose(pipe);
    while (!output.empty() && (output.back()=='\n' || output.back()=='\r')) output.pop_back();
    return stable(output);
}
extern "C" const char* shell_availableShells() {
    static const char* candidates[] = {"sh", "bash", "zsh", "dash", "ksh", "fish", "csh", "tcsh"};
    std::string json = "[";
    bool first = true;
    for (const char* candidate : candidates) {
        if (shell_commandExists(candidate)) {
            if (!first) json += ", ";
            json += "\"";
            json += candidate;
            json += "\"";
            first = false;
        }
    }
    json += "]";
    return stable(json);
}
extern "C" int clynxer_module_init_v1(RegisterFunction f, RegisterConstant, RegisterType) {
    return f("runShell","shell_runShell","cdecl:int64(cstring)") &&
           f("runShellCapture","shell_runShellCapture","cdecl:cstring(cstring)") &&
           f("runShellSilent","shell_runShellSilent","cdecl:int64(cstring)") &&
           f("runShellErr","shell_runShellErr","cdecl:cstring(cstring)") &&
           f("commandExists","shell_commandExists","cdecl:int64(cstring)") &&
           f("checkShell","shell_checkShell","cdecl:int64(cstring)") &&
           f("runShellCode","shell_runShellCode","cdecl:int64(cstring)") &&
           f("runShellAs","shell_runShellAs","cdecl:int64(cstring,cstring)") &&
           f("runShellCaptureAs","shell_runShellCaptureAs",
             "cdecl:cstring(cstring,cstring)") &&
           f("currentShell","shell_currentShell","cdecl:cstring()") &&
           f("shellPath","shell_shellPath","cdecl:cstring(cstring)") &&
           f("shellVersion","shell_shellVersion","cdecl:cstring(cstring)") &&
           f("availableShells","shell_availableShells","cdecl:cstring()") ? 0 : 1;
}
