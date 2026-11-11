#include "shell.hpp"

#include "bundle.hpp"
#include "config.hpp"
#include "debugger.hpp"
#include "error.hpp"
#include "exports.hpp"
#include "formatter.hpp"
#include "interrupt.hpp"
#include "lexer.hpp"
#include "lint.hpp"
#include "native_name.hpp"
#include "optimizer.hpp"
#include "parser.hpp"
#include "platform.hpp"
#include "runtime.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>


namespace lynxer {

namespace {

// The program's own command line, set by `shellMain` before it hands the
// program to `runProgram`: the script path followed by the arguments after it,
// or the whole command line for a compiled executable. It is what `sys.argv`
// reports instead of the interpreter's process command line.
std::vector<std::string> programArguments;

std::string readFile(const std::string& path, const std::string& display,
                     bool& ok) {
    std::ifstream input(path);
    if (!input) {
        std::cerr << Config::instance().format("error.file_not_found",
                                               "lynxer: file not found: '{0}'",
                                               "{0}", display)
                  << '\n';
        ok = false;
        return "";
    }
    std::ostringstream content;
    content << input.rdbuf();
    ok = true;
    return content.str();
}

// `<file>:<line>:<column>` for a source error. A failure raised inside an
// imported module names the module file, not the program that imported it.
std::string describeSourceError(const std::string& display,
                                const SourceError& error) {
    const std::string& where = error.source.empty() ? display : error.source;
    return where + ":" + std::to_string(error.line) + ":" +
           std::to_string(error.column);
}

std::string sourceErrorTip(const SourceError& error) {
    const std::string message = error.what();
    if (message.rfind("unknown variable ", 0) == 0) {
        return "Check the spelling and make sure the variable is declared in this scope before it is used.";
    }
    if (message.rfind("unknown function ", 0) == 0) {
        return "Check the function name and confirm its module is imported, if it is not a builtin.";
    }
    if (message.rfind("unknown class ", 0) == 0 ||
        message.rfind("unknown type ", 0) == 0) {
        return "Check the name's spelling and make sure the declaration or module that defines it is in scope.";
    }
    if (message.rfind("unknown macro ", 0) == 0) {
        return "Check the macro name and import the file that declares it. Macro calls must include '!'.";
    }
    if (message.rfind("module '", 0) == 0 &&
        message.find(" was not found") != std::string::npos) {
        return "Check the module path and extension, then verify the file is in the current directory or Lynxer's module search path.";
    }
    if (message.find("division by zero") != std::string::npos) {
        return "Check the divisor before dividing; use a condition or try/catch if zero is a valid input.";
    }
    if (message.rfind("expected ", 0) == 0 ||
        message.find("expected") != std::string::npos) {
        return "Check the nearby syntax, especially matching parentheses, braces, quotes, and statement terminators.";
    }
    if (message.find("expects ") != std::string::npos ||
        message.find("argument") != std::string::npos) {
        return "Compare the call with the function signature and check each argument's type and order.";
    }
    if (message.find("requires ") != std::string::npos ||
        message.find(" must be ") != std::string::npos) {
        return "Check the value types and constraints required by this operation.";
    }
    return {};
}

std::string formatSourceError(const std::string& display,
                              const std::string& source,
                              const SourceError& error) {
    std::ostringstream message;
    message << "lynxer: " << describeSourceError(display, error) << ": "
            << '[' << error.code << "] " << error.what();

    const std::string tip = sourceErrorTip(error);
    if (error.line <= 0 || error.column <= 0) {
        if (!tip.empty()) message << "\nhelp: " << tip;
        return message.str();
    }

    std::string errorSource;
    if (error.source.empty() || error.source == display) {
        errorSource = source;
    } else {
        std::ifstream input(error.source, std::ios::binary);
        if (input) {
            std::ostringstream contents;
            contents << input.rdbuf();
            errorSource = contents.str();
        }
    }

    std::istringstream lines(errorSource);
    std::string sourceLine;
    for (int current = 1; current <= error.line; ++current) {
        if (!std::getline(lines, sourceLine)) {
            if (!tip.empty()) message << "\nhelp: " << tip;
            return message.str();
        }
    }
    if (!sourceLine.empty() && sourceLine.back() == '\r') sourceLine.pop_back();

    std::string caret;
    for (int column = 1; column < error.column; ++column) {
        caret += column <= static_cast<int>(sourceLine.size()) &&
                         sourceLine[static_cast<std::size_t>(column - 1)] == '\t'
                     ? "    "
                     : " ";
    }
    message << "\n  |\n" << error.line << " | " << sourceLine << "\n  | "
            << caret << '^';
    if (!tip.empty()) message << "\nhelp: " << tip;
    return message.str();
}

void printUsage() {
    std::cout << "\n";
    std::cout << "Usage:\n";
    std::cout << "  lynxer <file.lynx>                          Run a Lynxer source file\n";
    std::cout << "  lynxer --no-opt <file.lynx>                 Run without the AST optimizer\n";
    std::cout << "  lynxer --init [name]                        Scaffold a Lynxer project in the current directory\n";
    std::cout << "  lynxer --build                              Compile the project entry into build/\n";
    std::cout << "  lynxer --run                                Build the project, then run the result\n";
    std::cout << "  lynxer --lint <file.lynx>                   Check syntax and run static analysis\n";
    std::cout << "  lynxer --debug <file.lynx> [args...]         Run with the source debugger\n";
    std::cout << "  lynxer --ast <file.lynx>                    Parse and print the abstract syntax tree\n";
    std::cout << "  lynxer --format <file.lynx>                 Rewrite a file with canonical spacing\n";
    std::cout << "  lynxer --format-oneline <file.lynx>         Collapse a file onto one physical line\n";
    std::cout << "  lynxer --compile <a.lynx> [options] [name]  Compile input files into one executable\n";
    std::cout << "      --include <file>                         Embed a module, native library or data file\n";
    std::cout << "      -o, --output <name>                      Name the output executable\n";
#if defined(_WIN32)
    std::cout << "  lynxer --emit-library <a.lynx> [options] [out.dll]\n";
    std::cout << "                                              Build a .dll exporting the program's `export`s, plus a matching .h\n";
#else
    std::cout << "  lynxer --emit-library <a.lynx> [options] [out.so]\n";
    std::cout << "                                              Build a .so exporting the program's `export`s, plus a matching .h\n";
#endif
    std::cout << "      --include <file>                         Embed a module, native library or data file\n";
#if defined(_WIN32)
    std::cout << "      --runtime <lynxer.dll>                   Use an explicit embedding runtime\n";
#else
    std::cout << "      --runtime <liblynxer.so>                 Use an explicit embedding runtime\n";
#endif
    std::cout << "      --cc <compiler>                          C++ compiler used to build the library\n";
    std::cout << "  lynxer --validate-executeable               Run the interpreter self-check\n";
    std::cout << "  lynxer --gui-ide                            Launch the native GUI IDE (iced)\n";
    std::cout << "  lynxer --cli-ide [file.lynx]                Launch the built-in terminal IDE\n";
    std::cout << "  lynxer --version                            Print version\n";
    std::cout << "  lynxer --list-stdlibs                       List available Lynxer stdlib modules\n";
    std::cout << "  lynxer --install                            Install under "
              << platform::defaultInstallPrefix()
              << " (override with LYNXER_PREFIX)\n";
    std::cout << "  lynxer --uninstall                          Remove the installed interpreter\n";
    std::cout << "\n";
}

void printVersion() {
    const Config& config = Config::instance();
    std::cout << config.format("version.line", "Lynxer {0}", "{0}",
                               config.get("version", "0.1.9b2"))
              << '\n';
}

void printEasterEgg() {
    std::cout << "Easter Egg found!\n";
    std::cout
        << "Wanna do sudo rm -rf / --no-preserve-root? Just kidding, don't do that.\n";
    std::cout << "But seriously, don't do that. It's a bad idea.\n";
    std::cout << "That will erase the entire linux OS and all your files. You will lose everything.\n";
}

std::string extractDocstring(const std::string& path) {
    std::vector<std::string> lines;
    bool inside = false;
    std::ifstream input(path);
    if (!input) {
        std::cout << "Error: could not read '" << path << "'";
        return "";
    }
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t first = line.find_first_not_of(" \t\r");
        const std::string stripped =
            first == std::string::npos ? "" : line.substr(first);
        if (!inside) {
            if (stripped == "////") {
                inside = true;
            }
            continue;
        }
        if (stripped == "////") {
            break;
        }
        while (!line.empty() &&
               (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        lines.push_back(line);
    }
    std::string text;
    for (const auto& entry : lines) {
        if (!text.empty()) {
            text += "\n";
        }
        text += entry;
    }
    while (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }
    return text;
}

int listStdlibs() {
    const std::filesystem::path stdlibPath =
        std::filesystem::path(stdlibDirectory());
    std::vector<std::string> files;
    std::error_code directoryError;
    if (!std::filesystem::is_directory(stdlibPath, directoryError)) {
        std::cout << "No stdlib directory found.\n";
        return 1;
    }
    for (const auto& entry :
         std::filesystem::directory_iterator(stdlibPath, directoryError)) {
        if (directoryError) {
            break;
        }
        const std::filesystem::path path = entry.path();
        const std::string name = path.filename().string();
        if (entry.is_regular_file() && name.size() > 5 &&
            name.compare(name.size() - 5, 5, ".lynx") == 0) {
            files.push_back(name);
        }
    }
    if (directoryError) {
        std::cout << "Could not read stdlib directory: " << directoryError.message()
                  << "\n";
        return 1;
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::cout << "No Lynxer stdlib modules found.\n";
        return 0;
    }
    std::cout << "Available Lynxer stdlib modules:\n\n";
    for (const auto& file : files) {
        const std::string path = (stdlibPath / file).string();
        const std::string name = file.substr(0, file.size() - 5);
        const std::string docstring = extractDocstring(path);
        if (!docstring.empty()) {
            std::cout << "  " << name << "\n";
            std::istringstream doc(docstring);
            std::string docLine;
            while (std::getline(doc, docLine)) {
                if (!docLine.empty() &&
                    !(docLine.size() == 1 && docLine[0] == '\r')) {
                    std::cout << "    " << docLine << "\n";
                } else {
                    std::cout << "\n";
                }
            }
            std::cout << "\n";
        } else {
            std::cout << "  " << name << "\n\n";
        }
    }
    return 0;
}

int lintFile(const std::string& display, const std::string& source) {
    std::unordered_map<std::string, Function> functions;
    int parserWarnings = 0;
    try {
        Lexer lexer(source, display);
        Parser parser(lexer.scan(), display);
        functions = parser.parseProgram();
        parserWarnings = parser.warningCount();
    } catch (const SourceError& error) {
        std::cerr << formatSourceError(display, source, error) << '\n';
        return 1;
    }
    const std::vector<LintDiagnostic> diagnostics = analyzeLint(functions);
    std::size_t errors = 0;
    std::size_t warnings = static_cast<std::size_t>(parserWarnings);
    for (const LintDiagnostic& diagnostic : diagnostics) {
        const char* severity = diagnostic.severity == LintDiagnostic::Severity::Error
                                   ? "error"
                                   : "warning";
        if (diagnostic.severity == LintDiagnostic::Severity::Error) {
            ++errors;
        } else {
            ++warnings;
        }
        std::cerr << display;
        if (diagnostic.line > 0) {
            std::cerr << ':' << diagnostic.line;
            if (diagnostic.column > 0) std::cerr << ':' << diagnostic.column;
        }
        std::cerr << ": " << severity << '['
                  << diagnosticCodeForMessage(diagnostic.message)
                  << "]: " << diagnostic.message << '\n';
    }
    if (errors != 0) {
        return 1;
    }
    std::cout << Config::instance().format("status.lint_ok", "Lint OK: {0}",
                                           "{0}", display)
              << " (" << warnings << " warning(s))\n";
    return 0;
}

// Parses `display` and prints its AST without executing it.
int astFile(const std::string& display, const std::string& source) {
    try {
        // The lexer keeps a reference to the source, so it must outlive it.
        Lexer lexer(source, display);
        Parser parser(lexer.scan(), display);
        const auto functions = parser.parseProgram(
            /*requireEntryPoints=*/true, /*resetTypeRegistry=*/true,
            /*emitDeprecationWarnings=*/false);
        std::vector<const Function*> ordered;
        ordered.reserve(parser.programOrder().size());
        for (const std::string& name : parser.programOrder()) {
            const auto found = functions.find(name);
            if (found != functions.end()) {
                ordered.push_back(&found->second);
            }
        }
        std::cout << "Lynxer AST\n===========\n";
        dumpProgram(std::cout, ordered);
    } catch (const SourceError& error) {
        std::cerr << formatSourceError(display, source, error) << '\n';
        return 1;
    }
    return 0;
}

// Rewrites `display` in place with canonical spacing. `oneline` collapses the
// whole file onto one line, converting `//` comments to the delimited form.
int formatFile(const std::string& display, const std::string& source,
               bool oneline) {
    try {
        const std::string formatted = formatSource(source, display, oneline);
        std::ofstream output(display, std::ios::binary | std::ios::trunc);
        output << formatted;
        if (!output) {
            std::cerr << "lynxer: could not write '" << display << "'\n";
            return 1;
        }
    } catch (const SourceError& error) {
        std::cerr << formatSourceError(display, source, error) << '\n';
        return 1;
    }
    std::cout << Config::instance().format("status.format_ok", "Formatted {0}",
                                           "{0}", display)
              << '\n';
    return 0;
}

// The path of the running executable, resolved through /proc where possible so
// a relative invocation still works.
std::string runningExecutable(const char* argv0) {
    const std::string path = platform::executablePath();
    if (!path.empty()) {
        return path;
    }
    return argv0 != nullptr ? std::string(argv0) : std::string();
}

// The prefix `--install` writes into. `LYNXER_PREFIX` overrides the platform
// default (`/usr`, or a per-user directory on Windows), so the installer can be
// exercised without root (the test suite uses a temp prefix).
std::string installPrefix() {
    const char* environment = std::getenv("LYNXER_PREFIX");
    std::string prefix = (environment != nullptr && environment[0] != '\0')
                             ? environment
                             : platform::defaultInstallPrefix();
    while (prefix.size() > 1 &&
           (prefix.back() == '/' || prefix.back() == '\\')) {
        prefix.pop_back();
    }
    return prefix;
}

// True when `directory` is one of the directories in `PATH`, so `--install`
// only tells the user to update `PATH` when it actually needs it.
bool directoryOnPath(const std::filesystem::path& directory) {
    const char* raw = std::getenv("PATH");
    if (raw == nullptr) {
        return false;
    }
    std::error_code error;
    const std::filesystem::path wanted =
        std::filesystem::weakly_canonical(directory, error);
    const std::string value(raw);
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(platform::pathListSeparator(), start);
        const std::string entry = value.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        if (!entry.empty()) {
            const std::filesystem::path candidate =
                std::filesystem::weakly_canonical(entry, error);
            if (!error && candidate == wanted) {
                return true;
            }
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}

bool sameFile(const std::filesystem::path& left,
              const std::filesystem::path& right) {
    std::error_code error;
    return std::filesystem::equivalent(left, right, error) && !error;
}

// `--install` lays the interpreter out as one self-contained tree so an
// installed binary resolves its stdlib from any working directory:
//
//   $PREFIX/lib/lynxer/lynxer          the real binary
//   $PREFIX/lib/lynxer/stdlib/*        every stdlib module
//   $PREFIX/lib/lynxer/lynxer.config   the configuration file, when present
//   $PREFIX/bin/lynxer                 a symlink to the real binary
//
// `/proc/self/exe` resolves that symlink, so `executableDirectory()` is the
// private lib directory and `<exe>/stdlib` is found by both `--list-stdlibs`
// and module resolution.
// The interpreter's file name on this host: `lynxer` or `lynxer.exe`.
static std::string executableFileName() {
    const std::string extension = platform::executableExtension();
    return extension.empty() ? std::string("lynxer") : "lynxer." + extension;
}

static std::string embeddingRuntimeFileName() {
#if defined(_WIN32)
    return "lynxer.dll";
#else
    return "liblynxer.so";
#endif
}

#if defined(_WIN32)
static std::string embeddingImportFileName() {
    return "lynxer.dll.a";
}
#endif

int installBinary(const char* argv0) {
    const std::string self = runningExecutable(argv0);
    if (self.empty()) {
        std::cerr << "lynxer: could not locate the running executable\n";
        return 1;
    }
    const std::filesystem::path prefix(installPrefix());
    const std::filesystem::path libDir = prefix / "lib" / "lynxer";
    const std::filesystem::path binDir = prefix / "bin";
    const std::filesystem::path installedBinary = libDir / executableFileName();
    const std::filesystem::path linkPath = binDir / executableFileName();
    const std::filesystem::path sourceDirectory(executableDirectory());
    const std::filesystem::path sourceStdlib = sourceDirectory / "stdlib";
    const std::filesystem::path sourceConfig = sourceDirectory / "lynxer.config";

    std::error_code stdlibError;
    if (!std::filesystem::is_directory(sourceStdlib, stdlibError)) {
        std::cerr << "lynxer: install failed: stdlib directory not found at "
                  << sourceStdlib << '\n';
        std::cerr << "lynxer: build Lynxer first so its stdlib/ ships next to "
                     "the interpreter\n";
        return 1;
    }

    try {
        std::filesystem::create_directories(libDir);
        std::filesystem::create_directories(binDir);

        if (!sameFile(self, installedBinary)) {
            std::filesystem::copy_file(
                self, installedBinary,
                std::filesystem::copy_options::overwrite_existing);
        }
        std::filesystem::permissions(
            installedBinary,
            std::filesystem::perms::owner_all |
                std::filesystem::perms::group_read |
                std::filesystem::perms::group_exec |
                std::filesystem::perms::others_read |
                std::filesystem::perms::others_exec,
            std::filesystem::perm_options::replace);

        const std::filesystem::path installedStdlib = libDir / "stdlib";
        if (!sameFile(sourceStdlib, installedStdlib)) {
            std::filesystem::remove_all(installedStdlib);
            std::filesystem::copy(sourceStdlib, installedStdlib,
                                  std::filesystem::copy_options::recursive);
        }

        std::error_code configError;
        if (std::filesystem::is_regular_file(sourceConfig, configError) &&
            !sameFile(sourceConfig, libDir / "lynxer.config")) {
            std::filesystem::copy_file(
                sourceConfig, libDir / "lynxer.config",
                std::filesystem::copy_options::overwrite_existing);
        }

        // The embedding runtime and its headers ship next to the binary so an
        // installed `lynxer --emit-library` can find both. Without them the
        // interpreter still runs; only library emission is unavailable.
        const std::filesystem::path sharedLibrary =
            sourceDirectory / embeddingRuntimeFileName();
        std::error_code sharedError;
        if (std::filesystem::is_regular_file(sharedLibrary, sharedError)) {
            const auto installedRuntime =
                libDir / embeddingRuntimeFileName();
            if (!sameFile(sharedLibrary, installedRuntime)) {
                std::filesystem::copy_file(
                    sharedLibrary, installedRuntime,
                    std::filesystem::copy_options::overwrite_existing);
            }
#if defined(_WIN32)
            const auto sourceImport = sourceDirectory / embeddingImportFileName();
            const auto installedImport = libDir / embeddingImportFileName();
            std::error_code importError;
            if (std::filesystem::is_regular_file(sourceImport, importError) &&
                !sameFile(sourceImport, installedImport)) {
                std::filesystem::copy_file(
                    sourceImport, installedImport,
                    std::filesystem::copy_options::overwrite_existing);
            }
#endif
        } else {
            std::cerr << "lynxer: note: " << embeddingRuntimeFileName()
                      << " not found; install it to "
                         "use --emit-library\n";
        }
        for (const char* header : {"lynxer.h", "ffi_abi.h"}) {
            const std::filesystem::path sourceHeader = sourceDirectory / header;
            std::error_code headerError;
            if (std::filesystem::is_regular_file(sourceHeader, headerError) &&
                !sameFile(sourceHeader, libDir / header)) {
                std::filesystem::copy_file(
                    sourceHeader, libDir / header,
                    std::filesystem::copy_options::overwrite_existing);
            }
        }

        std::string linkError;
        if (!platform::linkOrCopy(installedBinary.string(),
                                  linkPath.string(), linkError)) {
            throw std::filesystem::filesystem_error(linkError,
                                                    std::error_code());
        }
    } catch (const std::filesystem::filesystem_error& error) {
        std::cerr << "lynxer: install failed: " << error.what() << '\n';
        std::cerr << "lynxer: re-run with permission to write " << prefix
                  << " (for example with sudo)\n";
        return 1;
    }

    std::cout << "Installed " << installedBinary.string() << "\n";
    std::cout << "Linked " << linkPath.string() << " -> "
              << installedBinary.string() << "\n";
    if (!directoryOnPath(binDir)) {
        std::cout << "Add " << binDir.string()
                  << " to PATH to run `lynxer` from any directory\n";
    }
    return 0;
}

int uninstallBinary() {
    const std::filesystem::path prefix(installPrefix());
    const std::filesystem::path libDir = prefix / "lib" / "lynxer";
    const std::filesystem::path linkPath =
        prefix / "bin" / executableFileName();

    std::error_code linkError;
    const bool removedLink = std::filesystem::remove(linkPath, linkError);
    if (linkError) {
        std::cerr << "lynxer: could not remove " << linkPath << ": "
                  << linkError.message() << '\n';
        std::cerr << "lynxer: re-run with permission to write " << prefix
                  << " (for example with sudo)\n";
        return 1;
    }
    std::error_code libError;
    const std::uintmax_t removedLib =
        std::filesystem::remove_all(libDir, libError);
    if (libError) {
        std::cerr << "lynxer: could not remove " << libDir << ": "
                  << libError.message() << '\n';
        std::cerr << "lynxer: re-run with permission to write " << prefix
                  << " (for example with sudo)\n";
        return 1;
    }
    if (!removedLink && removedLib == 0) {
        std::cerr << "lynxer: nothing to uninstall under " << prefix << '\n';
        return 1;
    }
    if (removedLink) {
        std::cout << "Removed " << linkPath.string() << '\n';
    }
    if (removedLib > 0) {
        std::cout << "Removed " << libDir.string() << '\n';
    }
    return 0;
}

// A self-check of the interpreter: each case must either run cleanly or raise a
// source-located error containing the expected fragment. It needs no external
// files, so `--validate-executeable` works from an installed binary.
struct ValidationCase {
    const char* name;
    const char* source;
    const char* error;
};

const std::vector<ValidationCase>& validationCases() {
    static const std::vector<ValidationCase> cases = {
        {"arithmetic", "global setup(){}\nglobal main(){ int x = 2 + 3 * 4; assert(x is 14); }", ""},
        {"floor-division", "global setup(){}\nglobal main(){ assert(7 /% 3 is 2); assert(-7 /% 3 is -3); }", ""},
        {"word-operators", "global setup(){}\nglobal main(){ assert(true nand true is false); assert(6 bitand 3 is 2); }", ""},
        {"int-range", "global setup(){}\nglobal main(){ int8 small = 200; }", "out of range"},
        {"unknown-variable", "global setup(){}\nglobal main(){ println(missing); }", "unknown variable 'missing'"},
        {"builtin-as-value", "global setup(){}\nglobal main(){ any f = println; }", "cannot be used as a value"},
        {"division-by-zero", "global setup(){}\nglobal main(){ int z = 1 /% 0; }", "division by zero"},
        {"const-reassign", "global setup(){}\nglobal main(){ const int n = 1; n = 2; }", "constant"},
        {"field-compound", "global setup(){}\nclass C { int v = 1; }\nglobal main(){ C c = new C(); c.v += 2; c.v *= 3; assert(c.v is 9); }", ""},
        {"none-return", "global setup(){}\nglobal log(str m) -> none { }\nglobal main(){ global.log(\"x\"); }", ""},
        {"list-value-semantics", "global setup(){}\nglobal main(){ list a = [1, 2]; list b = listPush(a, 3); assert(returnLength(a) is 2); assert(returnLength(b) is 3); }", ""},
        {"int64-memory", "global setup(){}\nglobal main(){ int p = memoryAllocate(16); memoryWriteInt64(p, 0, 9223372036854775807); assert(memoryReadInt64(p, 0) is 9223372036854775807); memoryFree(p); }", ""},
        {"freed-memory", "global setup(){}\nglobal main(){ int p = memoryAllocate(8); memoryFree(p); memoryReadInt32(p, 0); }", "freed memory"},
        {"invalid-address", "global setup(){}\nglobal main(){ memoryReadInt32(12345, 0); }", "invalid native memory address"},
        {"enum-switch", "global setup(){}\nenum s = [ Ok(int v), Err(str m) ]{}\nglobal main(){ any e = s.Ok(5); int seen = 0; switch(e){ case(s.Ok(v)){ seen = v; } default(){ seen = -1; } } assert(seen is 5); }", ""},
        {"default-parameter", "global setup(){}\nfunc add(int a, int b = 2) -> int { return a + b; }\nglobal main(){ assert(add(1) is 3); }", ""},
        {"fn-nested", "fn outer(){ fn child(){ fn grandchild() -> int { return 42; } } }\nfn main(){ assert(outer.child.grandchild() is 42); }", ""},
        {"missing-main", "fn setup(){}", "program must define fn main()"},
        {"try-catch", "global setup(){}\nglobal main(){ int caught = 0; try { int z = 1 /% 0; } catch (str e) { caught = 1; } assert(caught is 1); }", ""},
        {"typing-guard", "global setup(){}\nglobal main(){ int n = 1.5; }", "cannot be assigned"},
    };
    return cases;
}

int validateInterpreter() {
    int failures = 0;
    for (const ValidationCase& test : validationCases()) {
        try {
            // The lexer keeps a reference to the source, so it must outlive it.
            const std::string source(test.source);
            Lexer lexer(source, test.name);
            Parser parser(lexer.scan(), test.name);
            auto functions = parser.parseProgram(
                /*requireEntryPoints=*/true, /*resetTypeRegistry=*/true,
                /*emitDeprecationWarnings=*/false);
            if (optimizerEnabled()) {
                optimizeProgram(functions, optimizationStats());
            }
            Environment environment;
            executeProgram(functions, environment);
            if (test.error[0] != '\0') {
                std::cerr << "FAIL " << test.name << ": expected an error containing '"
                          << test.error << "'\n";
                ++failures;
            } else {
                std::cout << "ok   " << test.name << '\n';
            }
        } catch (const SourceError& error) {
            const std::string message = error.what();
            if (test.error[0] != '\0' &&
                message.find(test.error) != std::string::npos) {
                std::cout << "ok   " << test.name << '\n';
            } else {
                std::cerr << "FAIL " << test.name << ": unexpected error: " << message
                          << '\n';
                ++failures;
            }
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << test.name << ": unexpected exception: "
                      << error.what() << '\n';
            ++failures;
        }
    }
    std::cout << "validator: " << validationCases().size() << " checks, "
              << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}

// The optimizer report is diagnostic only: it goes to stderr and only when
// LYNXER_OPT_REPORT is set, so no program output ever depends on it.
void reportOptimization() {
    if (std::getenv("LYNXER_OPT_REPORT") == nullptr) {
        return;
    }
    const OptimizationStats& stats = optimizationStats();
    std::cerr << "optimizer: constant folds=" << stats.constantFolds
              << ", short-circuits=" << stats.shortCircuits
              << ", dead branches=" << stats.deadBranches << '\n';
}

int runProgram(const std::string& display, const std::string& source) {
    try {
        Lexer lexer(source, display);
        Parser parser(lexer.scan(), display);
        auto functions = parser.parseProgram(
            /*requireEntryPoints=*/true, /*resetTypeRegistry=*/true);
        if (optimizerEnabled()) {
            optimizeProgram(functions, optimizationStats());
        }
        Environment environment;
        environment.setProgramArguments(programArguments);
        const std::size_t slash = display.find_last_of('/');
        if (slash != std::string::npos) {
            environment.setSourceDirectory(display.substr(0, slash));
        }
        executeProgram(functions, environment);
        reportOptimization();
        return 0;
    } catch (const ExitControl& control) {
        clearExitRequest(control.code);
        return control.code;
    } catch (const InterruptError&) {
        return 130;
    } catch (const SourceError& error) {
        std::cerr << formatSourceError(display, source, error) << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << Config::instance().format(
                         "error.interpreter_failure",
                         "lynxer: interpreter failure in '{0}': {1}", "{0}",
                         display)
                  << ": " << error.what() << '\n';
        return 1;
    }
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() > suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
               0;
}

// Resolves a file named directly on the command line: the path as written when
// it exists, otherwise the same search `import` uses.
std::string resolveExplicitPath(const std::string& given) {
    std::error_code error;
    if (std::filesystem::is_regular_file(given, error)) {
        return given;
    }
    return resolveModulePath("", given);
}

std::string baseName(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

// Recursively collects `mainSource` and every module it imports into `archive`,
// then embeds each file named explicitly on the command line. Source modules are
// embedded as text; native modules are embedded as library bytes so the compiled
// executable does not need the build tree at run time.
bool collectArchive(const std::string& mainPath, const std::string& mainSource,
                    const std::vector<std::string>& extraInputs,
                    ProgramArchive& archive, std::string& error,
                    bool requireEntryPoints = true) {
    std::map<std::string, bool> collectedSources;
    std::map<std::string, bool> collectedLibraries;
    std::map<std::string, bool> collectedAssets;

    struct Pending {
        std::string path;
        std::string source;
        bool requireEntryPoints = false;
    };
    std::vector<Pending> queue;
    queue.push_back(Pending{mainPath, mainSource, requireEntryPoints});
    collectedSources[mainPath] = true;

    // Explicit inputs are embedded first so they take part in the same
    // de-duplication as imported modules, so a source input has its own imports
    // walked below, and so `import` can resolve them by name even when they live
    // outside the normal module search path. Inputs that are neither `.lynx` nor
    // `.so` are embedded as data files.
    std::map<std::string, bool> extraKeys;
    const auto recordExtraKeys = [&extraKeys](const std::string& given) {
        const std::string bare = baseName(given);
        extraKeys[given] = true;
        extraKeys[bare] = true;
        if (endsWith(bare, ".lynx")) {
            extraKeys[bare.substr(0, bare.size() - 5)] = true;
        }
    };

    for (const auto& given : extraInputs) {
        const bool native = isNativeLibraryPath(given);
        const bool sourceModule = endsWith(given, ".lynx");
        const std::string resolved = resolveExplicitPath(given);
        if (resolved.empty()) {
            error = "input file '" + given + "' was not found";
            return false;
        }
        if (native || sourceModule) {
            recordExtraKeys(given);
            recordExtraKeys(resolved);
        }
        if (native) {
            if (collectedLibraries.count(resolved) != 0) {
                continue;
            }
            std::ifstream input(resolved, std::ios::binary);
            if (!input) {
                error = "cannot read native module '" + resolved + "'";
                return false;
            }
            ArchiveModule module;
            module.name = given;
            module.library.assign(std::istreambuf_iterator<char>(input),
                                  std::istreambuf_iterator<char>());
            collectedLibraries[resolved] = true;
            archive.modules.push_back(std::move(module));
            continue;
        }
        if (!sourceModule) {
            if (collectedAssets.count(resolved) != 0) {
                continue;
            }
            std::ifstream input(resolved, std::ios::binary);
            if (!input) {
                error = "cannot read included file '" + resolved + "'";
                return false;
            }
            ArchiveModule module;
            module.name = given;
            module.asset.assign(std::istreambuf_iterator<char>(input),
                                std::istreambuf_iterator<char>());
            collectedAssets[resolved] = true;
            archive.modules.push_back(std::move(module));
            continue;
        }
        if (collectedSources.count(resolved) != 0) {
            continue;
        }
        std::ifstream input(resolved);
        if (!input) {
            error = "cannot read module '" + resolved + "'";
            return false;
        }
        std::ostringstream content;
        content << input.rdbuf();
        ArchiveModule module;
        module.name = given;
        module.source = content.str();
        collectedSources[resolved] = true;
        queue.push_back(Pending{resolved, module.source, false});
        archive.modules.push_back(std::move(module));
    }

    while (!queue.empty()) {
        const Pending current = queue.back();
        queue.pop_back();
        std::string directory;
        const std::filesystem::path currentPath(current.path);
        if (currentPath.has_parent_path()) {
            directory = currentPath.parent_path().string();
        }

        std::vector<ImportRecord> imports;
        try {
            imports = collectImports(current.source, current.path,
                                     current.requireEntryPoints,
                                     /*emitDeprecationWarnings=*/false);
        } catch (const SourceError& importError) {
            std::cerr << "lynxer: " << current.path << ':' << importError.line
                      << ':' << importError.column << ": [" << importError.code
                      << "] " << importError.what() << '\n';
            return false;
        }

        for (const auto& import : imports) {
            const bool native = isNativeLibraryPath(import.path);
            // An explicitly supplied input already satisfies the import.
            if (extraKeys.count(import.path) != 0) {
                continue;
            }
            const std::string resolved =
                resolveModulePath(directory, import.path);
            if (resolved.empty()) {
                error = "module '" + import.path + "' was not found";
                return false;
            }
            if (native) {
                if (collectedLibraries.count(resolved) != 0) {
                    continue;
                }
                std::ifstream input(resolved, std::ios::binary);
                if (!input) {
                    error = "cannot read native module '" + resolved + "'";
                    return false;
                }
                ArchiveModule module;
                module.name = import.path;
                module.library.assign(
                    std::istreambuf_iterator<char>(input),
                    std::istreambuf_iterator<char>());
                collectedLibraries[resolved] = true;
                archive.modules.push_back(std::move(module));
                continue;
            }
            if (collectedSources.count(resolved) != 0) {
                continue;
            }
            std::ifstream input(resolved);
            if (!input) {
                error = "cannot read module '" + resolved + "'";
                return false;
            }
            std::ostringstream content;
            content << input.rdbuf();
            ArchiveModule module;
            module.name = import.path;
            module.source = content.str();
            collectedSources[resolved] = true;
            queue.push_back(Pending{resolved, module.source, false});
            archive.modules.push_back(std::move(module));
        }
    }
    return true;
}

int failWith(const char* key, const char* fallback, const std::string& value) {
    const std::string message =
        Config::instance().format(key, fallback, "{0}", value);
    std::cerr << '[' << diagnosticCodeForMessage(message) << "] " << message
              << '\n';
    return 1;
}

int removedFlag(const std::string& flag, const std::string& replacement) {
    std::cerr << "lynxer: '" << flag
              << "' was removed with the bytecode backend";
    if (!replacement.empty()) {
        std::cerr << "; use " << replacement << " instead";
    }
    std::cerr << '\n';
    return 1;
}

// Builds a standalone ELF executable carrying one or more input files and every
// module they import.
int compileProgramToExecutable(const std::vector<std::string>& arguments) {
    std::vector<std::string> inputs;
    std::string outputName;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string& argument = arguments[index];
        if (argument == "-o" || argument == "--output" ||
            argument == "-name" || argument == "--name") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "lynxer: " << argument << " requires a name\n";
                return 1;
            }
            if (!outputName.empty()) {
                std::cerr << "lynxer: the output name was given twice\n";
                return 1;
            }
            outputName = arguments[++index];
            continue;
        }
        // --include takes any file: a .lynx module, a native .so, or a data
        // file that the program reads with bundledFile().
        if (argument == "--include" || argument == "-i") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "lynxer: " << argument << " requires a file\n";
                return 1;
            }
            inputs.push_back(arguments[++index]);
            continue;
        }
        // Anything that looks like a source or library file is an input; a
        // bare name is the output.
        if (endsWith(argument, ".lynx") || isNativeLibraryPath(argument)) {
            inputs.push_back(argument);
            continue;
        }
        if (!outputName.empty()) {
            std::cerr << "lynxer: unexpected extra argument '" << argument
                      << "'\n";
            return 1;
        }
        outputName = argument;
    }

