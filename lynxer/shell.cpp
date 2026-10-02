#include "shell.hpp"

#include "bundle.hpp"
#include "config.hpp"
#include "error.hpp"
#include "exports.hpp"
#include "formatter.hpp"
#include "interrupt.hpp"
#include "lexer.hpp"
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
#include <iostream>
#include <map>
#include <sstream>
#include <string>
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

void printUsage() {
    std::cout << "\n";
    std::cout << "Usage:\n";
    std::cout << "  lynxer <file.lynx>                          Run a Lynxer source file\n";
    std::cout << "  lynxer --no-opt <file.lynx>                 Run without the AST optimizer\n";
    std::cout << "  lynxer --lint <file.lynx>                   Check Lynxer syntax without running it\n";
    std::cout << "  lynxer --ast <file.lynx>                    Parse and print the abstract syntax tree\n";
    std::cout << "  lynxer --format <file.lynx>                 Rewrite a file with canonical spacing\n";
    std::cout << "  lynxer --format-oneline <file.lynx>         Collapse a file onto one physical line\n";
    std::cout << "  lynxer --compile <a.lynx> [options] [name]  Compile input files into one executable\n";
    std::cout << "  lynxer --bundle <a.lynx> [options] [name]   Alias of --compile\n";
    std::cout << "      --include <file>                         Embed a module, native library or data file\n";
    std::cout << "      -o, --output <name>                      Name the output executable\n";
    std::cout << "  lynxer --emit-library <a.lynx> [options] [out.so]\n";
    std::cout << "                                              Build a .so exporting the program's `export`s, plus a matching .h\n";
    std::cout << "      --include <file>                         Embed a module, native library or data file\n";
    std::cout << "      --runtime <liblynxer.so>                 Use an explicit embedding runtime\n";
    std::cout << "      --cc <compiler>                          C++ compiler used to build the library\n";
    std::cout << "  lynxer --validate-executeable               Run the interpreter self-check\n";
    std::cout << "  lynxer --version                            Print version\n";
    std::cout << "  lynxer --list-stdlibs                       List available Lynxer stdlib modules\n";
    std::cout << "  lynxer --install                            Install into /usr/lib/lynxer and link /usr/bin/lynxer\n";
    std::cout << "  lynxer --uninstall                          Remove the installed interpreter\n";
    std::cout << "\n";
    std::cout << "Removed with the bytecode backend (use --compile):\n";
    std::cout << "  --view-bytecode, --benchmark-compile, --no-cache\n";
    std::cout << "\n";
}

void printVersion() {
    const Config& config = Config::instance();
    std::cout << config.format("version.line", "Lynxer {0}", "{0}",
                               config.get("version", "0.1.8.2"))
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
        std::filesystem::path(executableDirectory()) / "stdlib";
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
    Lexer lexer(source, display);
    Parser parser(lexer.scan(), display);
    try {
        parser.parseProgram();
    } catch (const SourceError& error) {
        std::cerr << "lynxer: " << describeSourceError(display, error) << ": "
                  << error.what() << '\n';
        return 1;
    }
    std::cout << Config::instance().format("status.lint_ok", "Lint OK: {0}",
                                           "{0}", display)
              << '\n';
    return 0;
}

// Parses `display` and prints its AST without executing it.
int astFile(const std::string& display, const std::string& source) {
    try {
        // The lexer keeps a reference to the source, so it must outlive it.
        Lexer lexer(source, display);
        Parser parser(lexer.scan(), display);
        const auto functions = parser.parseProgram();
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
        std::cerr << "lynxer: " << describeSourceError(display, error) << ": "
                  << error.what() << '\n';
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
        std::cerr << "lynxer: " << describeSourceError(display, error) << ": "
                  << error.what() << '\n';
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

// The prefix `--install` writes into: /usr by default, overridable so the
// installer can be exercised without root (the test suite uses a temp prefix).
std::string installPrefix() {
    const char* environment = std::getenv("LYNXER_PREFIX");
    std::string prefix =
        (environment != nullptr && environment[0] != '\0') ? environment : "/usr";
    while (prefix.size() > 1 && prefix.back() == '/') {
        prefix.pop_back();
    }
    return prefix;
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
            sourceDirectory / "liblynxer.so";
        std::error_code sharedError;
        if (std::filesystem::is_regular_file(sharedLibrary, sharedError)) {
            if (!sameFile(sharedLibrary, libDir / "liblynxer.so")) {
                std::filesystem::copy_file(
                    sharedLibrary, libDir / "liblynxer.so",
                    std::filesystem::copy_options::overwrite_existing);
            }
        } else {
            std::cerr << "lynxer: note: liblynxer.so not found; install it to "
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
            auto functions = parser.parseProgram();
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
        auto functions = parser.parseProgram();
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
        std::cerr << "lynxer: " << describeSourceError(display, error) << ": "
                  << error.what() << '\n';
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
    };
    std::vector<Pending> queue;
    queue.push_back(Pending{mainPath, mainSource});
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
        queue.push_back(Pending{resolved, module.source});
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
                                     requireEntryPoints);
        } catch (const SourceError& importError) {
            std::cerr << "lynxer: " << current.path << ':' << importError.line
                      << ':' << importError.column << ": " << importError.what()
                      << '\n';
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
            queue.push_back(Pending{resolved, module.source});
            archive.modules.push_back(std::move(module));
        }
    }
    return true;
}

