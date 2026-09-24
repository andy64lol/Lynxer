#include "shell.hpp"

#include "bundle.hpp"
#include "config.hpp"
#include "error.hpp"
#include "formatter.hpp"
#include "interrupt.hpp"
#include "lexer.hpp"
#include "optimizer.hpp"
#include "parser.hpp"
#include "runtime.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace clynxer {

namespace {

std::string readFile(const std::string& path, const std::string& display,
                     bool& ok) {
    std::ifstream input(path);
    if (!input) {
        std::cerr << Config::instance().format("error.file_not_found",
                                               "clynxer: file not found: '{0}'",
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

void printUsage() {
    std::cout << "\n";
    std::cout << "Usage:\n";
    std::cout << "  clynxer <file.lynx>                          Run a Lynxer source file\n";
    std::cout << "  clynxer --no-opt <file.lynx>                 Run without the AST optimizer\n";
    std::cout << "  clynxer --lint <file.lynx>                   Check Lynxer syntax without running it\n";
    std::cout << "  clynxer --ast <file.lynx>                    Parse and print the abstract syntax tree\n";
    std::cout << "  clynxer --format <file.lynx>                 Rewrite a file with canonical spacing\n";
    std::cout << "  clynxer --format-oneline <file.lynx>         Collapse a file onto one physical line\n";
    std::cout << "  clynxer --compile <a.lynx> [options] [name]  Compile input files into one executable\n";
    std::cout << "  clynxer --bundle <a.lynx> [options] [name]   Alias of --compile\n";
    std::cout << "      --include <file>                         Embed a module, native library or data file\n";
    std::cout << "      -o, --output <name>                      Name the output executable\n";
    std::cout << "  clynxer --validate-executeable               Run the interpreter self-check\n";
    std::cout << "  clynxer --version                            Print version\n";
    std::cout << "  clynxer --list-stdlibs                       List available Lynxer stdlib modules\n";
    std::cout << "  clynxer --install                            Install the executable as /usr/bin/lynxer\n";
    std::cout << "  clynxer --uninstall                          Remove /usr/bin/lynxer\n";
    std::cout << "\n";
    std::cout << "Removed with the bytecode backend (use --compile):\n";
    std::cout << "  --view-bytecode, --benchmark-compile, --no-cache\n";
    std::cout << "\n";
}

void printVersion() {
    const Config& config = Config::instance();
    std::cout << config.format("version.line", "CLynxer {0}", "{0}",
                               config.get("version", "0.1.8"))
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
    Parser parser(lexer.scan());
    try {
        parser.parseProgram();
    } catch (const SourceError& error) {
        std::cerr << "clynxer: " << display << ':' << error.line << ':'
                  << error.column << ": " << error.what() << '\n';
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
        Parser parser(lexer.scan());
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
        std::cerr << "clynxer: " << display << ':' << error.line << ':'
                  << error.column << ": " << error.what() << '\n';
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
            std::cerr << "clynxer: could not write '" << display << "'\n";
            return 1;
        }
    } catch (const SourceError& error) {
        std::cerr << "clynxer: " << display << ':' << error.line << ':'
                  << error.column << ": " << error.what() << '\n';
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
#if defined(__linux__)
    std::vector<char> buffer(4096);
    const ssize_t length =
        ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length > 0) {
        return std::string(buffer.data(), static_cast<std::size_t>(length));
    }
#endif
    return argv0 != nullptr ? std::string(argv0) : std::string();
}

int installBinary(const char* argv0) {
    const std::filesystem::path target = "/usr/bin/lynxer";
    const std::string self = runningExecutable(argv0);
    if (self.empty()) {
        std::cerr << "clynxer: could not locate the running executable\n";
        return 1;
    }
    try {
        std::filesystem::copy_file(self, target,
                                   std::filesystem::copy_options::overwrite_existing);
        std::filesystem::permissions(
            target,
            std::filesystem::perms::owner_all |
                std::filesystem::perms::group_read |
                std::filesystem::perms::group_exec |
                std::filesystem::perms::others_read |
                std::filesystem::perms::others_exec,
            std::filesystem::perm_options::replace);
    } catch (const std::filesystem::filesystem_error& error) {
        std::cerr << "clynxer: install failed: " << error.what() << '\n';
        std::cerr << "clynxer: re-run with permission to write " << target
                  << " (for example with sudo)\n";
        return 1;
    }
    std::cout << "Installed " << target << "\n";
    std::cout << "Keep the matching stdlib/ directory next to the installed "
                 "binary so imports resolve.\n";
    return 0;
}

int uninstallBinary() {
    const std::filesystem::path target = "/usr/bin/lynxer";
    std::error_code error;
    if (!std::filesystem::remove(target, error)) {
        std::cerr << "clynxer: could not remove " << target << ": "
                  << (error ? error.message() : std::string("no such file"))
                  << '\n';
        return 1;
    }
    std::cout << "Removed " << target << '\n';
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
            Parser parser(lexer.scan());
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
// CLYNXER_OPT_REPORT is set, so no program output ever depends on it.
void reportOptimization() {
    if (std::getenv("CLYNXER_OPT_REPORT") == nullptr) {
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
        Parser parser(lexer.scan());
        auto functions = parser.parseProgram();
        if (optimizerEnabled()) {
            optimizeProgram(functions, optimizationStats());
        }
        Environment environment;
        const std::size_t slash = display.find_last_of('/');
        if (slash != std::string::npos) {
            environment.setSourceDirectory(display.substr(0, slash));
        }
        executeProgram(functions, environment);
        reportOptimization();
        return 0;
    } catch (const InterruptError&) {
        return 130;
    } catch (const SourceError& error) {
        std::cerr << "clynxer: " << display << ':' << error.line << ':'
                  << error.column << ": " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << Config::instance().format(
                         "error.interpreter_failure",
                         "clynxer: interpreter failure in '{0}': {1}", "{0}",
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
                    ProgramArchive& archive, std::string& error) {
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
        const bool native = endsWith(given, ".so");
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
            imports = collectImports(current.source, current.path);
        } catch (const SourceError& importError) {
            std::cerr << "clynxer: " << current.path << ':' << importError.line
                      << ':' << importError.column << ": " << importError.what()
                      << '\n';
            return false;
        }

        for (const auto& import : imports) {
            const bool native = endsWith(import.path, ".so");
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
    std::cerr << "clynxer: '" << flag
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
                std::cerr << "clynxer: " << argument << " requires a name\n";
                return 1;
            }
            if (!outputName.empty()) {
                std::cerr << "clynxer: the output name was given twice\n";
                return 1;
            }
            outputName = arguments[++index];
            continue;
        }
        // --include takes any file: a .lynx module, a native .so, or a data
        // file that the program reads with bundledFile().
        if (argument == "--include" || argument == "-i") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "clynxer: " << argument << " requires a file\n";
                return 1;
            }
            inputs.push_back(arguments[++index]);
            continue;
        }
        // Anything that looks like a source or library file is an input; a
        // bare name is the output.
        if (endsWith(argument, ".lynx") || endsWith(argument, ".so")) {
            inputs.push_back(argument);
            continue;
        }
        if (!outputName.empty()) {
            std::cerr << "clynxer: unexpected extra argument '" << argument
                      << "'\n";
            return 1;
        }
        outputName = argument;
    }

    if (inputs.empty()) {
        std::cerr << Config::instance().get(
                         "error.compile_usage",
                         "clynxer: --compile requires a .lynx file, optionally "
                         "followed by --include <file> inputs and an output "
                         "name")
                  << '\n';
        return 1;
    }
    if (!endsWith(inputs.front(), ".lynx")) {
        std::cerr << "clynxer: the first input must be a .lynx program file\n";
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
        return failWith("error.compile_failed", "clynxer: compile failed: {0}",
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
        return failWith("error.compile_failed", "clynxer: compile failed: {0}",
                        error);
    }
    std::cout << Config::instance().format("status.compile_ok",
                                           "Compiled: {0}", "{0}", outputPath)
              << '\n';
    return 0;
}

// Runs the program carried by a compiled executable.
int runCompiledPayload(const std::vector<uint8_t>& payload) {
    ProgramArchive archive;
    if (!decodeProgramArchive(payload, archive)) {
        std::cerr << Config::instance().get(
                         "error.payload_invalid",
                         "clynxer: the embedded program payload is invalid")
                  << '\n';
        return 1;
    }

    std::map<std::string, std::string> libraries;
    std::map<std::string, std::string> assets;
    std::string error;
    if (!materializeBundle(archive.modules, libraries, assets, error)) {
        return failWith("error.compile_failed", "clynxer: compile failed: {0}",
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
        return removedFlag(args[0], "clynxer --compile");
    }
    if (args[0] == "--compile" || args[0] == "-c" || args[0] == "--c" ||
        args[0] == "-compile" || args[0] == "--bundle" ||
        args[0] == "-bundle") {
        return compileProgramToExecutable(
            std::vector<std::string>(args.begin() + 1, args.end()));
    }
    if (args[0] == "--view-bytecode" || args[0] == "--inspect-bytecode" ||
        args[0] == "--disasm") {
        return removedFlag(args[0], "clynxer --compile");
    }
    if (args[0] == "--no-cache") {
        return removedFlag(args[0], "clynxer --compile");
    }
    if (args[0] == "--format" || args[0] == "--format-oneline") {
        if (args.size() != 2) {
            std::cerr << "clynxer: " << args[0]
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
                                 "clynxer: {0} requires exactly one file argument",
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
                                 "clynxer: {0} requires exactly one file argument",
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
                         "clynxer: bytecode files are no longer supported; "
                         "compile the .lynx source with --compile instead")
                  << '\n';
        return 1;
    }

    bool ok = false;
    const std::string source = readFile(display, display, ok);
    if (!ok) {
        return 1;
    }
    const int exitCode = runProgram(display, source);
    if (interruptRequested()) {
        std::cout << '\n';
        return 130;
    }
    return exitCode;
}

} // namespace clynxer