    if (inputs.empty()) {
        std::cerr << Config::instance().get(
                         "error.compile_usage",
                         "lynxer: --compile requires a .lynx file, optionally "
                         "followed by --include <file> inputs and an output "
                         "name")
                  << '\n';
        return 1;
    }
    if (!endsWith(inputs.front(), ".lynx")) {
        std::cerr << "lynxer: the first input must be a .lynx program file\n";
        return 1;
    }

    const std::string& file = inputs.front();
    bool ok = false;
    const std::string source = readFile(file, file, ok);
    if (!ok) {
        return 1;
    }

    ProgramArchive archive;
    archive.mainPath = file;
    archive.mainSource = source;
    const std::vector<std::string> extras(inputs.begin() + 1, inputs.end());
    std::string error;
    if (!collectArchive(file, source, extras, archive, error)) {
        // An empty message means the failure was already reported with a source
        // location.
        if (error.empty()) {
            return 1;
        }
        return failWith("error.compile_failed", "lynxer: compile failed: {0}",
                        error);
    }

    std::string outputPath = outputName;
    if (outputPath.empty()) {
        outputPath = baseName(file);
        if (endsWith(outputPath, ".lynx")) {
            outputPath.resize(outputPath.size() - 5);
        }
    }

    if (!writeBundledExecutable(outputPath, makeBundlePayload(archive),
                                error)) {
        return failWith("error.compile_failed", "lynxer: compile failed: {0}",
                        error);
    }
    std::cout << Config::instance().format("status.compile_ok",
                                           "Compiled: {0}", "{0}", outputPath)
              << '\n';
    return 0;
}

// ---------------------------------------------------------------------------
// Projects
//
// A Lynxer project is a directory with a `lynxer.config` describing its entry
// point. `--init` scaffolds it, `--build` compiles the entry into `build/`, and
// `--run` builds and executes the result. Project config is parsed directly
// (not via Config::instance()) because that singleton intentionally prefers
// the interpreter's own lynxer.config shipped beside the executable.
// ---------------------------------------------------------------------------

std::string projectTrim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool readProjectConfig(const std::string& path,
                       std::unordered_map<std::string, std::string>& values,
                       std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "no " + path +
                " in the current directory (run 'lynxer --init' first)";
        return false;
    }
    std::string line;
    while (std::getline(input, line)) {
        const std::string stripped = projectTrim(line);
        if (stripped.empty() || stripped[0] == '#') {
            continue;
        }
        const std::size_t equals = stripped.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        values[projectTrim(stripped.substr(0, equals))] =
            projectTrim(stripped.substr(equals + 1));
    }
    return true;
}