int failWith(const char* key, const char* fallback, const std::string& value) {
    std::cerr << Config::instance().format(key, fallback, "{0}", value) << '\n';
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

std::string generateExportMap(const std::vector<ExportPlan>& exports) {
    std::ostringstream out;
    out << "{\n  global:\n";
    for (const ExportPlan& plan : exports) {
        out << "    " << plan.name << ";\n";
    }
    out << "  local:\n    *;\n};\n";
    return out.str();
}

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
        parser.parseProgram(/*requireEntryPoints=*/false);
        for (const ExportRecord& record : parser.exports()) {
            ExportPlan plan;
            plan.name = record.name;
            std::string reason;
            if (!parseExportSignature(record.signature, plan.signature,
                                      reason)) {
                std::cerr << "lynxer: " << file << ':' << record.line << ':'
                          << record.column << ": " << reason << '\n';
                return 1;
            }
            exports.push_back(std::move(plan));
        }
    } catch (const SourceError& parseError) {
        std::cerr << "lynxer: " << file << ':' << parseError.line << ':'
                  << parseError.column << ": " << parseError.what() << '\n';
        return 1;
    }
    if (exports.empty()) {
        return failWith("error.emit_library_failed",
                        "lynxer: emit-library failed: {0}",
                        "the program declares no exported functions");
    }

    // Locate `liblynxer.so`: an explicit path, the environment, then the
    // interpreter's own directory (dev tree and installed layouts).
    std::string runtimePath = runtimeOption;
    if (runtimePath.empty()) {
        const char* fromEnvironment = std::getenv("LYNXER_RUNTIME");
        if (fromEnvironment != nullptr && *fromEnvironment != '\0') {
            runtimePath = fromEnvironment;
        }
    }
    const std::string executableDir = executableDirectory();
    const std::vector<std::string> candidates = {
        executableDir + "/liblynxer.so",
        executableDir + "/lib/lynxer/liblynxer.so",
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
                        "cannot find liblynxer.so; build Lynxer first with "
                        "`make buildLynxer` (it needs the compiled interpreter "
                        "and runtime), or pass --runtime <path>");
    }
    std::error_code runtimeError;
    if (!std::filesystem::is_regular_file(runtimePath, runtimeError)) {
        return failWith("error.emit_library_failed",
                        "lynxer: emit-library failed: {0}",
                        "liblynxer.so was not found at '" + runtimePath +
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

    // Generate the wrapper source and version script in a private directory.
    std::string directory;
    std::string temporaryError;
    if (!platform::makeTemporaryDirectory(directory, temporaryError)) {
        return failWith("error.emit_library_failed",
                        "lynxer: emit-library failed: {0}", temporaryError);
    }
    const std::string shimPath = directory + "/shim.cpp";
    const std::string mapPath = directory + "/export.map";
    {
        std::ofstream shim(shimPath, std::ios::binary | std::ios::trunc);
        shim << generateExportShim(exports, makeBundleBody(archive));
        std::ofstream map(mapPath, std::ios::binary | std::ios::trunc);
        map << generateExportMap(exports);
        if (!shim || !map) {
            std::filesystem::remove_all(directory);
            return failWith("error.emit_library_failed",
                            "lynxer: emit-library failed: {0}",
                            "cannot write the generated shim");
        }
    }

    std::vector<std::string> command = {
        compiler, "-std=c++17", "-O2", "-fPIC", "-shared", shimPath, "-o",
        outputPath, runtimePath};
    for (const std::string& includeDir : includeDirs) {
        command.push_back("-I" + includeDir);
    }
    command.push_back("-Wl,-z,origin");
    command.push_back("-Wl,-rpath," + runtimeDir);
    command.push_back("-Wl,-rpath,$ORIGIN");
    command.push_back("-Wl,--version-script=" + mapPath);
    command.push_back("-Wl,-soname," + baseName(outputPath));

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
        args[0] == "--v") {
        printVersion();
        return 0;
    }
    if (args[0] == "-easterEgg" || args[0] == "--easterEgg" ||
        args[0] == "--idklmao" || args[0] == "-wnwnerbcyunwrbygnubeuyxnqybxun") {
        printEasterEgg();
        return 0;
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
    if (args[0] == "--validate-executeable" ||
        args[0] == "--validate-executable") {
        return validateInterpreter();
    }
    if (args[0] == "--benchmark-compile" || args[0] == "--bench-compile") {
        return removedFlag(args[0], "lynxer --compile");
    }
    if (args[0] == "--compile" || args[0] == "-c" || args[0] == "--c" ||
        args[0] == "-compile" || args[0] == "--bundle" ||
        args[0] == "-bundle") {
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
