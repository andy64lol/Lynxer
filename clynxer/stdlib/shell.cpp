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
extern "C" std::int64_t shell_runShellSilent(const char* command) { return std::system(command); }
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
extern "C" int lynxer_module_init_v1(RegisterFunction f, RegisterConstant, RegisterType) {
    return f("runShell","shell_runShell","cdecl:int64(cstring)") &&
           f("runShellCapture","shell_runShellCapture","cdecl:cstring(cstring)") &&
           f("runShellSilent","shell_runShellSilent","cdecl:int64(cstring)") &&
           f("commandExists","shell_commandExists","cdecl:int64(cstring)") &&
           f("checkShell","shell_checkShell","cdecl:int64(cstring)") &&
           f("runShellCode","shell_runShellCode","cdecl:int64(cstring)") &&
           f("runShellAs","shell_runShellAs","cdecl:int64(cstring,cstring)") &&
           f("runShellCaptureAs","shell_runShellCaptureAs",
             "cdecl:cstring(cstring,cstring)") &&
           f("currentShell","shell_currentShell","cdecl:cstring()") &&
           f("shellPath","shell_shellPath","cdecl:cstring(cstring)") ? 0 : 1;
}