bool writeProjectFile(const std::string& path, const std::string& content,
                      std::string& error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot write '" + path + "'";
        return false;
    }
    output << content;
    output.close();
    if (!output) {
        error = "cannot write '" + path + "'";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// GUI IDE
//
// `--gui-ide` is a launcher for the native Rust IDE in `lynxer/ide`. It is
// listed in --help together with `--cli-ide`. The IDE is a separate
// executable, so this command resolves it, tells it which interpreter to use
// for Run/Lint/Format, and waits for it to exit.
// ---------------------------------------------------------------------------

std::string ideFileName() {
    const std::string extension = platform::executableExtension();
    return extension.empty() ? std::string("lynxer-ide")
                             : "lynxer-ide." + extension;
}

bool isIdeFile(const std::string& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

// An explicit LYNXER_IDE always wins and is authoritative: a bad override is
// an error rather than a silent fallback. Otherwise look beside the running
// interpreter (installed layout), under <prefix>/lib/lynxer (Windows install,
// where the launcher is a copy), and finally at the in-tree cargo builds.
std::string findIdeBinary() {
    if (const char* overridePath = std::getenv("LYNXER_IDE");
        overridePath != nullptr && *overridePath != '\0') {
        std::filesystem::path candidate(overridePath);
        std::error_code error;
        if (std::filesystem::is_directory(candidate, error)) {
            candidate /= ideFileName();
        }
        if (!isIdeFile(candidate.string())) {
            return std::string();
        }
        return std::filesystem::absolute(candidate, error).string();
    }

    const std::filesystem::path beside(executableDirectory());
    const std::vector<std::filesystem::path> candidates = {
        beside / ideFileName(),
        beside / ".." / "lib" / "lynxer" / ideFileName(),
        beside / "ide" / "target" / "release" / ideFileName(),
        beside / "ide" / "target" / "debug" / ideFileName(),
    };
    for (const auto& candidate : candidates) {
        if (isIdeFile(candidate.string())) {
            std::error_code error;
            return std::filesystem::absolute(candidate, error)
                .lexically_normal()
                .string();
        }
    }
    return std::string();
}

int launchGuiIde(const char* argv0,
                 const std::vector<std::string>& extraArguments) {
    if (!extraArguments.empty()) {
        std::cerr << "lynxer: --gui-ide takes no arguments\n";
        return 1;
    }

    const std::string ide = findIdeBinary();
    if (ide.empty()) {
        if (const char* overridePath = std::getenv("LYNXER_IDE");
            overridePath != nullptr && *overridePath != '\0') {
            std::cerr << "lynxer: LYNXER_IDE does not point to an IDE "
                         "executable: "
                      << overridePath << '\n';
        } else {
            std::cerr << "lynxer: the Lynxer IDE executable ("
                      << ideFileName()
                      << ") was not found beside the interpreter or in "
                         "lynxer/ide/target/{release,debug}\n";
            std::cerr << "lynxer: build it with 'make -C lynxer/ide build', "
                         "or set LYNXER_IDE to its path\n";
        }
        return 1;
    }

    // Hand the IDE this exact interpreter. Without it the IDE would look for a
    // bare `lynxer` on PATH, which fails in a development checkout.
    std::string interpreter = runningExecutable(argv0);
    if (interpreter.empty()) {
        std::cerr << "lynxer: could not locate the running executable\n";
        return 1;
    }
    std::error_code pathError;
    const std::filesystem::path absolute =
        std::filesystem::absolute(interpreter, pathError);
    if (!pathError) {
        interpreter = absolute.string();
    }

    const int status =
        platform::spawnProcess({ide, "--lynxer", interpreter});
    if (status < 0) {
        std::cerr << "lynxer: could not start the IDE at '" << ide << "'\n";
        return 1;
    }
    return status;
}

// ---------------------------------------------------------------------------
// Terminal IDE
//
// `--cli-ide` runs a full-screen IDE inside the interpreter itself: no webview,
// no second binary, no GUI toolkit. It uses ANSI escape sequences for the frame
// and the portable terminal primitives in platform.hpp for raw input. All
// Run / Lint / Format / project commands shell out to this same interpreter via
// platform::captureProcess, so their output is shown in the output panel
// instead of trampling the screen.
// ---------------------------------------------------------------------------

constexpr int IC_TEXT = 0, IC_KEYWORD = 1, IC_TYPE = 2, IC_BUILTIN = 3,
              IC_CONSTANT = 4, IC_STRING = 5, IC_NUMBER = 6, IC_COMMENT = 7,
              IC_DOC = 8, IC_ATTR = 9, IC_OP = 10, IC_BRACKET = 11,
              IC_DELIM = 12, IC_ACCENT = 13, IC_MUTED = 14, IC_ERROR = 15,
              IC_WARN = 16, IC_INFO = 17, IC_SURFACE = 18, IC_PANEL = 19,
              IC_EDITOR = 20, IC_STATUS = 21, IC_GUTTER = 22,
              IC_OK = 23;

struct IdeRgb {
    int r, g, b;
};

const IdeRgb kIdePalette[] = {
    {0xd8, 0xdc, 0xe2},  // text
    {0x56, 0x9c, 0xd6},  // keyword
    {0x4e, 0xc9, 0xb0},  // type
    {0xdc, 0xdc, 0xaa},  // builtin / function
    {0x56, 0x9c, 0xd6},  // constant
    {0xce, 0x91, 0x78},  // string
    {0xb5, 0xce, 0xa8},  // number
    {0x6a, 0x99, 0x55},  // comment
    {0x60, 0x8b, 0x4e},  // doc comment
    {0xc5, 0x86, 0xc0},  // attribute
    {0xd4, 0xd4, 0xd4},  // operator
    {0xff, 0xd7, 0x00},  // bracket
    {0xd4, 0xd4, 0xd4},  // delimiter
    {0x4e, 0xc9, 0xb0},  // accent
    {0x89, 0x92, 0x9e},  // muted
    {0xf1, 0x4c, 0x4c},  // error
    {0xcc, 0xa7, 0x00},  // warning
    {0x37, 0x94, 0xff},  // info
    {0x1e, 0x20, 0x25},  // surface
    {0x24, 0x27, 0x2d},  // panel
    {0x18, 0x1a, 0x1e},  // editor
    {0x0f, 0x45, 0x4f},  // status
    {0x5a, 0x60, 0x6a},  // gutter
    {0x4e, 0xc9, 0xb0},  // ok
};

std::string ideFg(int index) {
    const IdeRgb& rgb = kIdePalette[index];
    std::ostringstream out;
    out << "\x1b[38;2;" << rgb.r << ';' << rgb.g << ';' << rgb.b << 'm';
    return out.str();
}

std::string ideBg(int index) {
    const IdeRgb& rgb = kIdePalette[index];
    std::ostringstream out;
    out << "\x1b[48;2;" << rgb.r << ';' << rgb.g << ';' << rgb.b << 'm';
    return out.str();
}

const std::string kIdeReset = "\x1b[0m";
const std::string kIdeBold = "\x1b[1m";
const std::string kIdeDim = "\x1b[2m";
const std::string kIdeReverse = "\x1b[7m";

int ideUtf8Length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

// Returns the index just past the escape sequence beginning at `start`.
// A CSI sequence is ESC '[', parameter/intermediate bytes (0x20-0x3F) and
// one final byte (0x40-0x7E) -- notably '[' itself is NOT the final byte, so
// scanning from ESC+1 would stop immediately and treat colour parameters as
// visible text.
std::size_t ideEscapeEnd(const std::string& text, std::size_t start) {
    std::size_t end = start + 1;
    if (end < text.size() && text[end] == '[') {
        ++end;
        while (end < text.size() &&
               static_cast<unsigned char>(text[end]) >= 0x20 &&
               static_cast<unsigned char>(text[end]) <= 0x3F) {
            ++end;
        }
    }
    if (end < text.size()) ++end;
    return end;
}

std::size_t ideVisibleWidth(const std::string& text) {
    std::size_t width = 0;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        if (lead == 0x1b) {
            i = ideEscapeEnd(text, i);
            continue;
        }
        const int length = ideUtf8Length(lead);
        i += (i + static_cast<std::size_t>(length) <= text.size())
                 ? static_cast<std::size_t>(length)
                 : 1;
        ++width;
    }
    return width;
}

// Keeps a row within `width` visible columns without ever cutting an ANSI
// escape sequence in half.
std::string ideClamp(const std::string& text, std::size_t width) {
    std::string out;
    std::size_t used = 0;
    for (std::size_t i = 0; i < text.size() && used < width;) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        if (lead == 0x1b) {
            const std::size_t end = ideEscapeEnd(text, i);
            out.append(text, i, end - i);
            i = end;
            continue;
        }
        const int length = ideUtf8Length(lead);
        const std::size_t take =
            (i + static_cast<std::size_t>(length) <= text.size())
                ? static_cast<std::size_t>(length)
                : 1;
        out.append(text, i, take);
        i += take;
        ++used;
    }
    return out;
}

std::string idePad(const std::string& text, std::size_t width) {
    const std::size_t current = ideVisibleWidth(text);
    if (current >= width) return text;
    return text + std::string(width - current, ' ');
}

void ideSetRow(std::vector<std::string>& rows, int index,
               const std::string& content, int width, int background) {
    if (index < 0 || index >= static_cast<int>(rows.size())) return;
    rows[index] = ideBg(background) +
                  idePad(ideClamp(content, static_cast<std::size_t>(width)),
                         static_cast<std::size_t>(width)) +
                  kIdeReset;
}

const std::unordered_set<std::string>& ideKeywords() {
    static const std::unordered_set<std::string> words = {
        "fn", "global", "func", "local", "lambda", "template", "build",
        "named", "extending", "class", "extends", "struct", "enum", "setup",
        "main", "return", "if", "elif", "else", "while", "for", "doWhile",
        "iterate", "forever", "switch", "case", "default", "try", "catch",
        "raise", "break", "continue", "restart", "new", "this", "super",
        "import", "importAs", "export", "const", "shared", "type", "of",
        "await", "exec", "codeblock", "inter", "true", "false", "none",
        "and", "or", "not", "xor", "is", "isnt",
    };
    return words;
}

const std::unordered_set<std::string>& ideTypes() {
    static const std::unordered_set<std::string> words = {
        "any", "int", "float", "num", "char", "str", "bool", "numBool",
        "bit", "void", "array", "map", "set", "tuple", "ptr", "ref",
    };
    return words;
}

const std::unordered_set<std::string>& ideBuiltins() {
    static const std::unordered_set<std::string> words = {
    "allOf",
    "anyOf",
    "assert",
    "asyncGather",
    "asyncPollClose",
    "asyncPollCreate",
    "asyncPollDispatch",
    "asyncPollModify",
    "asyncPollRegister",
    "asyncPollRemove",
    "asyncPollWait",
    "asyncRun",
    "asyncSleep",
    "asyncTimerCancel",
    "asyncTimerCreate",
    "asyncWakeupClose",
    "asyncWakeupCreate",
    "asyncWakeupSignal",
    "atomicAdd",
    "atomicLoad",
    "atomicStore",
    "bundledFile",
    "bundledFiles",
    "byte",
    "bytesAt",
    "bytesFromHex",
    "bytesLength",
    "bytesOf",
    "bytesToHex",
    "bytesToList",
    "bytesToStr",
    "charAt",
    "charCode",
    "charOf",
    "contains",
    "exceptionCode",
    "exceptionInfo",
    "ffiCall",
    "ffiCallback",
    "ffiCloseLibrary",
    "ffiFreeCallback",
    "ffiLoadLibrary",
    "ffiLookup",
    "filesystemChmod",
    "filesystemClose",
    "filesystemLink",
    "filesystemList",
    "filesystemMkdir",
    "filesystemOpen",
    "filesystemRead",
    "filesystemReadLink",
    "filesystemRemove",
    "filesystemRename",
    "filesystemStat",
    "filesystemWrite",
    "float32",
    "float64",
    "floatOf",
    "foreverDelay",
    "functionAddress",
    "getAddress",
    "getAddressValue",
    "input",
    "inputln",
    "int16",
    "int32",
    "int64",
    "int8",
    "intOf",
    "listAvg",
    "listClear",
    "listContains",
    "listCount",
    "listExtend",
    "listFirst",
    "listFlatten",
    "listGet",
    "listHead",
    "listIndex",
    "listInsert",
    "listJoin",
    "listJsonArray",
    "listJsonObject",
    "listLast",
    "listMax",
    "listMin",
    "listPop",
    "listPush",
    "listRemove",
    "listRepeat",
    "listSet",
    "listSlice",
    "listTail",
    "listToBytes",
    "listToTuple",
    "listUnique",
    "listZip",
    "lower",
    "memoryAllocate",
    "memoryAllocateZeroed",
    "memoryArrayAllocate",
    "memoryArrayGet",
    "memoryArrayLength",
    "memoryArraySet",
    "memoryArrayView",
    "memoryBlockAllocate",
    "memoryBlockGet",
    "memoryBlockLength",
    "memoryBlockSet",
    "memoryBlockView",
    "memoryCopy",
    "memoryFree",
    "memoryProtect",
    "memoryReadByte",
    "memoryReadEndian",
    "memoryReadFloat32",
    "memoryReadFloat64",
    "memoryReadInt16",
    "memoryReadInt32",
    "memoryReadInt64",
    "memoryReadInt8",
    "memoryReadUInt16",
    "memoryReadUInt32",
    "memoryReadUInt64",
    "memoryReadUInt8",
    "memoryReallocate",
    "memorySet",
    "memoryStructAlignment",
    "memoryStructAllocate",
    "memoryStructFieldCount",
    "memoryStructFieldOffset",
    "memoryStructFieldSize",
    "memoryStructFieldType",
    "memoryStructGet",
    "memoryStructSet",
    "memoryStructSize",
    "memoryTypeAlignment",
    "memoryTypeSize",
    "memoryViewGet",
    "memoryViewLength",
    "memoryViewSet",
    "memoryWriteByte",
    "memoryWriteEndian",
    "memoryWriteFloat32",
    "memoryWriteFloat64",
    "memoryWriteInt16",
    "memoryWriteInt32",
    "memoryWriteInt64",
    "memoryWriteInt8",
    "memoryWriteUInt16",
    "memoryWriteUInt32",
    "memoryWriteUInt64",
    "memoryWriteUInt8",
    "modifyAddressValue",
    "nativeCall",
    "nativeConditionClose",
    "nativeConditionCreate",
    "nativeConditionNotify",
    "nativeConditionNotifyAll",
    "nativeConditionWait",
    "nativeFunctionAddress",
    "nativeHandleAddress",
    "nativeHandleAllocate",
    "nativeHandleFree",
    "nativeHandleIsAlive",
    "nativeModuleClose",
    "nativeModuleConstant",
    "nativeModuleDependencies",
    "nativeModuleError",
    "nativeModuleFunction",
    "nativeModuleLoad",
    "nativeModuleName",
    "nativeModuleType",
    "nativeMutexClose",
    "nativeMutexCreate",
    "nativeMutexLock",
    "nativeMutexTryLock",
    "nativeMutexUnlock",
    "nativeSemaphoreClose",
    "nativeSemaphoreCreate",
    "nativeSemaphorePost",
    "nativeSemaphoreTryWait",
    "nativeSemaphoreWait",
    "nativeStructAlignment",
    "nativeStructAllocate",
    "nativeStructFieldCount",
    "nativeStructFieldOffset",
    "nativeStructFieldSize",
    "nativeStructFieldType",
    "nativeStructGet",
    "nativeStructSet",
    "nativeStructSize",
    "nativeThreadDetach",
    "nativeThreadIsAlive",
    "nativeThreadJoin",
    "nativeThreadJoinAll",
    "nativeThreadStart",
    "nativeThreadStatus",
    "nativeThreadYield",
    "nativeTypeAlignment",
    "networkingAccept",
    "networkingAddress",
    "networkingBind",
    "networkingBlocking",
    "networkingClose",
    "networkingConnect",
    "networkingListen",
    "networkingOpen",
    "networkingOption",
    "networkingReceive",
    "networkingResolve",
    "networkingSend",
    "networkingShutdown",
    "object",
    "overrideMain",
    "print",
    "printf",
    "println",
    "printlnf",
    "processClose",
    "processCloseInput",
    "processPoll",
    "processRead",
    "processSendSignal",
    "processSpawn",
    "processWait",
    "processWrite",
    "raise",
    "range",
    "replace",
    "returnLength",
    "returnType",
    "reverseList",
    "sentinel",
    "seqFromTo",
    "sizeOf",
    "sleep",
    "sortList",
    "soundIsPlaying",
    "soundLoad",
    "soundLoop",
    "soundPause",
    "soundPlay",
    "soundRelease",
    "soundResume",
    "soundSetVolume",
    "soundStop",
    "splitStr",
    "strOf",
    "substring",
    "sumOf",
    "suppressDeprecationWarning",
    "suppressForeverWarning",
    "trim",
    "tupleAll",
    "tupleAny",
    "tupleConcat",
    "tupleContains",
    "tupleCount",
    "tupleCreate",
    "tupleFirst",
    "tupleFlatten",
    "tupleGet",
    "tupleIndex",
    "tupleJoin",
    "tupleJsonArray",
    "tupleLast",
    "tupleLen",
    "tupleMax",
    "tupleMean",
    "tupleMin",
    "tupleReverse",
    "tupleSlice",
    "tupleSort",
    "tupleSortDesc",
    "tupleSum",
    "tupleToList",
    "tupleUnique",
    "tupleZip",
    "uint16",
    "uint32",
    "uint64",
    "uint8",
    "upper",
    "volatileRead",
    "volatileWrite"
    };
    return words;
}

bool ideIsIdentChar(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

// One palette index per byte, so the renderer can invert the cursor byte
// without disturbing the surrounding colours.
std::vector<int> ideTokenColors(const std::string& line) {
    std::vector<int> colors(line.size(), IC_TEXT);
    auto paint = [&](std::size_t from, std::size_t to, int color) {
        for (std::size_t i = from; i < to && i < line.size(); ++i) {
            colors[i] = color;
        }
    };
    const std::size_t size = line.size();
    std::size_t i = 0;
    while (i < size) {
        const unsigned char c = static_cast<unsigned char>(line[i]);
        if ((c == '#' && i + 1 < size && line[i + 1] == '#') ||
            (c == '/' && i + 2 < size && line[i + 1] == '/' &&
             line[i + 2] == '/')) {
            paint(i, size, IC_DOC);
            break;
        }
        if (c == '#' || (c == '/' && i + 1 < size && line[i + 1] == '/')) {
            paint(i, size, IC_COMMENT);
            break;
        }
        if (c == '/' && i + 1 < size && line[i + 1] == '*') {
            std::size_t end = i + 2;
            while (end + 1 < size &&
                   !(line[end] == '*' && line[end + 1] == '/')) {
                ++end;
            }
            end = std::min(end + 2, size);
            paint(i, end, IC_COMMENT);
            i = end;
            continue;
        }
        if (c == '"' || c == '\'') {
            const char quote = static_cast<char>(c);
            const std::size_t start = i++;
            while (i < size) {
                if (line[i] == '\\') {
                    i = std::min(i + 2, size);
                    continue;
                }
                if (line[i] == quote) {
                    ++i;
                    break;
                }
                ++i;
            }
            paint(start, i, IC_STRING);
            continue;
        }
        if (c == '@' && i + 1 < size &&
            (std::isalpha(static_cast<unsigned char>(line[i + 1])) != 0 ||
             line[i + 1] == '_')) {
            const std::size_t start = i++;
            while (i < size && ideIsIdentChar(static_cast<unsigned char>(line[i]))) ++i;
            paint(start, i, IC_ATTR);
            continue;
        }
        if (c >= '0' && c <= '9') {
            const std::size_t start = i;
            if (c == '0' && i + 1 < size &&
                (line[i + 1] == 'x' || line[i + 1] == 'X' ||
                 line[i + 1] == 'b' || line[i + 1] == 'B' ||
                 line[i + 1] == 'o' || line[i + 1] == 'O')) {
                i += 2;
                while (i < size && ideIsIdentChar(static_cast<unsigned char>(line[i]))) ++i;
            } else {
                while (i < size &&
                       (std::isdigit(static_cast<unsigned char>(line[i])) != 0 ||
                        line[i] == '_')) ++i;
                if (i + 1 < size && line[i] == '.' &&
                    std::isdigit(static_cast<unsigned char>(line[i + 1])) != 0) {
                    ++i;
                    while (i < size &&
                           (std::isdigit(static_cast<unsigned char>(line[i])) != 0 ||
                            line[i] == '_')) ++i;
                }
                if (i < size && (line[i] == 'e' || line[i] == 'E')) {
                    std::size_t j = i + 1;
                    if (j < size && (line[j] == '+' || line[j] == '-')) ++j;
                    if (j < size && std::isdigit(static_cast<unsigned char>(line[j])) != 0) {
                        i = j + 1;
                        while (i < size && std::isdigit(static_cast<unsigned char>(line[i])) != 0) ++i;
                    }
                }
            }
            paint(start, i, IC_NUMBER);
            continue;
        }
        if (std::isalpha(c) != 0 || c == '_') {
            const std::size_t start = i;
            while (i < size && ideIsIdentChar(static_cast<unsigned char>(line[i]))) ++i;
            const std::string word = line.substr(start, i - start);
            int color = IC_TEXT;
            if (ideKeywords().count(word) != 0) {
                color = (word == "true" || word == "false" || word == "none")
                            ? IC_CONSTANT
                            : IC_KEYWORD;
            } else if (ideTypes().count(word) != 0) {
                color = IC_TYPE;
            } else if (ideBuiltins().count(word) != 0) {
                color = IC_BUILTIN;
            } else {
                std::size_t j = i;
                while (j < size && line[j] == ' ') ++j;
                if (j < size && line[j] == '(') color = IC_BUILTIN;
            }
            paint(start, i, color);
            continue;
        }
        std::size_t length = 1;
        if (i + 1 < size) {
            const char a = line[i];
            const char b = line[i + 1];
            if ((a == '-' && b == '>') || (a == '=' && b == '>') ||
                (a == '=' && b == '=') || (a == '!' && b == '=') ||
                (a == '<' && b == '=') || (a == '>' && b == '=') ||
                (a == '&' && b == '&') || (a == '|' && b == '|') ||
                (a == ':' && b == ':') || (a == '+' && b == '+') ||
                (a == '-' && b == '-') || (a == '.' && b == '.')) {
                length = 2;
            }
        }
        int color = IC_TEXT;
        if (c == '{' || c == '}' || c == '(' || c == ')' || c == '[' ||
            c == ']') {
            color = IC_BRACKET;
        } else if (c == ';' || c == ',' || c == '.') {
            color = IC_DELIM;
        } else if (std::string("+-*/%=&|<>!^~:?").find(static_cast<char>(c)) !=
                   std::string::npos) {
            color = IC_OP;
        }
        paint(i, i + length, color);
        i += length;
    }
    return colors;
}

std::string ideRenderLine(const std::string& line, bool lynx,
                          std::size_t cursorColumn, bool current) {
    std::string display;
    for (const char c : line) display += (c == '\t') ? ' ' : c;
    const std::vector<int> colors =
        lynx ? ideTokenColors(display) : std::vector<int>(display.size(), IC_TEXT);
    std::string out;
    int active = -1;
    for (std::size_t i = 0; i < display.size();) {
        const int length = ideUtf8Length(static_cast<unsigned char>(display[i]));
        const std::size_t take =
            (i + static_cast<std::size_t>(length) <= display.size())
                ? static_cast<std::size_t>(length)
                : 1;
        const int color = colors[i];
        const bool caret = current && i == cursorColumn;
        if (color != active || caret) {
            out += ideFg(color);
            active = color;
        }
        if (caret) out += kIdeReverse;
        out.append(display, i, take);
        if (caret) out += "\x1b[27m" + ideFg(color);
        i += take;
    }
    if (current && cursorColumn >= display.size()) {
        out += ideFg(IC_TEXT) + std::string(kIdeReverse) + " " + "\x1b[27m";
    }
    return out;
}

struct IdeBuffer {
    std::string path;
    std::vector<std::string> lines;
    std::string saved;
    std::size_t cursorLine = 0;
    std::size_t cursorColumn = 0;
    std::size_t topLine = 0;
};

struct IdeTreeRow {
    std::string name;
    std::string path;
    bool isDir = false;
    int depth = 0;
    bool expanded = false;
    bool nonText = false;
};

struct IdeEntry {
    std::string name;
    std::string path;
    bool isDir = false;
    bool nonText = false;
};

struct IdeDiagnostic {
    std::string file;
    int line = 0;
    int column = 0;
    std::string severity;
    std::string code;
    std::string message;
};

enum class IdePrompt { None, Open, New, Folder, Goto, Find, SaveAll };

struct IdeState {
    std::string root;
    std::string interpreter;
    std::string version;
    std::vector<IdeTreeRow> tree;
    std::size_t treeSelection = 0;
    std::size_t treeTop = 0;
    bool explorerFocus = false;
    std::vector<IdeBuffer> buffers;
    std::size_t active = 0;
    std::string output;
    std::vector<IdeDiagnostic> diagnostics;
    std::size_t diagnosticIndex = 0;
    std::string status;
    std::string findQuery;
    bool showOutput = false;  // nano-like: no empty panel until a command runs
    bool showHelp = false;
    std::size_t helpTop = 0;
    bool showCommands = false;
    std::size_t commandIndex = 0;
    std::size_t commandTop = 0;
    IdePrompt prompt = IdePrompt::None;
    std::string promptLabel;
    std::string promptValue;
    // Set when the save-all prompt was raised by a toggle-close, a folder
    // collapse or by quit.
    std::string pendingClosePath;
    std::string pendingCollapsePath;
    bool pendingQuit = false;
    int columns = 80;
    int rows = 24;
};

// At most nine buffers so the 1-9 keys can reach every one.
constexpr std::size_t kIdeMaxBuffers = 9;

// The command menu (Ctrl+Alt+C) lists every action with its combo. The same
// table drives the nano-style shortcut bar, the help overlay and key dispatch,
// so a binding is defined in exactly one place.
enum class IdeCommandId {
    Help, Open, Save, Exit, CloseTab, New, Find, Run, Lint, Format, Output,
    Goto, NextMatch, NextProblem, Build, RunProject, Folder, Sidebar, Editor,
    Clear,
};

struct IdeCommand {
    IdeCommandId id;
    const char* label;   // full label, shown in help and the command menu
    const char* bar;     // compact label for the nano-style shortcut bar
    const char* combo;
    int control;  // control byte, or 0 when only reachable from the menu
};

const IdeCommand* ideCommandById(IdeCommandId id);

const std::vector<IdeCommand>& ideCommands() {
    static const std::vector<IdeCommand> commands = {
        {IdeCommandId::Help,        "Help / key bindings", "Help",   "^G",  7},
        {IdeCommandId::Open,        "Open file",           "Open",   "^O", 15},
        {IdeCommandId::Save,        "Save",                "Save",   "^S", 19},
        {IdeCommandId::Exit,        "Exit",                "Exit",   "^X", 24},
        {IdeCommandId::CloseTab,    "Close tab",           "Close",  "^W", 23},
        {IdeCommandId::New,         "New file",            "New",    "^N", 14},
        {IdeCommandId::Find,        "Find",                "Find",   "^F",  6},
        {IdeCommandId::Run,         "Run file",            "Run",    "^R", 18},
        {IdeCommandId::Lint,        "Lint file",           "Lint",   "^L", 12},
        {IdeCommandId::Format,      "Format file",         "Format", "^T", 20},
        {IdeCommandId::Output,      "Toggle output pane",  "Output", "^Z", 26},
        {IdeCommandId::Goto,        "Go to line",          "Goto",   "^_", 31},
        {IdeCommandId::NextMatch,   "Next match",          "Next",   "^D",  4},
        {IdeCommandId::NextProblem, "Next problem",        "Problem","^V", 22},
        {IdeCommandId::Build,       "Build project",       "Build",  "^Y", 25},
        {IdeCommandId::RunProject,  "Run project",         "RunProj","^U", 21},
        {IdeCommandId::Folder,      "Change folder",       "Folder", "^B",  2},
        {IdeCommandId::Sidebar,     "Focus sidebar",       "Side",  "^P", 16},
        {IdeCommandId::Editor,      "Focus editor",        "Edit",  "^E",  5},
        {IdeCommandId::Clear,       "Clear output",        "Clear",  "^K", 11},
    };
    return commands;
}

// Shortcuts shown in the two-row nano-style bar at the bottom of the screen.
const std::vector<IdeCommandId>& ideBarShortcuts() {
    static const std::vector<IdeCommandId> ids = {
        IdeCommandId::Help, IdeCommandId::Open, IdeCommandId::Save,
        IdeCommandId::Exit, IdeCommandId::CloseTab, IdeCommandId::New,
        IdeCommandId::Find, IdeCommandId::Run, IdeCommandId::Lint,
        IdeCommandId::Format, IdeCommandId::Sidebar, IdeCommandId::Editor,
    };
    return ids;
}

int ideOutputHeight(const IdeState& state) {
    return state.showOutput ? std::min(8, state.rows / 3) : 0;
}

int ideEditorHeight(const IdeState& state) {
    // Rows 0-1 are bars, the last four rows are the open-files list, message
    // line and two shortcut rows.
    return std::max(1, state.rows - 6 - ideOutputHeight(state));
}

std::vector<std::string> ideSplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (const char c : text) {
        if (c == '\n') {
            if (!current.empty() && current.back() == '\r') current.pop_back();
            lines.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) lines.push_back(current);
    if (lines.empty()) lines.push_back(std::string());
    return lines;
}

std::string ideJoinLines(const std::vector<std::string>& lines) {
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) out += '\n';
        out += lines[i];
    }
    out += '\n';
    return out;
}

std::string ideTrim(const std::string& text) {
    std::size_t begin = 0, end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) --end;
    return text.substr(begin, end - begin);
}

std::string ideBaseName(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool ideReadText(const std::string& path, std::string& out, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot read '" + path + "'";
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    out = buffer.str();
    return true;
}

bool ideWriteText(const std::string& path, const std::string& text,
                  std::string& error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot write '" + path + "'";
        return false;
    }
    output << text;
    output.close();
    if (!output) {
        error = "cannot write '" + path + "'";
        return false;
    }
    return true;
}

std::string ideExtension(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    const std::size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos ||
        (slash != std::string::npos && dot < slash)) {
        return std::string();
    }
    std::string extension = path.substr(dot + 1);
    for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return extension;
}

bool ideIsLynx(const std::string& path) {
    return ideExtension(path) == "lynx";
}

// Formats that are never UTF-8 text. Anything with one of these suffixes is
// refused before it is read, so a PDF, PNG, DOCX or archive can never corrupt
// an editor buffer or dump binary into the tree.
const std::unordered_set<std::string>& ideBinaryExtensions() {
    static const std::unordered_set<std::string> extensions = {
        // documents / office
        "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "odt", "ods",
        "odp", "rtf", "pages", "epub", "mobi",
        // images
        "png", "jpg", "jpeg", "gif", "bmp", "ico", "webp", "tif", "tiff",
        "svg", "psd", "ai", "eps", "heic", "heif", "avif", "raw", "cr2",
        "nef", "arw", "dng",
        // audio / video
        "mp3", "wav", "flac", "ogg", "oga", "m4a", "aac", "wma", "opus",
        "mp4", "mkv", "avi", "mov", "wmv", "webm", "flv", "m4v", "mpg",
        "mpeg", "3gp",
        // archives / packages
        "zip", "tar", "gz", "tgz", "bz2", "tbz2", "xz", "txz", "zst", "lz4",
        "7z", "rar", "iso", "dmg", "deb", "rpm", "apk", "jar", "war", "whl",
        "msi", "exe", "appimage",
        // compiled / binary artifacts
        "so", "dll", "dylib", "a", "lib", "o", "obj", "class", "pyc", "pyo",
        "wasm", "bin", "dat", "db", "sqlite", "sqlite3", "mdb", "pak",
        "ttf", "otf", "woff", "woff2", "eot", "fon",
        // misc binaries
        "pdb", "dSYM", "elf", "lnk", "tmp", "swp", "swo",
    };
    return extensions;
}

bool ideIsNonTextFile(const std::string& path) {
    if (ideBinaryExtensions().count(ideExtension(path)) != 0) return true;
    // Sniff the first block: NUL bytes or a high ratio of control bytes mean
    // binary content even when the suffix looks harmless (or is missing, as in
    // Makefile, LICENSE, README, ...).
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    char buffer[4096];
    input.read(buffer, sizeof(buffer));
    const std::streamsize count = input.gcount();
    std::streamsize control = 0;
    for (std::streamsize i = 0; i < count; ++i) {
        const unsigned char c = static_cast<unsigned char>(buffer[i]);
        if (c == 0) return true;
        if (c < 0x09 || (c > 0x0d && c < 0x20)) ++control;
    }
    return count > 0 && control * 10 > count;
}

std::vector<IdeEntry> ideListDir(const std::string& path) {
    static const std::unordered_set<std::string> ignored = {
        ".git", "node_modules", "target", ".cache", "__pycache__",
    };
    std::vector<IdeEntry> entries;
    std::error_code error;
    for (const auto& item :
         std::filesystem::directory_iterator(path, error)) {
        if (error) break;
        const std::string name = item.path().filename().string();
        if (!name.empty() && name[0] == '.' && name != ".gitignore") continue;
        if (ignored.count(name) != 0) continue;
        bool isDir = item.is_directory(error);
        const bool nonText = !isDir && ideIsNonTextFile(item.path().string());
        entries.push_back(IdeEntry{name, item.path().string(), isDir, nonText});
    }
    std::sort(entries.begin(), entries.end(),
              [](const IdeEntry& a, const IdeEntry& b) {
                  if (a.isDir != b.isDir) return a.isDir;
                  std::string left = a.name, right = b.name;
                  for (char& c : left) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                  for (char& c : right) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                  return left < right;
              });
    return entries;
}

void ideAppendTree(IdeState& state, const std::string& directory, int depth,
                   const std::unordered_set<std::string>& expanded) {
    if (depth > 32) return;
    for (const IdeEntry& entry : ideListDir(directory)) {
        const std::size_t index = state.tree.size();
        state.tree.push_back(IdeTreeRow{entry.name, entry.path, entry.isDir,
                                        depth, false, entry.nonText});
        if (entry.isDir && depth < 32 && expanded.count(entry.path) != 0) {
            state.tree[index].expanded = true;
            ideAppendTree(state, entry.path, depth + 1, expanded);
        }
    }
}

void ideRefreshTree(IdeState& state) {
    std::unordered_set<std::string> expanded;
    for (const IdeTreeRow& row : state.tree) {
        if (row.expanded) expanded.insert(row.path);
    }
    state.tree.clear();
    ideAppendTree(state, state.root, 0, expanded);
    if (state.treeSelection >= state.tree.size()) {
        state.treeSelection = state.tree.empty() ? 0 : state.tree.size() - 1;
    }
}

void ideToggleDir(IdeState& state, std::size_t index) {
    if (index >= state.tree.size() || !state.tree[index].isDir) return;
    const IdeTreeRow row = state.tree[index];
    if (row.expanded) {
        std::size_t end = index + 1;
        while (end < state.tree.size() && state.tree[end].depth > row.depth) ++end;
        state.tree.erase(state.tree.begin() + static_cast<std::ptrdiff_t>(index + 1),
                         state.tree.begin() + static_cast<std::ptrdiff_t>(end));
        state.tree[index].expanded = false;
    } else {
        std::vector<IdeTreeRow> children;
        for (const IdeEntry& entry : ideListDir(row.path)) {
            children.push_back(IdeTreeRow{entry.name, entry.path, entry.isDir,
                                          row.depth + 1, false, entry.nonText});
        }
        state.tree[index].expanded = true;
        state.tree.insert(state.tree.begin() + static_cast<std::ptrdiff_t>(index + 1),
                          children.begin(), children.end());
    }
}

bool ideOpenFile(IdeState& state, const std::string& path);
void ideClosePath(IdeState& state, const std::string& path);
void ideCollapsePath(IdeState& state, const std::string& path);
bool ideAnyDirty(const IdeState& state);

bool ideBufferIsOpen(const IdeState& state, const std::string& path) {
    for (const IdeBuffer& buffer : state.buffers) {
        if (buffer.path == path) return true;
    }
    return false;
}

// Perform the action deferred by the save-all prompt (close a file, collapse a
// folder, quit).
void ideApplyPendingAction(IdeState& state, bool& quit) {
    if (!state.pendingClosePath.empty()) {
        ideClosePath(state, state.pendingClosePath);
    }
    if (!state.pendingCollapsePath.empty()) {
        ideCollapsePath(state, state.pendingCollapsePath);
    }
    if (state.pendingQuit) quit = true;
    state.pendingClosePath.clear();
    state.pendingCollapsePath.clear();
    state.pendingQuit = false;
}

// Close a file, collapse a folder or quit. When any buffer has unsaved edits,
// ask once to save all of them first; with a clean workspace the action happens
// immediately.
void ideRequestSaveAll(IdeState& state, const std::string& closePath,
                       const std::string& collapsePath, bool quitAfter,
                       bool& quit) {
    if (!ideAnyDirty(state)) {
        if (!closePath.empty()) ideClosePath(state, closePath);
        if (!collapsePath.empty()) ideCollapsePath(state, collapsePath);
        if (quitAfter) quit = true;
        state.status = closePath.empty() && collapsePath.empty()
                           ? "Exiting"
                           : state.status;
        return;
    }
    state.pendingClosePath = closePath;
    state.pendingCollapsePath = collapsePath;
    state.pendingQuit = quitAfter;
    state.prompt = IdePrompt::SaveAll;
    state.promptLabel = "Save all changes? (Y/n/c to cancel)";
    state.promptValue.clear();
}

// Open or toggle-close the selected tree row. A folder expands (and the
// selection steps into its first child); selecting an already-expanded folder
// collapses it. A file opens in the editor; selecting an already-open file
// closes it. `openFiles` is false for the Right arrow, used only to walk into
// folders.
void ideActivateTreeSelection(IdeState& state, bool openFiles) {
    if (state.treeSelection >= state.tree.size()) return;
    const IdeTreeRow row = state.tree[state.treeSelection];
    if (!row.isDir) {
        if (!openFiles) return;
        if (row.nonText) {
            state.status = "Cannot open non-text file: " + row.name;
            return;
        }
        if (ideBufferIsOpen(state, row.path)) {
            bool ignored = false;
            ideRequestSaveAll(state, row.path, std::string(), false, ignored);
        } else if (ideOpenFile(state, row.path)) {
            state.explorerFocus = false;
        }
        return;
    }
    if (row.expanded) {
        bool ignored = false;
        ideRequestSaveAll(state, std::string(), row.path, false, ignored);
        return;
    }
    const int depth = row.depth;
    ideToggleDir(state, state.treeSelection);
    if (state.treeSelection + 1 < state.tree.size() &&
        state.tree[state.treeSelection + 1].depth > depth) {
        ++state.treeSelection;
    }
    state.status = "Folder: " + row.name;
}

// Collapse the selected folder, or jump to its parent row with Left.
void ideCollapseOrParent(IdeState& state) {
    if (state.treeSelection >= state.tree.size()) return;
    const IdeTreeRow row = state.tree[state.treeSelection];
    if (row.expanded) {
        ideToggleDir(state, state.treeSelection);
        return;
    }
    if (row.depth == 0) return;
    for (std::size_t i = state.treeSelection; i > 0; --i) {
        if (state.tree[i - 1].depth < row.depth) {
            state.treeSelection = i - 1;
            state.status = "Folder: " + state.tree[i - 1].name;
            return;
        }
    }
}

bool ideHasBuffer(const IdeState& state) {
    return state.active < state.buffers.size();
}

IdeBuffer& ideActive(IdeState& state) {
    return state.buffers[state.active];
}

bool ideOpenFile(IdeState& state, const std::string& path) {
    for (std::size_t i = 0; i < state.buffers.size(); ++i) {
        if (state.buffers[i].path == path) {
            state.active = i;
            return true;
        }
    }
    if (ideIsNonTextFile(path)) {
        state.status = "Cannot open non-text file: " + ideBaseName(path);
        return false;
    }
    if (state.buffers.size() >= kIdeMaxBuffers) {
        state.status = "Only " + std::to_string(kIdeMaxBuffers) +
                       " files can be open; close one first";
        return false;
    }
    std::string text, error;
    if (!ideReadText(path, text, error)) {
        state.status = "Error: " + error;
        return false;
    }
    IdeBuffer buffer;
    buffer.path = path;
    buffer.lines = ideSplitLines(text);
    buffer.saved = ideJoinLines(buffer.lines);
    state.buffers.push_back(std::move(buffer));
    state.active = state.buffers.size() - 1;
    state.status = "Opened " + path;
    return true;
}

// Close a buffer by path, used when toggling an already-open file in the tree.
void ideClosePath(IdeState& state, const std::string& path) {
    for (std::size_t i = 0; i < state.buffers.size(); ++i) {
        if (state.buffers[i].path != path) continue;
        state.buffers.erase(state.buffers.begin() +
                            static_cast<std::ptrdiff_t>(i));
        // Closing a buffer before the active one shifts it down; closing the
        // active last buffer falls back to the new last one.
        if (i < state.active) --state.active;
        if (state.active >= state.buffers.size()) {
            state.active = state.buffers.empty() ? 0 : state.buffers.size() - 1;
        }
        if (state.buffers.empty()) state.explorerFocus = true;
        state.status = "Closed " + ideBaseName(path);
        return;
    }
}

// Collapse an expanded folder by path, used when toggling it in the tree.
void ideCollapsePath(IdeState& state, const std::string& path) {
    for (std::size_t i = 0; i < state.tree.size(); ++i) {
        if (state.tree[i].path != path || !state.tree[i].isDir) continue;
        if (state.tree[i].expanded) ideToggleDir(state, i);
        state.status = "Closed folder: " + state.tree[i].name;
        return;
    }
}

void ideSaveAll(IdeState& state) {
    int saved = 0;
    for (IdeBuffer& buffer : state.buffers) {
        if (ideJoinLines(buffer.lines) == buffer.saved) continue;
        std::string error;
        if (!ideWriteText(buffer.path, ideJoinLines(buffer.lines), error)) {
            state.status = "Error: " + error;
            return;
        }
        buffer.saved = ideJoinLines(buffer.lines);
        ++saved;
    }
    state.status = "Saved " + std::to_string(saved) + " file(s)";
}

void ideSaveActive(IdeState& state) {
    if (!ideHasBuffer(state)) return;
    IdeBuffer& buffer = ideActive(state);
    std::string error;
    if (!ideWriteText(buffer.path, ideJoinLines(buffer.lines), error)) {
        state.status = "Error: " + error;
        return;
    }
    buffer.saved = ideJoinLines(buffer.lines);
    state.status = "Saved " + buffer.path;
}

void ideEnsureVisible(IdeState& state, int editorHeight) {
    if (!ideHasBuffer(state)) return;
    IdeBuffer& buffer = ideActive(state);
    if (editorHeight < 1) editorHeight = 1;
    if (buffer.cursorLine < buffer.topLine) {
        buffer.topLine = buffer.cursorLine;
    } else if (buffer.cursorLine >=
               buffer.topLine + static_cast<std::size_t>(editorHeight)) {
        buffer.topLine = buffer.cursorLine -
                         static_cast<std::size_t>(editorHeight) + 1;
    }
}

bool ideFindPosition(const std::string& line, std::size_t& after, int& row,
                     int& column) {
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] != ':') continue;
        std::size_t j = i + 1;
        const std::size_t lineStart = j;
        while (j < line.size() && std::isdigit(static_cast<unsigned char>(line[j])) != 0) ++j;
        if (j == lineStart || j >= line.size() || line[j] != ':') continue;
        const int parsedLine = std::atoi(line.substr(lineStart, j - lineStart).c_str());
        std::size_t k = j + 1;
        const std::size_t columnStart = k;
        while (k < line.size() && std::isdigit(static_cast<unsigned char>(line[k])) != 0) ++k;
        if (k == columnStart || k >= line.size() || line[k] != ':') continue;
        const int parsedColumn = std::atoi(line.substr(columnStart, k - columnStart).c_str());
        after = k + 1;
        row = parsedLine;
        column = parsedColumn;
        return true;
    }
    return false;
}

std::string ideCodeAndMessage(const std::string& rest, std::string& code) {
    const std::size_t open = rest.find('[');
    if (open == std::string::npos) return ideTrim(rest);
    const std::size_t close = rest.find(']', open);
    if (close == std::string::npos) return ideTrim(rest);
    code = rest.substr(open + 1, close - open - 1);
    std::string message = ideTrim(rest.substr(close + 1));
    if (!message.empty() && message[0] == ':') message = ideTrim(message.substr(1));
    return message;
}

std::vector<IdeDiagnostic> ideParseDiagnostics(const std::string& text) {
    std::vector<IdeDiagnostic> out;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        const std::string trimmed = ideTrim(line);
        if (trimmed.empty() || trimmed.rfind("Lint OK", 0) == 0 ||
            trimmed.rfind("help:", 0) == 0 || trimmed[0] == '|') {
            continue;
        }
        std::size_t after = 0;
        int row = 0, column = 0;
        if (!ideFindPosition(line, after, row, column)) {
            if (trimmed.rfind("lynxer:", 0) == 0) {
                out.push_back(IdeDiagnostic{"", 0, 0, "error", "", trimmed});
            }
            continue;
        }
        const std::string rest = ideTrim(line.substr(after));
        if (rest.empty()) continue;
        IdeDiagnostic diagnostic;
        diagnostic.line = row;
        diagnostic.column = column;
        if (rest.rfind("warning[", 0) == 0) {
            diagnostic.severity = "warning";
        } else if (rest.rfind("error[", 0) == 0 || rest[0] == '[') {
            diagnostic.severity = "error";
        } else {
            continue;
        }
        diagnostic.message = ideCodeAndMessage(rest, diagnostic.code);
        if (diagnostic.message.empty()) continue;
        out.push_back(std::move(diagnostic));
    }
    return out;
}

void ideCounts(const IdeState& state, int& errors, int& warnings) {
    errors = 0;
    warnings = 0;
    for (const IdeDiagnostic& diagnostic : state.diagnostics) {
        if (diagnostic.severity == "error") ++errors;
        else if (diagnostic.severity == "warning") ++warnings;
    }
}

void ideRender(IdeState& state) {
    platform::terminalSize(state.columns, state.rows);
    const int columns = state.columns;
    const int rows = state.rows;
    std::vector<std::string> frame(static_cast<std::size_t>(rows), std::string());

    if (columns < 40 || rows < 10) {
        std::cout << "\x1b[2J\x1b[H" << kIdeReset
                  << "Lynxer IDE: terminal too small (need 40x10)\n"
                  << std::flush;
        return;
    }

    // nano-style frame: a reverse-video title bar, the editor body, a message
    // line, then two rows of caret shortcuts. No boxes or panels.
    const int messageRow = rows - 3;
    const int hintStart = rows - 2;
    const int filesRow = rows - 4;
    const int outputHeight = ideOutputHeight(state);
    const int outputStart = filesRow - outputHeight;
    const int mainStart = 2;
    const int mainEnd = outputHeight > 0 ? outputStart - 1 : filesRow - 1;
    const int mainHeight = mainEnd - mainStart + 1;
    const int explorerWidth = std::min(32, std::max(22, columns / 4));

    // ---------------------------------------------------------- title bar
    std::ostringstream title;
    title << "  LYNXER IDE " << state.version;
    if (ideHasBuffer(state)) {
        const IdeBuffer& buffer = ideActive(state);
        title << "        File: " << buffer.path;
        if (ideJoinLines(buffer.lines) != buffer.saved) title << "   Modified";
        if (state.buffers.size() > 1) {
            title << "   [" << (state.active + 1) << "/" << state.buffers.size() << "]";
        }
    } else {
        title << "        New Buffer";
    }
    frame[0] = std::string(kIdeReverse) + kIdeBold +
               idePad(ideClamp(title.str(), static_cast<std::size_t>(columns)),
                      static_cast<std::size_t>(columns)) +
               kIdeReset;

    // ------------------------------------------------------------ tab line
    std::string tabs = ideFg(IC_MUTED) + "  ";
    if (state.buffers.empty()) {
        tabs += "No file open    ^O Open    M-^C Commands";
    } else {
        for (std::size_t i = 0; i < state.buffers.size(); ++i) {
            const bool active = i == state.active;
            tabs += (active ? ideFg(IC_ACCENT) : ideFg(IC_MUTED)) +
                    (active ? "[" + ideBaseName(state.buffers[i].path) + "]"
                            : ideBaseName(state.buffers[i].path)) +
                    kIdeReset + "  ";
        }
    }
    ideSetRow(frame, 1, tabs + kIdeReset, columns, IC_SURFACE);

    // --------------------------------------------------------------- body
    std::vector<std::string> helpLines;
    helpLines.push_back("  LYNXER IDE - KEY BINDINGS");
    helpLines.push_back("");
    helpLines.push_back("  Shortcuts (^ = Ctrl, M-^C = Ctrl+Alt+C):");
    for (const IdeCommand& command : ideCommands()) {
        std::ostringstream line;
        line << "    " << std::left << std::setw(6) << command.combo
             << command.label;
        helpLines.push_back(line.str());
    }
    helpLines.push_back("");
    helpLines.push_back("  M-^C / ^A   Command menu: every command, arrow keys + Enter");
    helpLines.push_back("  ^Q          Exit (alias for ^X)");
    helpLines.push_back("  Ctrl+Left / Ctrl+Right   previous / next tab");
    helpLines.push_back("  1-9 / Alt+1-9   switch open file (1-9 in sidebar)");
    helpLines.push_back("");
    helpLines.push_back("  Explorer: Up/Down select files and folders, Enter opens a file");
    helpLines.push_back("            or expands a folder; selecting an open file/folder");
    helpLines.push_back("            closes it (save-all prompt if edits are unsaved)");
    helpLines.push_back("            Right steps into a folder, Left collapses / parent");
    helpLines.push_back("  Editor:   arrow keys move, Home/End, PgUp/PgDn, type to edit");
    helpLines.push_back("            Enter newline, Tab indent, Backspace/Delete remove");
    helpLines.push_back("            Left/Right cross line boundaries");
    helpLines.push_back("");
    helpLines.push_back("  Up to 9 files open at once; the bottom FILES list shows them.");
    helpLines.push_back("  Non-text files (.pdf, .png, .docx, ...) are never opened.");
    helpLines.push_back("  Syntax highlighting is enabled for .lynx files only.");
    helpLines.push_back("  Up/Down scroll; ^G, Esc or q closes this help.");
    const std::size_t maxHelpTop =
        helpLines.size() > static_cast<std::size_t>(mainHeight)
            ? helpLines.size() - static_cast<std::size_t>(mainHeight)
            : 0;
    state.helpTop = std::min(state.helpTop, maxHelpTop);

    const int visibleCommands = std::max(1, mainHeight - 2);
    std::size_t commandTop = 0;
    if (state.commandIndex + 1 > static_cast<std::size_t>(visibleCommands)) {
        commandTop = state.commandIndex + 1 - static_cast<std::size_t>(visibleCommands);
    }
    state.commandTop = commandTop;

    for (int r = 0; r < mainHeight; ++r) {
        const int row = mainStart + r;
        std::string left;
        std::string right;

        if (state.showHelp || state.showCommands) {
            std::string text;
            int color = IC_TEXT;
            bool standout = false;
            if (state.showHelp) {
                const std::size_t index = state.helpTop + static_cast<std::size_t>(r);
                if (index < helpLines.size()) {
                    text = helpLines[index];
                    if (index == 0) color = IC_ACCENT;
                }
            } else if (r == 0) {
                text = "  COMMANDS - Up/Down select, Enter run, Esc close";
                color = IC_ACCENT;
            } else if (r > 1) {
                const std::size_t index = commandTop + static_cast<std::size_t>(r - 2);
                if (index < ideCommands().size()) {
                    const IdeCommand& command = ideCommands()[index];
                    std::ostringstream line;
                    line << (index == state.commandIndex ? "  > " : "    ")
                         << std::left << std::setw(6) << command.combo
                         << command.label;
                    text = line.str();
                    standout = index == state.commandIndex;
                }
            }
            left = (standout ? std::string(kIdeReverse) : std::string()) +
                   ideFg(color) +
                   idePad(text, static_cast<std::size_t>(columns)) + kIdeReset;
            frame[static_cast<std::size_t>(row)] =
                ideClamp(left, static_cast<std::size_t>(columns));
            continue;
        }

        // Explorer column.
        const std::size_t treeIndex = state.treeTop + static_cast<std::size_t>(r);
        if (treeIndex < state.tree.size()) {
            const IdeTreeRow& item = state.tree[treeIndex];
            const std::string indent(static_cast<std::size_t>(item.depth) * 2, ' ');
            const std::string marker = item.isDir
                                           ? (item.expanded ? "[-] " : "[+] ")
                                           : (item.nonText ? " B  "
                                                           : (ideIsLynx(item.path) ? " L  " : " .  "));
            const bool selected = treeIndex == state.treeSelection && state.explorerFocus;
            left = (selected ? std::string(kIdeReverse) : std::string()) +
                   (item.isDir ? ideFg(IC_ACCENT) :
                                 (item.nonText ? ideFg(IC_ERROR) :
                                  (ideIsLynx(item.path) ? ideFg(IC_OK) : ideFg(IC_MUTED)))) +
                   indent + marker + item.name + kIdeReset;
        }
        left = idePad(ideClamp(left, static_cast<std::size_t>(explorerWidth - 1)),
                      static_cast<std::size_t>(explorerWidth - 1));
        left += ideFg(IC_MUTED) + "|" + kIdeReset;

        // Editor column.
        const int editorWidth = columns - explorerWidth;
        if (ideHasBuffer(state)) {
            IdeBuffer& buffer = ideActive(state);
            const std::size_t lineIndex = buffer.topLine + static_cast<std::size_t>(r);
            if (lineIndex < buffer.lines.size()) {
                std::ostringstream gutter;
                gutter << std::setw(4) << (lineIndex + 1) << " | ";
                const bool current = lineIndex == buffer.cursorLine;
                right = ideFg(IC_GUTTER) + gutter.str() + kIdeReset +
                        ideRenderLine(buffer.lines[lineIndex],
                                      ideIsLynx(buffer.path),
                                      buffer.cursorColumn, current) +
                        kIdeReset;
            } else {
                right = ideFg(IC_GUTTER) + "     | ~" + kIdeReset;
            }
        } else if (r == mainHeight / 2) {
            right = ideFg(IC_MUTED) +
                    "  ^O Open a file, or pick one in the explorer" + kIdeReset;
        }
        frame[static_cast<std::size_t>(row)] =
            ideClamp(left, static_cast<std::size_t>(explorerWidth)) +
            ideClamp(right, static_cast<std::size_t>(editorWidth));
    }

    // ------------------------------------------------------- output panel
    if (outputHeight > 0 && !state.showHelp && !state.showCommands) {
        int errors = 0, warnings = 0;
        ideCounts(state, errors, warnings);
        std::ostringstream panelHeader;
        panelHeader << kIdeBold << ideFg(IC_ACCENT) << " OUTPUT " << kIdeReset
                    << ideFg(errors > 0 ? IC_ERROR : IC_MUTED) << "E " << errors
                    << kIdeReset << "  "
                    << ideFg(warnings > 0 ? IC_WARN : IC_MUTED) << "W " << warnings
                    << kIdeReset << ideFg(IC_MUTED)
                    << "    ^R run  ^L lint  ^T format  ^Y build  ^U run-project"
                    << kIdeReset;
        ideSetRow(frame, outputStart, panelHeader.str(), columns, IC_PANEL);
        const std::vector<std::string> outputLines = ideSplitLines(state.output);
        const int visible = outputHeight - 1;
        const std::size_t first = outputLines.size() >
                                         static_cast<std::size_t>(visible)
                                     ? outputLines.size() -
                                           static_cast<std::size_t>(visible)
                                     : 0;
        for (int i = 0; i < visible; ++i) {
            const std::size_t index = first + static_cast<std::size_t>(i);
            std::string text = index < outputLines.size() ? outputLines[index] : std::string();
            int color = IC_MUTED;
            if (text.find("error") != std::string::npos ||
                text.find("Error") != std::string::npos) color = IC_ERROR;
            else if (text.find("warning") != std::string::npos ||
                     text.find("Warning") != std::string::npos) color = IC_WARN;
            else if (!text.empty() && text[0] == '[') color = IC_INFO;
            ideSetRow(frame, outputStart + 1 + i,
                      ideFg(color) + " " + text + kIdeReset, columns, IC_SURFACE);
        }
    }

    // ---------------------------------------------------- open-files list
    // One row under everything, listing every open buffer with its 1-9 key.
    // The active buffer is highlighted; a trailing * marks unsaved edits.
    {
        std::ostringstream list;
        list << " " << kIdeBold << ideFg(IC_ACCENT) << "FILES " << kIdeReset;
        if (state.buffers.empty()) {
            list << ideFg(IC_MUTED) << "(none - ^O to open)" << kIdeReset;
        } else {
            for (std::size_t i = 0; i < state.buffers.size(); ++i) {
                const IdeBuffer& buffer = state.buffers[i];
                const bool activeBuffer = i == state.active;
                std::string entry = " " + std::to_string(i + 1) + " " +
                                    ideBaseName(buffer.path);
                if (ideJoinLines(buffer.lines) != buffer.saved) entry += "*";
                entry += " ";
                if (activeBuffer) {
                    list << kIdeReverse << ideFg(IC_TEXT) << entry << kIdeReset;
                } else {
                    list << ideFg(IC_MUTED) << entry << kIdeReset;
                }
            }
            list << ideFg(IC_MUTED) << "  ^W close  1-9 / Alt+1-9 switch"
                 << kIdeReset;
        }
        ideSetRow(frame, filesRow, list.str(), columns, IC_SURFACE);
    }

    // ------------------------------------------------------- message line
    // nano shows prompts and status messages on one reverse-video line just
    // above the shortcut bar.
    std::string message;
    if (state.prompt != IdePrompt::None) {
        message = " " + state.promptLabel + ": " + state.promptValue +
                  std::string(kIdeReverse) + " " + "\x1b[27m";
    } else {
        std::ostringstream status;
        if (ideHasBuffer(state)) {
            const IdeBuffer& buffer = ideActive(state);
            status << " Ln " << (buffer.cursorLine + 1) << ", Col "
                   << (buffer.cursorColumn + 1) << "   "
                   << (ideIsLynx(buffer.path) ? "Lynxer" : "Plain text") << "   UTF-8";
            if (ideJoinLines(buffer.lines) != buffer.saved) status << "   modified";
        } else {
            status << " No file";
        }
        int errors = 0, warnings = 0;
        ideCounts(state, errors, warnings);
        status << "   " << errors << " errors  " << warnings << " warnings";
        if (!state.status.empty()) status << "   " << state.status;
        message = status.str();
    }
    frame[static_cast<std::size_t>(messageRow)] = std::string(kIdeReverse) +
        idePad(ideClamp(message, static_cast<std::size_t>(columns)),
               static_cast<std::size_t>(columns)) + kIdeReset;

    // ------------------------------------------------------- shortcut bar
    // Two rows of nano-style `^X Label` pairs. Overlays and prompts replace
    // them with context-specific keys.
    for (int line = 0; line < 2; ++line) {
        std::string hint;
        if (state.showHelp) {
            hint = line == 0
                       ? " ^G Help   Esc Close   Up/Down Scroll   q Close"
                       : " All actions use Ctrl combos - no function keys";
        } else if (state.showCommands) {
            hint = line == 0
                       ? " Up/Down Select   Enter Run   Esc Close   M-^C Toggle"
                       : " ^G Help   ^X Exit   ^O Open   ^S Save   ^R Run";
        } else if (state.prompt == IdePrompt::SaveAll) {
            hint = line == 0 ? " Y save all   N discard   C cancel   Esc cancel"
                             : " ^S Save   ^W Close   ^X Exit   M-^C Commands";
        } else if (state.prompt != IdePrompt::None) {
            hint = line == 0 ? " Enter Confirm   Esc Cancel   ^G Help"
                             : " ^O Open   ^S Save   ^X Exit   M-^C Commands";
        } else if (state.explorerFocus || !ideHasBuffer(state)) {
            hint = line == 0
                       ? " Up/Down Select   Enter Open/Close   Right In   Left Back"
                       : " 1-9 File   ^P Side   ^E Editor   ^W Close   M-^C More";
        } else {
            const std::vector<IdeCommandId>& ids = ideBarShortcuts();
            const std::size_t half = (ids.size() + 1) / 2;
            const std::size_t begin = static_cast<std::size_t>(line) * half;
            const std::size_t end = std::min(begin + half, ids.size());
            hint = " ";
            for (std::size_t i = begin; i < end; ++i) {
                const IdeCommand* command = ideCommandById(ids[i]);
                if (command == nullptr) continue;
                hint += std::string(kIdeBold) + ideFg(IC_ACCENT) +
                        command->combo + kIdeReset + " " + command->bar + "   ";
            }
            if (line == 1) {
                hint += std::string(kIdeBold) + ideFg(IC_ACCENT) + "M-^C" +
                        kIdeReset + " More";
            }
        }
        frame[static_cast<std::size_t>(hintStart + line)] =
            idePad(ideClamp(ideFg(IC_MUTED) + hint + kIdeReset,
                            static_cast<std::size_t>(columns)),
                   static_cast<std::size_t>(columns));
    }

    std::string output;
    for (int i = 0; i < rows; ++i) {
        output += "\x1b[" + std::to_string(i + 1) + ";1H\x1b[0m\x1b[K";
        output += frame[static_cast<std::size_t>(i)];
        output += kIdeReset;
    }
    std::cout << output << std::flush;
}

void ideRunCommand(IdeState& state, const std::vector<std::string>& arguments,
                   const std::string& label, bool lint, bool reload) {
    std::vector<std::string> command = {state.interpreter};
    for (const std::string& argument : arguments) command.push_back(argument);
    state.status = label + " ...";
    ideRender(state);
    platform::CapturedCommand captured;
    std::string error;
    if (!platform::captureProcess(command, captured, error)) {
        state.output = "error: " + error + "\n";
        state.showOutput = true;
        state.status = label + " failed";
        return;
    }
    std::ostringstream out;
    out << label << " [exit " << captured.exitCode << "]\n";
    out << captured.output;
    state.output = out.str();
    if (state.output.empty() || state.output.back() != '\n') state.output += '\n';
    state.showOutput = true;
    state.status = label + " finished [exit " +
                   std::to_string(captured.exitCode) + "]";
    if (lint) {
        state.diagnostics = ideParseDiagnostics(captured.output);
        state.diagnosticIndex = 0;
    }
    if (reload && ideHasBuffer(state)) {
        std::string text, readError;
        if (ideReadText(ideActive(state).path, text, readError)) {
            IdeBuffer& buffer = ideActive(state);
            buffer.lines = ideSplitLines(text);
            buffer.saved = ideJoinLines(buffer.lines);
            buffer.cursorLine = std::min(buffer.cursorLine, buffer.lines.size() - 1);
            buffer.cursorColumn = 0;
        }
    }
}

void ideCommandForFile(IdeState& state, const std::string& flag,
                       const std::string& label, bool lint, bool reload) {
    if (!ideHasBuffer(state)) {
        state.status = "Open a file first";
        return;
    }
    ideSaveActive(state);
    std::vector<std::string> arguments;
    if (!flag.empty()) arguments.push_back(flag);
    arguments.push_back(ideActive(state).path);
    ideRunCommand(state, arguments, label, lint, reload);
}

void ideInsertText(IdeState& state, const std::string& text) {
    if (!ideHasBuffer(state)) return;
    IdeBuffer& buffer = ideActive(state);
    std::string& line = buffer.lines[buffer.cursorLine];
    buffer.cursorColumn = std::min(buffer.cursorColumn, line.size());
    line.insert(buffer.cursorColumn, text);
    buffer.cursorColumn += text.size();
}

void ideNewline(IdeState& state) {
    IdeBuffer& buffer = ideActive(state);
    const std::string& line = buffer.lines[buffer.cursorLine];
    const std::string tail = line.substr(buffer.cursorColumn);
    buffer.lines[buffer.cursorLine] = line.substr(0, buffer.cursorColumn);
    buffer.lines.insert(buffer.lines.begin() +
                            static_cast<std::ptrdiff_t>(buffer.cursorLine + 1),
                        tail);
    ++buffer.cursorLine;
    buffer.cursorColumn = 0;
}

void ideBackspace(IdeState& state) {
    IdeBuffer& buffer = ideActive(state);
    if (buffer.cursorColumn > 0) {
        std::string& line = buffer.lines[buffer.cursorLine];
        std::size_t start = buffer.cursorColumn - 1;
        while (start > 0 &&
               (static_cast<unsigned char>(line[start]) & 0xC0) == 0x80) {
            --start;
        }
        line.erase(start, buffer.cursorColumn - start);
        buffer.cursorColumn = start;
    } else if (buffer.cursorLine > 0) {
        const std::string tail = buffer.lines[buffer.cursorLine];
        buffer.cursorColumn = buffer.lines[buffer.cursorLine - 1].size();
        buffer.lines[buffer.cursorLine - 1] += tail;
        buffer.lines.erase(buffer.lines.begin() +
                           static_cast<std::ptrdiff_t>(buffer.cursorLine));
        --buffer.cursorLine;
    }
}

void ideDelete(IdeState& state) {
    IdeBuffer& buffer = ideActive(state);
    std::string& line = buffer.lines[buffer.cursorLine];
    if (buffer.cursorColumn < line.size()) {
        std::size_t end = buffer.cursorColumn + 1;
        while (end < line.size() &&
               (static_cast<unsigned char>(line[end]) & 0xC0) == 0x80) {
            ++end;
        }
        line.erase(buffer.cursorColumn, end - buffer.cursorColumn);
    } else if (buffer.cursorLine + 1 < buffer.lines.size()) {
        line += buffer.lines[buffer.cursorLine + 1];
        buffer.lines.erase(buffer.lines.begin() +
                           static_cast<std::ptrdiff_t>(buffer.cursorLine + 1));
    }
}

void ideMoveCursor(IdeState& state, int deltaLine, int deltaColumn) {
    IdeBuffer& buffer = ideActive(state);
    const std::size_t maxLine = buffer.lines.size() - 1;
    if (deltaLine < 0) {
        buffer.cursorLine = buffer.cursorLine > static_cast<std::size_t>(-deltaLine)
                                ? buffer.cursorLine - static_cast<std::size_t>(-deltaLine)
                                : 0;
    } else if (deltaLine > 0) {
        buffer.cursorLine = std::min(buffer.cursorLine + static_cast<std::size_t>(deltaLine), maxLine);
    }
    const std::string& line = buffer.lines[buffer.cursorLine];
    if (deltaColumn < 0) {
        std::size_t target = buffer.cursorColumn;
        for (int i = 0; i < -deltaColumn && target > 0; ++i) {
            --target;
            while (target > 0 && (static_cast<unsigned char>(line[target]) & 0xC0) == 0x80) --target;
        }
        buffer.cursorColumn = target;
    } else if (deltaColumn > 0) {
        std::size_t target = buffer.cursorColumn;
        for (int i = 0; i < deltaColumn && target < line.size(); ++i) {
            ++target;
            while (target < line.size() && (static_cast<unsigned char>(line[target]) & 0xC0) == 0x80) ++target;
        }
        buffer.cursorColumn = target;
    }
    buffer.cursorColumn = std::min(buffer.cursorColumn, buffer.lines[buffer.cursorLine].size());
}

// Left and Right cross line boundaries: Left at the start of a line jumps to
// the end of the line above, Right at the end of a line jumps to the start of
// the line below. Without this the caret gets stuck at the first and last
// column of a line.
void ideMoveLeft(IdeState& state) {
    IdeBuffer& buffer = ideActive(state);
    if (buffer.cursorColumn > 0) {
        ideMoveCursor(state, 0, -1);
        return;
    }
    if (buffer.cursorLine == 0) return;
    --buffer.cursorLine;
    buffer.cursorColumn = buffer.lines[buffer.cursorLine].size();
}

void ideMoveRight(IdeState& state) {
    IdeBuffer& buffer = ideActive(state);
    if (buffer.cursorColumn < buffer.lines[buffer.cursorLine].size()) {
        ideMoveCursor(state, 0, 1);
        return;
    }
    if (buffer.cursorLine + 1 >= buffer.lines.size()) return;
    ++buffer.cursorLine;
    buffer.cursorColumn = 0;
}

void ideFindNext(IdeState& state) {
    if (!ideHasBuffer(state) || state.findQuery.empty()) {
        state.status = "Nothing to find (use Ctrl+F)";
        return;
    }
    IdeBuffer& buffer = ideActive(state);
    const std::size_t count = buffer.lines.size();
    for (std::size_t step = 0; step < count; ++step) {
        const std::size_t index = (buffer.cursorLine + step) % count;
        std::size_t from = (step == 0) ? buffer.cursorColumn + 1 : 0;
        const std::string& line = buffer.lines[index];
        const std::size_t found = line.find(state.findQuery, from);
        if (found != std::string::npos) {
            buffer.cursorLine = index;
            buffer.cursorColumn = found;
            state.status = "Found '" + state.findQuery + "'";
            return;
        }
    }
    state.status = "Not found: " + state.findQuery;
}

void ideNextDiagnostic(IdeState& state) {
    if (state.diagnostics.empty()) {
        state.status = "No problems";
        return;
    }
    for (std::size_t step = 0; step < state.diagnostics.size(); ++step) {
        state.diagnosticIndex = (state.diagnosticIndex + 1) % state.diagnostics.size();
        const IdeDiagnostic& diagnostic = state.diagnostics[state.diagnosticIndex];
        if (diagnostic.line <= 0 || !ideHasBuffer(state)) continue;
        IdeBuffer& buffer = ideActive(state);
        buffer.cursorLine = std::min(static_cast<std::size_t>(diagnostic.line - 1),
                                     buffer.lines.size() - 1);
        buffer.cursorColumn = static_cast<std::size_t>(std::max(0, diagnostic.column - 1));
        buffer.cursorColumn = std::min(buffer.cursorColumn,
                                       buffer.lines[buffer.cursorLine].size());
        state.status = diagnostic.severity + ": " + diagnostic.message;
        return;
    }
    state.status = "No located problems";
}

void ideConfirmPrompt(IdeState& state, bool& quit) {
    const std::string value = ideTrim(state.promptValue);
    switch (state.prompt) {
        case IdePrompt::Open:
            if (!value.empty()) {
                if (ideOpenFile(state, value)) state.explorerFocus = false;
            }
            break;
        case IdePrompt::New: {
            if (value.empty()) break;
            std::string error;
            if (!ideWriteText(value, "\n", error)) {
                state.status = "Error: " + error;
                break;
            }
            ideRefreshTree(state);
            if (ideOpenFile(state, value)) state.explorerFocus = false;
            break;
        }
        case IdePrompt::Folder: {
            std::error_code error;
            if (!value.empty() && std::filesystem::is_directory(value, error)) {
                state.root = std::filesystem::absolute(value, error).string();
                state.tree.clear();
                state.treeSelection = 0;
                state.treeTop = 0;
                ideRefreshTree(state);
                state.status = "Folder: " + state.root;
            } else {
                state.status = "Not a directory: " + value;
            }
            break;
        }
        case IdePrompt::Goto: {
            const int target = std::atoi(value.c_str());
            if (ideHasBuffer(state) && target > 0) {
                IdeBuffer& buffer = ideActive(state);
                buffer.cursorLine = std::min(static_cast<std::size_t>(target - 1),
                                             buffer.lines.size() - 1);
                buffer.cursorColumn = 0;
                state.status = "Line " + std::to_string(target);
            }
            break;
        }
        case IdePrompt::Find:
            state.findQuery = value;
            ideFindNext(state);
            break;
        case IdePrompt::SaveAll: {
            const bool cancel = value == "c" || value == "C" ||
                                value == "cancel" || value == "Cancel";
            if (cancel) {
                state.status = "Close cancelled";
                state.pendingClosePath.clear();
                state.pendingCollapsePath.clear();
                state.pendingQuit = false;
                break;
            }
            const bool no = value == "n" || value == "N" || value == "no";
            if (!no) ideSaveAll(state);  // default (Y / Enter) saves all
            ideApplyPendingAction(state, quit);
            break;
        }
        case IdePrompt::None:
            break;
    }
    state.prompt = IdePrompt::None;
    state.promptValue.clear();
}

bool ideAnyDirty(const IdeState& state) {
    for (const IdeBuffer& buffer : state.buffers) {
        if (ideJoinLines(buffer.lines) != buffer.saved) return true;
    }
    return false;
}

// Runs one command from either a key combo or the Ctrl+Alt+C menu. Returns
// true when the IDE should exit.
bool ideExecuteCommand(IdeState& state, IdeCommandId id) {
    switch (id) {
        case IdeCommandId::Help:
            state.showHelp = true;
            state.helpTop = 0;
            return false;
        case IdeCommandId::Open:
            state.prompt = IdePrompt::Open;
            state.promptLabel = "Open file";
            state.promptValue.clear();
            return false;
        case IdeCommandId::New:
            state.prompt = IdePrompt::New;
            state.promptLabel = "New file";
            state.promptValue.clear();
            return false;
        case IdeCommandId::Save:
            ideSaveActive(state);
            return false;
        case IdeCommandId::Exit: {
            bool quit = false;
            ideRequestSaveAll(state, std::string(), std::string(), true, quit);
            return quit;
        }
        case IdeCommandId::CloseTab:
            if (ideHasBuffer(state)) {
                bool ignored = false;
                ideRequestSaveAll(state, ideActive(state).path, std::string(),
                                  false, ignored);
            }
            return false;
        case IdeCommandId::Find:
            state.prompt = IdePrompt::Find;
            state.promptLabel = "Find";
            state.promptValue = state.findQuery;
            return false;
        case IdeCommandId::Run:
            ideCommandForFile(state, "", "Run", false, false);
            return false;
        case IdeCommandId::Lint:
            ideCommandForFile(state, "--lint", "Lint", true, false);
            return false;
        case IdeCommandId::Format:
            ideCommandForFile(state, "--format", "Format", false, true);
            return false;
        case IdeCommandId::Output:
            state.showOutput = !state.showOutput;
            return false;
        case IdeCommandId::Goto:
            state.prompt = IdePrompt::Goto;
            state.promptLabel = "Go to line";
            state.promptValue.clear();
            return false;
        case IdeCommandId::NextMatch:
            ideFindNext(state);
            return false;
        case IdeCommandId::NextProblem:
            ideNextDiagnostic(state);
            return false;
        case IdeCommandId::Build:
            ideRunCommand(state, {"--build"}, "Build project", false, false);
            return false;
        case IdeCommandId::RunProject:
            ideRunCommand(state, {"--run"}, "Run project", false, false);
            return false;
        case IdeCommandId::Folder:
            state.prompt = IdePrompt::Folder;
            state.promptLabel = "Open folder";
            state.promptValue = state.root;
            return false;
        case IdeCommandId::Sidebar:
            state.explorerFocus = true;
            state.status = "Sidebar focused";
            return false;
        case IdeCommandId::Editor:
            state.explorerFocus = false;
            state.status = "Editor focused";
            return false;
        case IdeCommandId::Clear:
            state.output.clear();
            state.diagnostics.clear();
            state.status = "Output cleared";
            return false;
    }
    return false;
}

const IdeCommand* ideCommandById(IdeCommandId id) {
    for (const IdeCommand& command : ideCommands()) {
        if (command.id == id) return &command;
    }
    return nullptr;
}

bool ideHandleKey(IdeState& state, const platform::KeyEvent& event) {
    bool quit = false;

    // ------------------------------------------------------- prompt editing
    if (state.prompt != IdePrompt::None) {
        if (event.code == 0 && !event.bytes.empty()) {
            const unsigned char c = static_cast<unsigned char>(event.bytes[0]);
            if (c == 27) {
                state.prompt = IdePrompt::None;
                state.promptValue.clear();
                state.pendingClosePath.clear();
                state.pendingCollapsePath.clear();
                state.pendingQuit = false;
                state.status = "Cancelled";
            } else if (c == '\r' || c == '\n') {
                ideConfirmPrompt(state, quit);
            } else if (c == 8 || c == 127) {
                if (!state.promptValue.empty()) state.promptValue.pop_back();
            } else if (c >= 32) {
                state.promptValue += event.bytes;
            }
        }
        return quit;
    }

    const int key = event.code;
    int control = 0;
    if (key == 0 && event.bytes.size() == 1) {
        const unsigned char c = static_cast<unsigned char>(event.bytes[0]);
        if (c < 32) control = static_cast<int>(c);
    }
    const bool escape =
        key == 0 && event.bytes.size() == 1 && event.bytes[0] == 27;
    // Backspace reaches the terminal as BS (8) or as DEL (127) depending on
    // the terminal and the keyboard layout, so both bytes must reach the
    // editor. DEL is not a control character, so it never shows up in
    // `control`.
    const bool backspaceKey =
        key == 0 && event.bytes.size() == 1 &&
        (event.bytes[0] == 8 || event.bytes[0] == 127);
    const bool commandKey = key == platform::KEY_COMMAND || control == 1 ||
                            control == 3;  // ^A / ^C aliases
    // With no file open there is no editor to move a caret in, so the arrow
    // keys browse the tree regardless of the focus flag.
    const bool inExplorer = state.explorerFocus || !ideHasBuffer(state);

    // 1-9 switch open buffers. Plain digits are only shortcuts while the
    // sidebar is focused (typing a digit in the editor must insert it);
    // Alt+1-9 works from anywhere, including during editing.
    int bufferIndex = -1;
    bool focusEditor = false;
    if (key >= platform::KEY_ALT_1 && key <= platform::KEY_ALT_9) {
        bufferIndex = static_cast<int>(key - platform::KEY_ALT_1);
        focusEditor = true;
    } else if (inExplorer && key == 0 && event.bytes.size() == 1 &&
               event.bytes[0] >= '1' && event.bytes[0] <= '9') {
        bufferIndex = static_cast<int>(event.bytes[0] - '1');
    }
    if (bufferIndex >= 0) {
        if (static_cast<std::size_t>(bufferIndex) < state.buffers.size()) {
            state.active = static_cast<std::size_t>(bufferIndex);
            if (focusEditor) state.explorerFocus = false;
            state.status = "File " + std::to_string(bufferIndex + 1) + ": " +
                           ideBaseName(ideActive(state).path);
        } else {
            state.status = "No file " + std::to_string(bufferIndex + 1);
        }
        return false;
    }

    // ------------------------------------------- command menu (Ctrl+Alt+C)
    if (state.showCommands) {
        const std::size_t count = ideCommands().size();
        switch (key) {
            case platform::KEY_UP:
            case platform::KEY_CTRL_UP:
                if (state.commandIndex > 0) --state.commandIndex;
                break;
            case platform::KEY_DOWN:
            case platform::KEY_CTRL_DOWN:
                if (state.commandIndex + 1 < count) ++state.commandIndex;
                break;
            case platform::KEY_HOME: state.commandIndex = 0; break;
            case platform::KEY_END: state.commandIndex = count - 1; break;
            case platform::KEY_PAGE_UP:
                state.commandIndex = state.commandIndex > 5 ? state.commandIndex - 5 : 0;
                break;
            case platform::KEY_PAGE_DOWN:
                state.commandIndex = std::min(state.commandIndex + 5, count - 1);
                break;
            default: break;
        }
        if (control == 13 || control == 10) {  // Enter runs the selection
            const IdeCommandId id = ideCommands()[state.commandIndex].id;
            state.showCommands = false;
            return ideExecuteCommand(state, id);
        }
        if (escape || commandKey || control == 7 ||
            (key == 0 && event.bytes.size() == 1 && event.bytes[0] == 'q')) {
            state.showCommands = false;
            return false;
        }
        // Combos stay live while the menu is open, so ^R runs immediately.
        if (control != 0) {
            for (const IdeCommand& command : ideCommands()) {
                if (command.control != 0 && command.control == control) {
                    state.showCommands = false;
                    return ideExecuteCommand(state, command.id);
                }
            }
        }
        return false;
    }

    // ------------------------------------------------------------- help pane
    if (state.showHelp) {
        if (key == platform::KEY_UP &&
            state.helpTop > 0) --state.helpTop;
        else if (key == platform::KEY_DOWN) ++state.helpTop;
        else if (key == platform::KEY_PAGE_UP)
            state.helpTop = state.helpTop > 5 ? state.helpTop - 5 : 0;
        else if (key == platform::KEY_PAGE_DOWN) state.helpTop += 5;
        else if (key == platform::KEY_HOME) state.helpTop = 0;
        else if (key == platform::KEY_END) state.helpTop = 100;
        else if (control == 7 || escape || commandKey ||
                 (key == 0 && event.bytes.size() == 1 && event.bytes[0] == 'q'))
            state.showHelp = false;
        return false;
    }

    // ------------------------------------ Ctrl+Alt+C opens the command menu
    if (commandKey) {
        state.showCommands = true;
        state.commandIndex = 0;
        state.commandTop = 0;
        return false;
    }

    // ---------------------------------------------------------- global keys
    if (control == 17) {  // ^Q is a nano-familiar alias for ^X exit
        return ideExecuteCommand(state, IdeCommandId::Exit);
    }
    for (const IdeCommand& command : ideCommands()) {
        if (command.control != 0 && command.control == control) {
            return ideExecuteCommand(state, command.id);
        }
    }
    switch (control) {
        case 9:  // Tab
            if (inExplorer) {
                state.explorerFocus = false;
            } else {
                ideInsertText(state, "    ");
            }
            return false;
        case 8:   // Backspace (BS)
            if (!inExplorer) ideBackspace(state);
            return false;
        case 13:  // Enter: open the selected file or folder, else newline
            if (inExplorer) {
                ideActivateTreeSelection(state, true);
            } else {
                ideNewline(state);
            }
            return false;
        default:
            break;
    }
    // Backspace as DEL (127), the byte most terminals send.
    if (backspaceKey) {
        if (!inExplorer) ideBackspace(state);
        return false;
    }

    if (key == platform::KEY_CTRL_LEFT) {
        if (ideHasBuffer(state)) state.active = state.active > 0 ? state.active - 1 : state.buffers.size() - 1;
        return false;
    }
    if (key == platform::KEY_CTRL_RIGHT) {
        if (ideHasBuffer(state)) state.active = (state.active + 1) % state.buffers.size();
        return false;
    }

    // A lone Escape moves focus to the explorer, so the arrow keys can be
    // used to navigate the file tree as well as the editor.
    if (escape) {
        state.explorerFocus = true;
        return false;
    }

    if (inExplorer) {
        switch (key) {
            case platform::KEY_UP:
                if (state.treeSelection > 0) --state.treeSelection;
                break;
            case platform::KEY_DOWN:
                if (state.treeSelection + 1 < state.tree.size()) ++state.treeSelection;
                break;
            case platform::KEY_RIGHT:
                // Expand a folder and step into it; files are opened by Enter.
                ideActivateTreeSelection(state, false);
                break;
            case platform::KEY_LEFT:
                ideCollapseOrParent(state);
                break;
            case platform::KEY_HOME:
                state.treeSelection = 0;
                break;
            case platform::KEY_END:
                if (!state.tree.empty()) state.treeSelection = state.tree.size() - 1;
                break;
            case platform::KEY_PAGE_UP:
                state.treeSelection = state.treeSelection > 10 ? state.treeSelection - 10 : 0;
                break;
            case platform::KEY_PAGE_DOWN:
                state.treeSelection = std::min(state.treeSelection + 10,
                                               state.tree.empty() ? 0 : state.tree.size() - 1);
                break;
            default:
                break;
        }
        // Keep the selection inside the visible explorer pane.
        const int paneHeight = ideEditorHeight(state);
        if (state.treeSelection < state.treeTop) {
            state.treeTop = state.treeSelection;
        } else if (state.treeSelection >=
                   state.treeTop + static_cast<std::size_t>(paneHeight)) {
            state.treeTop = state.treeSelection -
                            static_cast<std::size_t>(paneHeight) + 1;
        }
        return false;
    }

    // ---------------------------------------------------------- editor keys
    switch (key) {
        case platform::KEY_UP: ideMoveCursor(state, -1, 0); break;
        case platform::KEY_DOWN: ideMoveCursor(state, 1, 0); break;
        case platform::KEY_LEFT: ideMoveLeft(state); break;
        case platform::KEY_RIGHT: ideMoveRight(state); break;
        case platform::KEY_HOME:
            ideActive(state).cursorColumn = 0;
            break;
        case platform::KEY_END:
            ideActive(state).cursorColumn =
                ideActive(state).lines[ideActive(state).cursorLine].size();
            break;
        case platform::KEY_PAGE_UP:
            ideMoveCursor(state, -std::max(1, state.rows / 2), 0);
            break;
        case platform::KEY_PAGE_DOWN:
            ideMoveCursor(state, std::max(1, state.rows / 2), 0);
            break;
        case platform::KEY_DELETE:
            ideDelete(state);
            break;
        default:
            break;
    }
    if (key == 0 && control == 0 && !event.bytes.empty() &&
        static_cast<unsigned char>(event.bytes[0]) >= 32 &&
        static_cast<unsigned char>(event.bytes[0]) != 127) {
        ideInsertText(state, event.bytes);
    }

    ideEnsureVisible(state, ideEditorHeight(state));
    return false;
}

int launchCliIde(const char* argv0,
                 const std::vector<std::string>& arguments) {
    if (arguments.size() > 1) {
        std::cerr << "lynxer: --cli-ide takes at most one file or folder\n";
        return 1;
    }
    if (!platform::isInteractiveTerminal()) {
        std::cerr << "lynxer: --cli-ide requires an interactive terminal\n";
        std::cerr << "lynxer: stdin and stdout must both be attached to a TTY\n";
        return 1;
    }

    IdeState state;
    state.interpreter = runningExecutable(argv0);
    if (state.interpreter.empty()) {
        std::cerr << "lynxer: could not locate the running executable\n";
        return 1;
    }
    std::error_code pathError;
    const std::filesystem::path absolute =
        std::filesystem::absolute(state.interpreter, pathError);
    if (!pathError) state.interpreter = absolute.string();
    state.version = "Lynxer " +
                    Config::instance().get("version", "0.1.9b2");

    std::error_code cwdError;
    state.root = std::filesystem::current_path(cwdError).string();
    if (cwdError) state.root = ".";

    if (!arguments.empty()) {
        const std::string& target = arguments.front();
        std::error_code error;
        if (std::filesystem::is_directory(target, error)) {
            const std::filesystem::path resolved =
                std::filesystem::absolute(target, error);
            state.root = error ? target : resolved.string();
        } else if (!ideOpenFile(state, target)) {
            std::cerr << "lynxer: " << state.status << '\n';
            return 1;
        }
    }
    ideRefreshTree(state);
    // No file open: start in the explorer so the arrow keys browse files and
    // folders immediately. When a file was passed, focus the editor.
    state.explorerFocus = !ideHasBuffer(state);
    state.status = state.explorerFocus ? "Browse: Up/Down, Enter opens" : "Ready";

    std::string error;
    if (!platform::enableRawInput(error)) {
        std::cerr << "lynxer: " << error << '\n';
        return 1;
    }
    std::cout << "\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H" << std::flush;

    struct Guard {
        ~Guard() {
            std::cout << "\x1b[0m\x1b[?25h\x1b[?1049l" << std::flush;
            platform::disableRawInput();
        }
    } guard;

    while (true) {
        ideRender(state);
        const bool quit = ideHandleKey(state, platform::readKey());
        if (quit) break;
    }
    return 0;
}

// Scaffolds a project into the current working directory. Refuses to
// overwrite any of the files it would create.
int initProject(const std::vector<std::string>& arguments) {
    if (arguments.size() > 1) {
        std::cerr << "lynxer: --init takes at most one project name\n";
        return 1;
    }
    std::string name = arguments.empty() ? std::string() : arguments.front();
    if (name.empty()) {
        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        name = ec ? "lynxer-project" : cwd.filename().string();
    }
    if (name.empty() || name == "." || name == ".." || name == "/" ||
        name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos) {
        std::cerr << "lynxer: invalid project name '" << name << "'\n";
        return 1;
    }

    const char* targets[] = {"README", "src/main.lynx", "lynxer.config",
                             ".gitignore"};
    for (const char* target : targets) {
        std::error_code ec;
        if (std::filesystem::exists(target, ec)) {
            std::cerr << "lynxer: '" << target
                      << "' already exists; --init will not overwrite files\n";
            return 1;
        }
    }

    std::error_code ec;
    std::filesystem::create_directories("src", ec);
    if (ec) {
        std::cerr << "lynxer: cannot create src/: " << ec.message() << '\n';
        return 1;
    }
    std::filesystem::create_directories("build", ec);
    if (ec) {
        std::cerr << "lynxer: cannot create build/: " << ec.message() << '\n';
        return 1;
    }

    const std::string mainSource =
        "fn setup(){}\n"
        "\n"
        "fn main(){\n"
        "    println(\"Hello, Lynxer!\");\n"
        "}\n";
    const std::string config =
        "# Lynxer project configuration.\n"
        "# entry: source file compiled by --build and executed by --run.\n"
        "# name:  executable name produced inside build/.\n"
        "name = " + name + "\n"
        "entry = src/main.lynx\n";
    const std::string readme =
        "# " + name + "\n\n"
        "A Lynxer project.\n\n"
        "Layout:\n"
        "  README            this file\n"
        "  src/main.lynx     program entry point\n"
        "  lynxer.config     project settings (name, entry)\n"
        "  .gitignore        ignores build/\n"
        "  build/            compiled executables\n\n"
        "Commands:\n"
        "  lynxer --build    compile the entry into build/\n"
        "  lynxer --run      build, then run the result\n\n"
        "Edit src/main.lynx; change the entry point in lynxer.config.\n";
    const std::string gitignore = "build/\n";

    std::string error;
    if (!writeProjectFile("README", readme, error) ||
        !writeProjectFile("src/main.lynx", mainSource, error) ||
        !writeProjectFile("lynxer.config", config, error) ||
        !writeProjectFile(".gitignore", gitignore, error)) {
        std::cerr << "lynxer: " << error << '\n';
        return 1;
    }

    std::error_code cwdError;
    const std::string here = std::filesystem::current_path(cwdError).string();
    std::cout << "Initialized Lynxer project '" << name << "' in " << here
              << "\n";
    std::cout << "  README, src/main.lynx, lynxer.config, .gitignore, build/\n";
    std::cout << "Run 'lynxer --run' to build and execute it.\n";
    return 0;
}

// Compiles the configured entry into build/<name>. When `run` is true the
// freshly built executable is launched and its exit code is returned.
int buildProject(bool run) {
    std::unordered_map<std::string, std::string> config;
    std::string error;
    if (!readProjectConfig("lynxer.config", config, error)) {
        std::cerr << "lynxer: " << error << '\n';
        return 1;
    }

    std::string entry;
    if (const auto found = config.find("entry"); found != config.end()) {
        entry = found->second;
    }
    if (entry.empty()) {
        if (const auto found = config.find("main"); found != config.end()) {
            entry = found->second;
        }
    }
    if (entry.empty()) {
        std::cerr << "lynxer: lynxer.config has no 'entry' key "
                     "(expected e.g. 'entry = src/main.lynx')\n";
        return 1;
    }
    if (!endsWith(entry, ".lynx")) {
        std::cerr << "lynxer: project entry '" << entry
                  << "' is not a .lynx file\n";
        return 1;
    }
    {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(entry, ec)) {
            std::cerr << "lynxer: project entry not found: '" << entry
                      << "'\n";
            return 1;
        }
    }

    std::string name;
    if (const auto found = config.find("name"); found != config.end()) {
        name = found->second;
    }
    if (name.empty()) {
        name = baseName(entry);
        if (endsWith(name, ".lynx")) {
            name.resize(name.size() - 5);
        }
    }
    if (name.empty() || name == "." || name == ".." ||
        name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos) {
        std::cerr << "lynxer: invalid project name '" << name
                  << "' in lynxer.config\n";
        return 1;
    }

    // Every build artifact lives under build/.
    std::error_code ec;
    std::filesystem::create_directories("build", ec);
    if (ec) {
        std::cerr << "lynxer: cannot create build/: " << ec.message() << '\n';
        return 1;
    }
    std::string outputPath = "build/" + name;
    const std::string extension = platform::executableExtension();
    if (!extension.empty()) {
        outputPath += "." + extension;
    }

    const int compileStatus =
        compileProgramToExecutable({entry, "-o", outputPath});
    if (compileStatus != 0) {
        return compileStatus;
    }
    if (!run) {
        return 0;
    }

    std::error_code pathError;
    const std::filesystem::path absolute =
        std::filesystem::absolute(outputPath, pathError);
    const std::string target = pathError ? outputPath : absolute.string();
    const int status = platform::spawnProcess({target});
    if (status < 0) {
        std::cerr << "lynxer: could not run '" << outputPath << "'\n";
        return 1;
    }
    return status;
}

// One exported function the emitted library exposes.
struct ExportPlan {
    std::string name;
    ExportSignature signature;
};

// The single-token C spelling of an export type, as written in the generated
// wrapper prototypes.
std::string exportCppType(ExportType type) {
    switch (type) {
        case ExportType::Int64: return "int64_t";
        case ExportType::Float64: return "double";
        case ExportType::CString: return "const char*";
        case ExportType::Bytes: return "const uint8_t*";
        case ExportType::Void: return "void";
    }
    return "void";
}

// The value a wrapper returns when the embedded call fails.
std::string exportZeroValue(ExportType type) {
    switch (type) {
        case ExportType::Int64: return "0";
        case ExportType::Float64: return "0.0";
        case ExportType::CString: return "\"\"";
        case ExportType::Bytes: return "nullptr";
        case ExportType::Void: return "";
    }
    return "";
}

// Generates the C++ source of the emitted shared library: the program archive
// as a byte array, a lazy `lynxer_embed_init`, and one typed `extern "C"`
// wrapper per export that marshals through the embedding C ABI.
std::string generateExportShim(const std::vector<ExportPlan>& exports,
                               const std::vector<std::uint8_t>& archive) {
    std::ostringstream out;
    out << "// Generated by lynxer --emit-library. Do not edit.\n";
    out << "#include \"lynxer.h\"\n\n";
    out << "#include <cstdint>\n";
    out << "#include <cstring>\n";
    out << "#include <vector>\n\n";
    out << "static const unsigned char kLynxerArchive[] = {";
    for (std::size_t index = 0; index < archive.size(); ++index) {
        if (index % 16 == 0) {
            out << "\n    ";
        }
        out << "0x" << std::hex << std::uppercase
            << static_cast<unsigned>(archive[index]) << ',';
    }
    out << "\n};\n\n";
    out << "static LynxerEmbedContext* lynxer_export_context() {\n";
    out << "    static LynxerEmbedContext* context = lynxer_embed_init(\n";
    out << "        kLynxerArchive,\n";
    out << "        static_cast<std::int64_t>(sizeof(kLynxerArchive)));\n";
    out << "    return context;\n";
    out << "}\n\n";
    out << "static thread_local std::vector<std::uint8_t> lynxer_export_bytes;\n";
    out << "static const std::uint8_t* lynxer_frame_bytes(const std::uint8_t* "
           "data,\n";
    out << "                                              std::int64_t length) "
           "{\n";
    out << "    const std::int64_t size = length < 0 ? 0 : length;\n";
    out << "    lynxer_export_bytes.assign(static_cast<std::size_t>(size) + 8, "
           "0);\n";
    out << "    for (int index = 0; index < 8; ++index) {\n";
    out << "        lynxer_export_bytes[static_cast<std::size_t>(index)] =\n";
    out << "            static_cast<std::uint8_t>(\n";
    out << "                (static_cast<std::uint64_t>(size) >> (8 * index)) & "
           "0xFF);\n";
    out << "    }\n";
    out << "    if (data != nullptr && size > 0) {\n";
    out << "        std::memcpy(lynxer_export_bytes.data() + 8, data,\n";
    out << "                    static_cast<std::size_t>(size));\n";
    out << "    }\n";
    out << "    return lynxer_export_bytes.data();\n";
    out << "}\n\n";

    for (const ExportPlan& plan : exports) {
        const ExportType returnType = plan.signature.returnType;
        const bool isVoid = returnType == ExportType::Void;
        out << "extern \"C\" " << exportCppType(returnType) << " " << plan.name
            << "(";
        for (std::size_t index = 0; index < plan.signature.parameters.size();
             ++index) {
            if (index != 0) {
                out << ", ";
            }
            if (plan.signature.parameters[index] == ExportType::Bytes) {
                out << "const std::uint8_t* a" << index << ", std::int64_t a"
                    << index << "_length";
            } else {
                out << exportCppType(plan.signature.parameters[index]) << " a"
                    << index;
            }
        }
        out << ") {\n";
        out << "    LynxerEmbedContext* context = lynxer_export_context();\n";
        if (isVoid) {
            out << "    if (context == nullptr) { return; }\n";
        } else {
            out << "    if (context == nullptr) { return "
                << exportZeroValue(returnType) << "; }\n";
        }
        const std::size_t count = plan.signature.parameters.size();
        if (count > 0) {
            out << "    LynxerFfiArg arguments[" << count << "];\n";
            out << "    std::memset(arguments, 0, sizeof(arguments));\n";
            for (std::size_t index = 0; index < count; ++index) {
                const ExportType type = plan.signature.parameters[index];
                switch (type) {
                    case ExportType::Int64:
                        out << "    arguments[" << index
                            << "].tag = LYNXER_FFI_ARG_INT;\n";
                        out << "    arguments[" << index << "].i = a" << index
                            << ";\n";
                        break;
                    case ExportType::Float64:
                        out << "    arguments[" << index
                            << "].tag = LYNXER_FFI_ARG_FLOAT;\n";
                        out << "    arguments[" << index << "].f = a" << index
                            << ";\n";
                        break;
                    case ExportType::CString:
                        out << "    arguments[" << index
                            << "].tag = LYNXER_FFI_ARG_STRING;\n";
                        out << "    arguments[" << index << "].s = a" << index
                            << ";\n";
                        break;
                    case ExportType::Bytes:
                        out << "    arguments[" << index
                            << "].tag = LYNXER_FFI_ARG_BYTES;\n";
                        out << "    arguments[" << index << "].data = a" << index
                            << ";\n";
                        out << "    arguments[" << index
                            << "].data_length = a" << index << "_length;\n";
                        break;
                    case ExportType::Void:
                        break;
                }
            }
        }
        out << "    LynxerFfiResult result = {};\n";
        out << "    if (lynxer_embed_call(context, \"" << plan.name << "\", ";
        out << (count == 0 ? "nullptr" : "arguments");
        out << ", " << count << ", &result) != 0) {\n";
        if (isVoid) {
            out << "        return;\n";
        } else {
            out << "        return " << exportZeroValue(returnType) << ";\n";
        }
        out << "    }\n";
        switch (returnType) {
            case ExportType::Int64:
                out << "    return result.i;\n";
                break;
            case ExportType::Float64:
                out << "    return result.f;\n";
                break;
            case ExportType::CString:
                out << "    return result.s;\n";
                break;
            case ExportType::Bytes:
                out << "    return lynxer_frame_bytes(result.data, "
                       "result.data_length);\n";
                break;
            case ExportType::Void:
                out << "    return;\n";
                break;
        }
        out << "}\n\n";
    }
    return out.str();
}

#if !defined(_WIN32)
std::string generateExportMap(const std::vector<ExportPlan>& exports) {
    std::ostringstream out;
    out << "{\n  global:\n";
    for (const ExportPlan& plan : exports) {
        out << "    " << plan.name << ";\n";
    }
    out << "  local:\n    *;\n};\n";
    return out.str();
}
#endif

#if defined(_WIN32)
std::string generateExportDefinition(const std::vector<ExportPlan>& exports) {
    std::ostringstream out;
    out << "EXPORTS\n";
    for (const ExportPlan& plan : exports) {
        out << "    " << plan.name << "\n";
    }
    return out.str();
}
#endif

// A C/C++ header for the emitted library: the exported prototypes, so a
// consumer includes one file instead of declaring them by hand. It is
// self-contained — the scalar prototypes only need `<stdint.h>`; a consumer
// that also wants the embedding wire types includes `lynxer.h` itself.
std::string generateExportHeader(const std::vector<ExportPlan>& exports,
                                 const std::string& libraryName) {
    std::string guard = "LYNXER_EXPORT_";
    for (const char character : libraryName) {
        guard += (std::isalnum(static_cast<unsigned char>(character)) != 0)
                     ? static_cast<char>(
                           std::toupper(static_cast<unsigned char>(character)))
                     : '_';
    }
    guard += "_H";

    std::ostringstream out;
    out << "/* Generated by lynxer --emit-library for " << libraryName
        << ". Do not edit. */\n";
    out << "#ifndef " << guard << "\n#define " << guard << "\n\n";
    out << "#include <stdint.h>\n\n";
    out << "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n";
    for (const ExportPlan& plan : exports) {
        out << exportCppType(plan.signature.returnType) << " " << plan.name
            << "(";
        for (std::size_t index = 0; index < plan.signature.parameters.size();
             ++index) {
            if (index != 0) {
                out << ", ";
            }
            if (plan.signature.parameters[index] == ExportType::Bytes) {
                out << "const uint8_t* a" << index << ", int64_t a" << index
                    << "_length";
            } else {
                out << exportCppType(plan.signature.parameters[index]) << " a"
                    << index;
            }
        }
        out << ");\n";
    }
    out << "\n#ifdef __cplusplus\n}\n#endif\n\n#endif\n";
    return out.str();
}

// Spawns `command` and returns its exit status, or -1 if it could not run.
int runProcess(const std::vector<std::string>& command) {
    return platform::spawnProcess(command);
}

// Builds a shared library that exposes a program's `export`s over a C ABI.
int emitLibrary(const std::vector<std::string>& arguments) {
    std::vector<std::string> inputs;
    std::string outputName;
    std::string runtimeOption;
    std::string compiler = "c++";
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string& argument = arguments[index];
        if (argument == "-o" || argument == "--output") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "lynxer: " << argument << " requires a name\n";
                return 1;
            }
            if (!outputName.empty()) {
                std::cerr << "lynxer: the output name was given twice\n";
                return 1;
            }
            outputName = arguments[++index];
            continue;
        }
        if (argument == "--include" || argument == "-i") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "lynxer: " << argument << " requires a file\n";
                return 1;
            }
            inputs.push_back(arguments[++index]);
            continue;
        }
        if (argument == "--runtime") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "lynxer: --runtime requires a library path\n";
                return 1;
            }
            runtimeOption = arguments[++index];
            continue;
        }
        if (argument == "--cc") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "lynxer: --cc requires a compiler command\n";
                return 1;
            }
            compiler = arguments[++index];
            continue;
        }
        if (endsWith(argument, ".lynx") || isNativeLibraryPath(argument)) {
            inputs.push_back(argument);
            continue;
        }
        if (!outputName.empty()) {
            std::cerr << "lynxer: unexpected extra argument '" << argument
                      << "'\n";
            return 1;
        }
        outputName = argument;
    }

    if (inputs.empty() || !endsWith(inputs.front(), ".lynx")) {
        std::cerr << Config::instance().get(
                         "error.emit_library_usage",
                         "lynxer: --emit-library requires a .lynx program "
                         "file, optionally followed by --include <file> "
                         "inputs and an output path")
                  << '\n';
        return 1;
    }

    const std::string& file = inputs.front();
    bool ok = false;
    const std::string source = readFile(file, file, ok);
    if (!ok) {
        return 1;
    }

    ProgramArchive archive;
    archive.mainPath = file;
    archive.mainSource = source;
    const std::vector<std::string> extras(inputs.begin() + 1, inputs.end());
    std::string error;
    if (!collectArchive(file, source, extras, archive, error,
                        /*requireEntryPoints=*/false)) {
        if (error.empty()) {
            return 1;
        }
        return failWith("error.emit_library_failed",
                        "lynxer: emit-library failed: {0}", error);
    }

    std::vector<ExportPlan> exports;
    try {
        Lexer lexer(source, file);
        Parser parser(lexer.scan(), file);
        parser.parseProgram(/*requireEntryPoints=*/false,
                            /*resetTypeRegistry=*/true,
                            /*emitDeprecationWarnings=*/false);
        for (const ExportRecord& record : parser.exports()) {
            ExportPlan plan;
            plan.name = record.name;
            std::string reason;
            if (!parseExportSignature(record.signature, plan.signature,
                                      reason)) {
                std::cerr << "lynxer: " << file << ':' << record.line << ':'
                          << record.column << ": ["
                          << diagnosticCodeForMessage(reason) << "] " << reason
                          << '\n';
                return 1;
            }
            exports.push_back(std::move(plan));
        }
    } catch (const SourceError& parseError) {
        std::cerr << "lynxer: " << file << ':' << parseError.line << ':'
                  << parseError.column << ": [" << parseError.code << "] "
                  << parseError.what() << '\n';
        return 1;
    }
    if (exports.empty()) {
        return failWith("error.emit_library_failed",
                        "lynxer: emit-library failed: {0}",
                        "the program declares no exported functions");
    }

    // Locate the embedding runtime: an explicit path, the environment, then
    // the interpreter's own directory (development and installed layouts).
    std::string runtimePath = runtimeOption;
    if (runtimePath.empty()) {
        const char* fromEnvironment = std::getenv("LYNXER_RUNTIME");
        if (fromEnvironment != nullptr && *fromEnvironment != '\0') {
            runtimePath = fromEnvironment;
        }
    }
    const std::string executableDir = executableDirectory();
    const std::string runtimeName = embeddingRuntimeFileName();
    const std::vector<std::string> candidates = {
        executableDir + "/" + runtimeName,
        executableDir + "/lib/lynxer/" + runtimeName,
        executableDir + "/../lib/lynxer/" + runtimeName,
    };
    if (runtimePath.empty()) {
        for (const std::string& candidate : candidates) {
            if (std::filesystem::exists(candidate)) {
                runtimePath = candidate;
                break;
            }
        }
    }
    if (runtimePath.empty()) {
        return failWith("error.emit_library_failed",
                        "lynxer: emit-library failed: {0}",
                        "cannot find " + runtimeName + "; build Lynxer first with "
                        "`make buildLynxer` (it needs the compiled interpreter "
                        "and runtime), or pass --runtime <path>");
    }
    std::error_code runtimeError;
    if (!std::filesystem::is_regular_file(runtimePath, runtimeError)) {
        return failWith("error.emit_library_failed",
                        "lynxer: emit-library failed: {0}",
                        runtimeName + " was not found at '" + runtimePath +
                            "'; build Lynxer first with `make buildLynxer` "
                            "(the compiled interpreter and runtime are "
                            "required)");
    }
    const std::string runtimeDir =
        std::filesystem::path(runtimePath).parent_path().string();

    std::vector<std::string> includeDirs = {runtimeDir, executableDir,
                                            executableDir + "/include"};
    if (const char* prefix = std::getenv("LYNXER_PREFIX")) {
        if (*prefix != '\0') {
            includeDirs.push_back(std::string(prefix) + "/include");
        }
    }

    std::string outputPath = outputName;
    if (outputPath.empty()) {
        outputPath = baseName(file);
        if (endsWith(outputPath, ".lynx")) {
            outputPath.resize(outputPath.size() - 5);
        }
        outputPath += ".";
        outputPath += platform::libraryExtension();
    }

    // Generate the wrapper source and platform export list in a private directory.
    std::string directory;
    std::string temporaryError;
    if (!platform::makeTemporaryDirectory(directory, temporaryError)) {
        return failWith("error.emit_library_failed",
                        "lynxer: emit-library failed: {0}", temporaryError);
    }
    const std::string shimPath = directory + "/shim.cpp";
    const std::string mapPath = directory +
#if defined(_WIN32)
                                "/exports.def";
#else
                                "/export.map";
#endif
    {
        std::ofstream shim(shimPath, std::ios::binary | std::ios::trunc);
        shim << generateExportShim(exports, makeBundleBody(archive));
        std::ofstream map(mapPath, std::ios::binary | std::ios::trunc);
        map <<
#if defined(_WIN32)
            generateExportDefinition(exports);
#else
            generateExportMap(exports);
#endif
        if (!shim || !map) {
            std::filesystem::remove_all(directory);
            return failWith("error.emit_library_failed",
                            "lynxer: emit-library failed: {0}",
                            "cannot write the generated shim");
        }
    }

    std::vector<std::string> command = {compiler, "-std=c++17", "-O2"};
#if defined(_WIN32)
    const std::string importLibraryPath = runtimePath + ".a";
    std::error_code importLibraryError;
    if (!std::filesystem::is_regular_file(importLibraryPath,
                                          importLibraryError)) {
        std::filesystem::remove_all(directory);
        return failWith("error.emit_library_failed",
                        "lynxer: emit-library failed: {0}",
                        "cannot find MinGW import library '" +
                            importLibraryPath + "' beside the runtime DLL");
    }
    const std::string outputImportLibrary = outputPath + ".a";
    command.insert(command.end(), {"-shared", shimPath, "-o", outputPath,
                                   importLibraryPath, mapPath,
                                   "-Wl,--exclude-all-symbols",
                                   "-Wl,--out-implib=" + outputImportLibrary});
#else
    command.insert(command.end(), {"-fPIC", "-shared", shimPath, "-o",
                                   outputPath, runtimePath});
#endif
    for (const std::string& includeDir : includeDirs) {
        command.push_back("-I" + includeDir);
    }
#if !defined(_WIN32)
    command.push_back("-Wl,-z,origin");
    command.push_back("-Wl,-rpath," + runtimeDir);
    command.push_back("-Wl,-rpath,$ORIGIN");
    command.push_back("-Wl,--version-script=" + mapPath);
    command.push_back("-Wl,-soname," + baseName(outputPath));
#endif

    const int status = runProcess(command);
    std::filesystem::remove_all(directory);
    if (status != 0) {
        return failWith("error.emit_library_failed",
                        "lynxer: emit-library failed: {0}",
                        "the C++ compiler exited with status " +
                            std::to_string(status));
    }

    // The header travels with the library so a consumer includes one file
    // instead of declaring the exported prototypes by hand.
    const std::string headerPath =
        std::filesystem::path(outputPath).replace_extension(".h").string();
    {
        std::ofstream header(headerPath, std::ios::binary | std::ios::trunc);
        header << generateExportHeader(exports, baseName(outputPath));
        if (!header) {
            return failWith("error.emit_library_failed",
                            "lynxer: emit-library failed: {0}",
                            "cannot write the header '" + headerPath + "'");
        }
    }
    std::cout << Config::instance().format("status.emit_library_ok",
                                           "Emitted library: {0}", "{0}",
                                           outputPath)
              << '\n';
    std::cout << Config::instance().format("status.emit_header_ok",
                                           "Emitted header: {0}", "{0}",
                                           headerPath)
              << '\n';
    return 0;
}

// Runs the program carried by a compiled executable.
int runCompiledPayload(const std::vector<uint8_t>& payload) {    ProgramArchive archive;
    if (!decodeProgramArchive(payload, archive)) {
        std::cerr << Config::instance().get(
                         "error.payload_invalid",
                         "lynxer: the embedded program payload is invalid")
                  << '\n';
        return 1;
    }

    std::map<std::string, std::string> libraries;
    std::map<std::string, std::string> assets;
    std::string error;
    if (!materializeBundle(archive.modules, libraries, assets, error)) {
        return failWith("error.compile_failed", "lynxer: compile failed: {0}",
                        error);
    }
    setBundledAssets(std::move(assets));

    std::map<std::string, std::string> sources;
    for (const auto& module : archive.modules) {
        if (!module.library.empty()) {
            continue;
        }
        const std::string bare = baseName(module.name);
        sources[module.name] = module.source;
        sources[bare] = module.source;
        // Register both the bare module name and its `.lynx` file name so
        // `import("name")` resolves whether or not the extension was written.
        if (endsWith(bare, ".lynx")) {
            sources[bare.substr(0, bare.size() - 5)] = module.source;
        } else {
            sources[bare + ".lynx"] = module.source;
        }
    }
    setEmbeddedModuleSources(std::move(sources));
    setEmbeddedModuleLibraries(std::move(libraries));
    return runProgram(archive.mainPath, archive.mainSource);
}

} // namespace

int shellMain(int argc, char** argv) {
    platform::configureStandardStreams();
    installInterruptHandler();

    // A compiled executable runs its embedded program directly.
    std::vector<uint8_t> selfPayload;
    if (readSelfPayload(selfPayload)) {
        // A compiled executable is the program, so its whole command line is
        // the program's arguments.
        programArguments.assign(argv, argv + argc);
        const int exitCode = runCompiledPayload(selfPayload);
        if (interruptRequested()) {
            std::cout << '\n';
            return 130;
        }
        return exitCode;
    }

    std::vector<std::string> args(argv + 1, argv + argc);
    // `--no-opt` disables the AST optimization pass for this run. It is a
    // run-time switch: a compiled executable ignores its command line and
    // always optimizes.
    for (auto it = args.begin(); it != args.end();) {
        if (*it == "--no-opt" || *it == "-no-opt") {
            setOptimizerEnabled(false);
            it = args.erase(it);
        } else {
            ++it;
        }
    }
    if (args.empty() || args[0] == "-h" || args[0] == "--help") {
        printUsage();
        return 0;
    }
    if (args[0] == "-v" || args[0] == "--version" || args[0] == "-version" ||
        args[0] == "--v" || args[0] == "--ver" || args[0] == "-ver") {
        printVersion();
        return 0;
    }
    if (args[0] == "-easterEgg" || args[0] == "--easterEgg" ||
        args[0] == "--idklmao" || args[0] == "-wnwnerbcyunwrbygnubeuyxnqybxun") {
        printEasterEgg();
        return 0;
    }
    if (args[0] == "--gui-ide") {
        return launchGuiIde(
            argv[0],
            std::vector<std::string>(args.begin() + 1, args.end()));
    }
    if (args[0] == "--cli-ide" || args[0] == "-cli-ide") {
        return launchCliIde(
            argv[0],
            std::vector<std::string>(args.begin() + 1, args.end()));
    }
    if (args[0] == "--list-stdlibs" || args[0] == "--stdlibs" ||
        args[0] == "-stdlibs" || args[0] == "-list-stdlibs") {
        return listStdlibs();
    }
    if (args[0] == "--install") {
        return installBinary(argv[0]);
    }
    if (args[0] == "--uninstall") {
        return uninstallBinary();
    }
    if (args[0] == "--init") {
        return initProject(
            std::vector<std::string>(args.begin() + 1, args.end()));
    }
    if (args[0] == "--build" || args[0] == "--run") {
        if (args.size() != 1) {
            std::cerr << "lynxer: " << args[0]
                      << " takes no arguments\n";
            return 1;
        }
        return buildProject(args[0] == "--run");
    }
    if (args[0] == "--validate-executeable" ||
        args[0] == "--validate-executable") {
        return validateInterpreter();
    }
    if (args[0] == "--benchmark-compile" || args[0] == "--bench-compile") {
        return removedFlag(args[0], "lynxer --compile");
    }
    if (args[0] == "--compile" || args[0] == "-c" || args[0] == "--c" ||
        args[0] == "-compile") {
        return compileProgramToExecutable(
            std::vector<std::string>(args.begin() + 1, args.end()));
    }
    if (args[0] == "--emit-library" || args[0] == "--shared-library" ||
        args[0] == "-emit-library") {
        return emitLibrary(
            std::vector<std::string>(args.begin() + 1, args.end()));
    }
    if (args[0] == "--view-bytecode" || args[0] == "--inspect-bytecode" ||
        args[0] == "--disasm") {
        return removedFlag(args[0], "lynxer --compile");
    }
    if (args[0] == "--no-cache") {
        return removedFlag(args[0], "lynxer --compile");
    }
    if (args[0] == "--format" || args[0] == "--format-oneline") {
        if (args.size() != 2) {
            std::cerr << "lynxer: " << args[0]
                      << " requires exactly one file argument\n";
            return 1;
        }
        bool ok = false;
        const std::string source = readFile(args[1], args[1], ok);
        if (!ok) {
            return 1;
        }
        return formatFile(args[1], source, args[0] == "--format-oneline");
    }
    if (args[0] == "--ast") {
        if (args.size() != 2) {
            std::cerr << Config::instance().format(
                                 "error.requires_one_file",
                                 "lynxer: {0} requires exactly one file argument",
                                 "{0}", args[0])
                      << '\n';
            return 1;
        }
        bool ok = false;
        const std::string source = readFile(args[1], args[1], ok);
        if (!ok) {
            return 1;
        }
        return astFile(args[1], source);
    }
    if (args[0] == "--lint") {
        if (args.size() != 2) {
            std::cerr << Config::instance().format(
                                 "error.requires_one_file",
                                 "lynxer: {0} requires exactly one file argument",
                                 "{0}", args[0])
                      << '\n';
            return 1;
        }
        bool ok = false;
        const std::string source = readFile(args[1], args[1], ok);
        if (!ok) {
            return 1;
        }
        return lintFile(args[1], source);
    }
    if (args[0] == "--debug") {
        if (args.size() < 2) {
            std::cerr << "lynxer: --debug requires a source file\n";
            return 1;
        }
        const std::string& display = args[1];
        bool ok = false;
        const std::string source = readFile(display, display, ok);
        if (!ok) return 1;
        programArguments.assign(1, display);
        programArguments.insert(programArguments.end(), args.begin() + 2,
                                args.end());
        setDebuggerEnabled(true);
        const int exitCode = runProgram(display, source);
        setDebuggerEnabled(false);
        if (interruptRequested()) {
            std::cout << '\n';
            return 130;
        }
        return exitCode;
    }

    const std::string& display = args[0];
    if (display.size() > 6 &&
        display.compare(display.size() - 6, 6, ".lynxc") == 0) {
        std::cerr << Config::instance().get(
                         "error.bytecode_removed",
                         "lynxer: bytecode files are no longer supported; "
                         "compile the .lynx source with --compile instead")
                  << '\n';
        return 1;
    }

    bool ok = false;
    const std::string source = readFile(display, display, ok);
    if (!ok) {
        return 1;
    }
    // The program's arguments are its own command line: the script path
    // followed by everything after it.
    programArguments.assign(1, display);
    programArguments.insert(programArguments.end(), args.begin() + 1,
                            args.end());
    const int exitCode = runProgram(display, source);
    if (interruptRequested()) {
        std::cout << '\n';
        return 130;
    }
    return exitCode;
}

} // namespace lynxer
