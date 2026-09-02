#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cctype>
#include <algorithm>
#include <limits>
#include <type_traits>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <thread>
#include <mutex>
#include <exception>
#include <memory>
#include <new>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif
#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace {

struct ReferenceCell {
    PyObject *value;
};

struct NativeThread {
    uintptr_t handle{0};
    std::thread worker;
    std::atomic<bool> alive{true};
    std::atomic<bool> done{false};
    bool detached{false};
    bool joined{false};
    std::string status{"running"};
    std::mutex statusMutex;
    std::mutex lifecycleMutex;
};

std::mutex nativeThreadsMutex;
std::unordered_map<uintptr_t, std::shared_ptr<NativeThread>> nativeThreads;
uintptr_t nextNativeThreadHandle = 1;

std::mutex referenceCellsMutex;
std::unordered_map<uintptr_t, std::unique_ptr<ReferenceCell>> referenceCells;
uintptr_t nextReferenceHandle = 1;

bool pointerFromPy(PyObject *obj, void **out) {
    if (!PyLong_Check(obj)) {
        PyErr_SetString(PyExc_TypeError, "native pointer must be an integer");
        return false;
    }
    unsigned long long value = PyLong_AsUnsignedLongLong(obj);
    if (PyErr_Occurred()) {
        return false;
    }
    if (value > static_cast<unsigned long long>(
                    std::numeric_limits<uintptr_t>::max())) {
        PyErr_SetString(PyExc_OverflowError,
                        "native pointer does not fit this process pointer width");
        return false;
    }
    *out = reinterpret_cast<void *>(static_cast<uintptr_t>(value));
    return true;
}

std::recursive_mutex memoryMutex;
std::unordered_map<void *, size_t> allocations;
std::unordered_set<void *> freedAllocations;

struct MemoryType {
    size_t size;
    size_t alignment;
};

struct TypedBlock {
    std::string type;
    size_t count;
};

struct StructField {
    std::string name;
    std::string type;
    size_t offset;
    size_t size;
    size_t alignment;
};

struct StructLayout {
    std::vector<StructField> fields;
    size_t size;
};

std::unordered_map<void *, TypedBlock> typedBlocks;
std::unordered_map<void *, StructLayout> structBlocks;

struct NativeLibrary {
    NativeLibrary(void *libraryHandle, const char *libraryPath)
        : handle(libraryHandle), path(libraryPath) {}

    void *handle;
    std::string path;
    std::unordered_map<std::string, std::pair<uintptr_t, std::string>> functions;
    std::unordered_map<std::string, std::int64_t> constants;
    std::unordered_map<std::string, std::string> types;
    std::unordered_set<uintptr_t> functionAddresses;
    std::mutex lifecycleMutex;
    size_t activeCalls{0};
    bool closing{false};
};

std::mutex nativeLibrariesMutex;
std::unordered_map<std::int64_t, std::shared_ptr<NativeLibrary>> nativeLibraries;
std::unordered_map<uintptr_t, std::shared_ptr<NativeLibrary>> nativeFunctionOwners;
std::unordered_set<uintptr_t> closedNativeFunctions;
std::int64_t nextNativeLibrary = 1;

struct FfiCallbackCallable {
    PyObject_HEAD
    PyObject *target;
    std::string *resultType;
    std::vector<std::string> *parameterTypes;
};

struct FfiCallbackRecord {
    PyObject *callable;
    FfiCallbackCallable *thunk;
};

PyTypeObject FfiCallbackCallableType = {PyVarObject_HEAD_INIT(nullptr, 0)};
std::mutex ffiCallbackMutex;
std::unordered_map<uintptr_t, FfiCallbackRecord> ffiCallbacks;
std::vector<FfiCallbackRecord> retiredFfiCallbacks;

bool registerFunctionOwner(const std::shared_ptr<NativeLibrary> &library,
                           uintptr_t address) {
    if (address == 0) return false;
    nativeFunctionOwners[address] = library;
    closedNativeFunctions.erase(address);
    library->functionAddresses.insert(address);
    return true;
}

std::shared_ptr<NativeLibrary> acquireFunctionOwner(uintptr_t address) {
    std::lock_guard<std::mutex> librariesLock(nativeLibrariesMutex);
    auto owner = nativeFunctionOwners.find(address);
    if (owner == nativeFunctionOwners.end()) {
        if (closedNativeFunctions.count(address)) {
            PyErr_SetString(PyExc_RuntimeError,
                            "native function belongs to a closed library");
            return nullptr;
        }
        return {};
    }
    auto library = owner->second;
    std::lock_guard<std::mutex> lifecycleLock(library->lifecycleMutex);
    if (library->closing) {
        PyErr_SetString(PyExc_RuntimeError,
                        "native function belongs to a closing library");
        return nullptr;
    }
    ++library->activeCalls;
    return library;
}

void releaseFunctionOwner(const std::shared_ptr<NativeLibrary> &library) {
    if (!library) return;
    std::lock_guard<std::mutex> lifecycleLock(library->lifecycleMutex);
    if (library->activeCalls > 0) --library->activeCalls;
}

struct FunctionOwnerLease {
    std::shared_ptr<NativeLibrary> library;
    ~FunctionOwnerLease() { releaseFunctionOwner(library); }
};

bool closeNativeLibrary(uintptr_t id) {
    std::lock_guard<std::mutex> librariesLock(nativeLibrariesMutex);
    auto it = nativeLibraries.find(static_cast<std::int64_t>(id));
    if (it == nativeLibraries.end()) {
        PyErr_SetString(PyExc_RuntimeError, "unknown native library handle");
        return false;
    }
    const auto &library = it->second;
    std::lock_guard<std::mutex> lifecycleLock(library->lifecycleMutex);
    if (library->closing) {
        PyErr_SetString(PyExc_RuntimeError, "native library is already closing");
        return false;
    }
    if (library->activeCalls != 0) {
        PyErr_SetString(PyExc_RuntimeError,
                        "cannot close a native library while a function is running");
        return false;
    }
    try {
        closedNativeFunctions.reserve(
            closedNativeFunctions.size() + library->functionAddresses.size());
    } catch (const std::exception &) {
        PyErr_NoMemory();
        return false;
    }
    library->closing = true;
#if defined(__unix__) || defined(__APPLE__)
    if (dlclose(library->handle) != 0) {
        library->closing = false;
        const char *detail = dlerror();
        PyErr_Format(PyExc_RuntimeError, "could not close native library: %s",
                     detail ? detail : "unknown loader error");
        return false;
    }
#endif
    for (uintptr_t address : library->functionAddresses) {
        auto owner = nativeFunctionOwners.find(address);
        if (owner != nativeFunctionOwners.end() && owner->second == library) {
            nativeFunctionOwners.erase(owner);
            closedNativeFunctions.insert(address);
        }
    }
    nativeLibraries.erase(it);
    return true;
}

struct NativeRegistration {
    NativeLibrary *library;
    std::string error;
};

bool validNativeName(const char *name) {
    if (!name || !*name) return false;
    if (!(std::isalpha(static_cast<unsigned char>(*name)) || *name == '_')) return false;
    for (const char *p = name + 1; *p; ++p) {
        if (!(std::isalnum(static_cast<unsigned char>(*p)) || *p == '_')) return false;
    }
    return true;
}

int registerNativeFunction(const char *name, const char *symbol,
                           const char *signature, NativeRegistration *registration) {
    if (!validNativeName(name) || !symbol || !*symbol || !signature || !*signature) {
        registration->error = "invalid function registration";
        return 0;
    }
    if (registration->library->functions.count(name) ||
        registration->library->constants.count(name) ||
        registration->library->types.count(name)) {
        registration->error = std::string("duplicate registration '") + name + "'";
        return 0;
    }
#if defined(__unix__) || defined(__APPLE__)
    void *address = dlsym(registration->library->handle, symbol);
    if (!address) {
        const char *detail = dlerror();
        registration->error = std::string("registered symbol '") + symbol +
                              "' was not found: " +
                              (detail ? detail : "unknown linker error");
        return 0;
    }
#else
    registration->error = "native modules are only supported on POSIX hosts";
    return 0;
#endif
    registration->library->functions[name] = {
        reinterpret_cast<uintptr_t>(address), signature};
    return 1;
}

int registerNativeFunctionThunk(const char *name, const char *symbol,
                                const char *signature, void *opaque) {
    return registerNativeFunction(name, symbol, signature,
                                  static_cast<NativeRegistration *>(opaque));
}

int registerNativeConstant(const char *name, std::int64_t value, void *opaque) {
    auto *registration = static_cast<NativeRegistration *>(opaque);
    if (!validNativeName(name)) {
        registration->error = "invalid constant registration";
        return 0;
    }
    if (registration->library->functions.count(name) ||
        registration->library->constants.count(name) ||
        registration->library->types.count(name)) {
        registration->error = std::string("duplicate registration '") + name + "'";
        return 0;
    }
    registration->library->constants[name] = value;
    return 1;
}

int registerNativeType(const char *name, const char *layout, void *opaque) {
    auto *registration = static_cast<NativeRegistration *>(opaque);
    if (!validNativeName(name) || !layout || !*layout) {
        registration->error = "invalid type registration";
        return 0;
    }
    if (registration->library->functions.count(name) ||
        registration->library->constants.count(name) ||
        registration->library->types.count(name)) {
        registration->error = std::string("duplicate registration '") + name + "'";
        return 0;
    }
    registration->library->types[name] = layout;
    return 1;
}

PyObject *pyNativeModuleLoad(PyObject *, PyObject *args) {
#if !defined(__unix__) && !defined(__APPLE__)
    PyErr_SetString(PyExc_NotImplementedError, "native modules are only supported on POSIX hosts");
    return nullptr;
#else
    const char *path;
    if (!PyArg_ParseTuple(args, "s", &path)) return nullptr;
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char *detail = dlerror();
        PyErr_Format(PyExc_RuntimeError, "could not load native module '%s': %s",
                     path, detail ? detail : "unknown loader error");
        return nullptr;
    }
    std::shared_ptr<NativeLibrary> library;
    try {
        library = std::make_shared<NativeLibrary>(handle, path);
    } catch (const std::exception &) {
        dlclose(handle);
        PyErr_NoMemory();
        return nullptr;
    }
    auto *initializer = reinterpret_cast<int (*)(int (*)(const char *, const char *, const char *),
                                                  int (*)(const char *, std::int64_t),
                                                  int (*)(const char *, const char *))>(
        dlsym(handle, "lynxer_module_init_v1"));
    if (!initializer) {
        dlclose(handle);
        PyErr_SetString(PyExc_RuntimeError,
            "native module lifecycle failure: missing lynxer_module_init_v1 entry point");
        return nullptr;
    }
    NativeRegistration registration{library.get(), ""};
    // The registration ABI has no user-data parameter.  Keep the active
    // registration in thread-local storage for the three C callbacks.
    static thread_local NativeRegistration *active = nullptr;
    active = &registration;
    auto functionCallback = [](const char *n, const char *s, const char *g) noexcept -> int {
        try {
            return registerNativeFunction(n, s, g, active);
        } catch (...) {
            try { active->error = "function registration raised an exception"; } catch (...) {}
            return 0;
        }
    };
    auto constantCallback = [](const char *n, std::int64_t v) noexcept -> int {
        try {
            return registerNativeConstant(n, v, active);
        } catch (...) {
            try { active->error = "constant registration raised an exception"; } catch (...) {}
            return 0;
        }
    };
    auto typeCallback = [](const char *n, const char *l) noexcept -> int {
        try {
            return registerNativeType(n, l, active);
        } catch (...) {
            try { active->error = "type registration raised an exception"; } catch (...) {}
            return 0;
        }
    };
    int status = -1;
    try {
        status = initializer(functionCallback, constantCallback, typeCallback);
    } catch (const std::exception &error) {
        try {
            registration.error =
                std::string("initializer threw an exception: ") + error.what();
        } catch (...) {
            status = -1;
        }
    } catch (...) {
        try {
            registration.error = "initializer threw an unknown exception";
        } catch (...) {
            status = -1;
        }
    }
    active = nullptr;
    if (status != 0 || !registration.error.empty()) {
        if (registration.error.empty()) {
            PyErr_Format(PyExc_RuntimeError,
                         "native module lifecycle failure: initializer returned %d",
                         status);
        } else {
            PyErr_Format(PyExc_RuntimeError,
                         "native module lifecycle failure: %s",
                         registration.error.c_str());
        }
        dlclose(handle);
        return nullptr;
    }
    std::int64_t id;
    try {
        std::lock_guard<std::mutex> librariesLock(nativeLibrariesMutex);
        id = nextNativeLibrary++;
        nativeLibraries[id] = library;
        for (const auto &entry : library->functions) {
            registerFunctionOwner(library, entry.second.first);
        }
    } catch (const std::exception &) {
        std::lock_guard<std::mutex> librariesLock(nativeLibrariesMutex);
        for (uintptr_t address : library->functionAddresses) {
            auto owner = nativeFunctionOwners.find(address);
            if (owner != nativeFunctionOwners.end() && owner->second == library) {
                nativeFunctionOwners.erase(owner);
            }
        }
        for (auto it = nativeLibraries.begin(); it != nativeLibraries.end();) {
            if (it->second == library) it = nativeLibraries.erase(it);
            else ++it;
        }
        dlclose(handle);
        PyErr_NoMemory();
        return nullptr;
    }
    PyObject *result = PyDict_New();
    PyDict_SetItemString(result, "handle", PyLong_FromLongLong(id));
    std::string name = path;
    size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name.resize(dot);
    PyDict_SetItemString(result, "name", PyUnicode_FromString(name.c_str()));
    PyObject *functions = PyDict_New();
    for (const auto &entry : library->functions) {
        PyObject *info = Py_BuildValue("{s:Ks:s}", "pointer",
            static_cast<unsigned long long>(entry.second.first),
            "signature", entry.second.second.c_str());
        PyDict_SetItemString(functions, entry.first.c_str(), info); Py_DECREF(info);
    }
    PyDict_SetItemString(result, "functions", functions); Py_DECREF(functions);
    PyObject *constants = PyDict_New();
    for (const auto &entry : library->constants) {
        PyObject *value = PyLong_FromLongLong(entry.second);
        PyDict_SetItemString(constants, entry.first.c_str(), value); Py_DECREF(value);
    }
    PyDict_SetItemString(result, "constants", constants); Py_DECREF(constants);
    PyObject *types = PyDict_New();
    for (const auto &entry : library->types) {
        PyObject *value = PyUnicode_FromString(entry.second.c_str());
        PyDict_SetItemString(types, entry.first.c_str(), value); Py_DECREF(value);
    }
    PyDict_SetItemString(result, "types", types); Py_DECREF(types);
    return result;
#endif
}

PyObject *pyNativeModuleClose(PyObject *, PyObject *args) {
    unsigned long long id;
    if (!PyArg_ParseTuple(args, "K", &id)) return nullptr;
    if (id > static_cast<unsigned long long>(
                  std::numeric_limits<uintptr_t>::max())) {
        PyErr_SetString(PyExc_OverflowError, "library handle does not fit this process");
        return nullptr;
    }
    if (!closeNativeLibrary(static_cast<uintptr_t>(id))) return nullptr;
    Py_RETURN_NONE;
}

PyObject *pyFfiLoadLibrary(PyObject *, PyObject *args) {
    const char *path;
    if (!PyArg_ParseTuple(args, "s", &path)) return nullptr;
#if defined(__unix__) || defined(__APPLE__)
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char *detail = dlerror();
        PyErr_Format(PyExc_RuntimeError, "could not load library '%s': %s",
                     path, detail ? detail : "unknown loader error");
        return nullptr;
    }
    std::shared_ptr<NativeLibrary> library;
    try {
        library = std::make_shared<NativeLibrary>(handle, path);
    } catch (const std::exception &) {
        dlclose(handle);
        PyErr_NoMemory();
        return nullptr;
    }
    std::int64_t id;
    try {
        std::lock_guard<std::mutex> librariesLock(nativeLibrariesMutex);
        id = nextNativeLibrary++;
        nativeLibraries[id] = library;
    } catch (const std::exception &) {
        dlclose(handle);
        PyErr_NoMemory();
        return nullptr;
    }
    return PyLong_FromLongLong(id);
#else
    PyErr_SetString(PyExc_NotImplementedError, "dynamic libraries are only supported on POSIX hosts");
    return nullptr;
#endif
}

PyObject *pyFfiLookup(PyObject *, PyObject *args) {
    unsigned long long id;
    const char *symbol;
    if (!PyArg_ParseTuple(args, "Ks", &id, &symbol)) return nullptr;
    std::lock_guard<std::mutex> librariesLock(nativeLibrariesMutex);
    auto it = nativeLibraries.find(static_cast<std::int64_t>(id));
    if (it == nativeLibraries.end()) {
        PyErr_SetString(PyExc_RuntimeError, "unknown library handle");
        return nullptr;
    }
#if defined(__unix__) || defined(__APPLE__)
    {
        std::lock_guard<std::mutex> lifecycleLock(it->second->lifecycleMutex);
        if (it->second->closing) {
            PyErr_SetString(PyExc_RuntimeError, "library is closing");
            return nullptr;
        }
    }
    void *address = dlsym(it->second->handle, symbol);
    if (!address) {
        const char *detail = dlerror();
        PyErr_Format(PyExc_RuntimeError, "symbol '%s' was not found: %s",
                     symbol, detail ? detail : "unknown linker error");
        return nullptr;
    }
    uintptr_t rawAddress = reinterpret_cast<uintptr_t>(address);
    registerFunctionOwner(it->second, rawAddress);
    return PyLong_FromUnsignedLongLong(static_cast<unsigned long long>(rawAddress));
#else
    PyErr_SetString(PyExc_NotImplementedError, "dynamic symbols are only supported on POSIX hosts");
    return nullptr;
#endif
}

PyObject *pyFfiCloseLibrary(PyObject *self, PyObject *args) {
    return pyNativeModuleClose(self, args);
}

bool memoryType(const std::string &name, MemoryType *out) {
    if (name == "byte" || name == "int8" || name == "uint8") *out = {1, 1};
    else if (name == "int16" || name == "uint16") *out = {2, 2};
    else if (name == "int32" || name == "uint32" || name == "float32") *out = {4, 4};
    else if (name == "int64" || name == "uint64" || name == "float64") *out = {8, 8};
    else if (name == "uintptr") *out = {sizeof(uintptr_t), alignof(uintptr_t)};
    else if (name == "pointer") *out = {sizeof(void *), alignof(void *)};
    else if (name == "functionPointer") {
        if (sizeof(void (*)()) != sizeof(uintptr_t)) return false;
        *out = {sizeof(void (*)()), alignof(void (*)())};
    }
    else return false;
    return true;
}

bool parseByteOrder(const char *name, bool *little) {
    if (std::strcmp(name, "little") == 0 || std::strcmp(name, "le") == 0) {
        *little = true;
        return true;
    }
    if (std::strcmp(name, "big") == 0 || std::strcmp(name, "be") == 0) {
        *little = false;
        return true;
    }
    return false;
}

bool nativeIntegerType(const std::string &name) {
    return name == "int8" || name == "uint8" ||
           name == "int16" || name == "uint16" ||
           name == "int32" || name == "uint32" ||
           name == "int64" || name == "uint64" ||
           name == "uintptr" || name == "float32" || name == "float64" ||
           name == "cstring";
}

bool parseNativeSignature(const char *signature, std::string *result,
                          std::vector<std::string> *parameters,
                          std::string *convention) {
    std::string text(signature);
    *convention = "cdecl";
    if (text.rfind("cdecl:", 0) == 0 || text.rfind("stdcall:", 0) == 0) {
        *convention = text.substr(0, text.find(':'));
        text = text.substr(text.find(':') + 1);
    }
    size_t open = text.find('(');
    if (open == std::string::npos || text.back() != ')' || open == 0) {
        PyErr_SetString(PyExc_ValueError,
            "native signature must be returnType(type,...)");
        return false;
    }
    *result = text.substr(0, open);
    if (*result != "void" && !nativeIntegerType(*result)) {
        PyErr_SetString(PyExc_ValueError,
            "native call supports void, integer, floating-point, and pointer return types");
        return false;
    }
    std::string parametersText = text.substr(open + 1, text.size() - open - 2);
    if (parametersText.empty()) return true;
    size_t start = 0;
    while (start <= parametersText.size()) {
        size_t end = parametersText.find(',', start);
        std::string parameter = parametersText.substr(
            start, end == std::string::npos ? end : end - start);
        size_t first = parameter.find_first_not_of(" \t");
        size_t last = parameter.find_last_not_of(" \t");
        if (first == std::string::npos) {
            PyErr_SetString(PyExc_ValueError, "native signature contains an empty parameter");
            return false;
        }
        parameter = parameter.substr(first, last - first + 1);
        if (!nativeIntegerType(parameter) || parameter == "void") {
            PyErr_SetString(PyExc_ValueError,
                "native call parameters must be integer, floating-point, or pointer types");
            return false;
        }
        parameters->push_back(parameter);
        if (parameters->size() > 6) {
            PyErr_SetString(PyExc_ValueError,
                "native call supports at most six parameters");
            return false;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

bool hostIsLittleEndian() {
    const std::uint16_t marker = 1;
    return *reinterpret_cast<const unsigned char *>(&marker) == 1;
}

template <typename T>
T loadOrdered(const unsigned char *bytes, bool little) {
    unsigned char native[sizeof(T)];
    if (little == hostIsLittleEndian()) {
        std::memcpy(native, bytes, sizeof(T));
    } else {
        for (size_t i = 0; i < sizeof(T); ++i) {
            native[i] = bytes[sizeof(T) - 1 - i];
        }
    }
    T value;
    std::memcpy(&value, native, sizeof(T));
    return value;
}

template <typename T>
void storeOrdered(unsigned char *bytes, T value, bool little) {
    unsigned char native[sizeof(T)];
    std::memcpy(native, &value, sizeof(T));
    if (little == hostIsLittleEndian()) {
        std::memcpy(bytes, native, sizeof(T));
    } else {
        for (size_t i = 0; i < sizeof(T); ++i) {
            bytes[i] = native[sizeof(T) - 1 - i];
        }
    }
}

bool validFieldName(const std::string &name) {
    if (name.empty() ||
        !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) {
        return false;
    }
    for (size_t i = 1; i < name.size(); ++i) {
        unsigned char character = static_cast<unsigned char>(name[i]);
        if (!(std::isalnum(character) || name[i] == '_')) return false;
    }
    return true;
}

std::string trimLayoutText(const std::string &text) {
    size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    size_t last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

bool splitLayoutFields(const std::string &text, std::vector<std::string> *fields) {
    size_t start = 0;
    int braces = 0;
    int brackets = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        char character = i < text.size() ? text[i] : ',';
        if (character == '{') ++braces;
        else if (character == '}') {
            if (braces == 0) {
                PyErr_SetString(PyExc_ValueError, "layout has an unmatched closing brace");
                return false;
            }
            --braces;
        }
        else if (character == '[') ++brackets;
        else if (character == ']') {
            if (brackets == 0) {
                PyErr_SetString(PyExc_ValueError, "layout has an unmatched closing bracket");
                return false;
            }
            --brackets;
        }
        else if (character == ',' && braces == 0 && brackets == 0) {
            std::string field = trimLayoutText(text.substr(start, i - start));
            if (field.empty()) {
                PyErr_SetString(PyExc_ValueError, "layout contains an empty field");
                return false;
            }
            fields->push_back(field);
            start = i + 1;
        }
    }
    if (braces != 0 || brackets != 0) {
        PyErr_SetString(PyExc_ValueError, "layout has unbalanced braces or brackets");
        return false;
    }
    return true;
}

bool layoutFromText(const std::string &text, StructLayout *out, bool unionLayout);
size_t layoutAlignment(const StructLayout &layout);

bool typeLayout(const std::string &rawType, MemoryType *out) {
    std::string type = trimLayoutText(rawType);
    if (memoryType(type, out)) return true;
    if (type.size() > 2 && type.back() == ']') {
        size_t open = type.rfind('[');
        if (open == std::string::npos || open == 0 || open == type.size() - 1) return false;
        std::string countText = type.substr(open + 1, type.size() - open - 2);
        char *end = nullptr;
        unsigned long long count = std::strtoull(countText.c_str(), &end, 10);
        if (!end || *end != '\0' || count == 0 ||
            count > std::numeric_limits<size_t>::max()) return false;
        MemoryType element;
        if (!typeLayout(type.substr(0, open), &element) ||
            count > std::numeric_limits<size_t>::max() / element.size) return false;
        out->size = element.size * static_cast<size_t>(count);
        out->alignment = element.alignment;
        return true;
    }
    bool isStruct = type.rfind("struct{", 0) == 0;
    bool isUnion = type.rfind("union{", 0) == 0;
    if (isStruct || isUnion) {
        if (type.back() != '}') return false;
        StructLayout nested;
        if (!layoutFromText(type.substr(type.find('{') + 1,
                                        type.size() - type.find('{') - 2),
                            &nested, isUnion)) return false;
        out->size = nested.size;
        out->alignment = layoutAlignment(nested);
        return true;
    }
    return false;
}

size_t layoutAlignment(const StructLayout &layout) {
    size_t alignment = 1;
    for (const auto &field : layout.fields) alignment = std::max(alignment, field.alignment);
    return alignment;
}

bool layoutFromText(const std::string &text, StructLayout *out, bool unionLayout) {
    *out = StructLayout{{}, 0};
    size_t offset = 0;
    size_t alignment = 1;
    std::unordered_set<std::string> names;
    std::vector<std::string> fields;
    if (!splitLayoutFields(text, &fields)) return false;
    for (const std::string &item : fields) {
        size_t split = std::string::npos;
        int braces = 0;
        int brackets = 0;
        for (size_t i = 0; i < item.size(); ++i) {
            if (item[i] == '{') ++braces;
            else if (item[i] == '}') --braces;
            else if (item[i] == '[') ++brackets;
            else if (item[i] == ']') --brackets;
            else if (std::isspace(static_cast<unsigned char>(item[i])) &&
                     braces == 0 && brackets == 0) {
                split = i;
                break;
            }
        }
        if (split == std::string::npos) {
            PyErr_SetString(PyExc_ValueError, "layout fields must be '<type> <name>'");
            return false;
        }
        std::string type = trimLayoutText(item.substr(0, split));
        size_t nameStart = item.find_first_not_of(" \t", split);
        std::string name = nameStart == std::string::npos
            ? "" : trimLayoutText(item.substr(nameStart));
        MemoryType info;
        if (!typeLayout(type, &info) || !validFieldName(name) || names.count(name)) {
            PyErr_SetString(PyExc_ValueError, "invalid or duplicate struct layout field");
            return false;
        }
        names.insert(name);
        size_t fieldOffset = 0;
        if (!unionLayout) {
            size_t remainder = offset % info.alignment;
            size_t padding = remainder == 0 ? 0 : info.alignment - remainder;
            if (offset > std::numeric_limits<size_t>::max() - padding) {
                PyErr_SetString(PyExc_OverflowError, "struct layout offset overflow");
                return false;
            }
            fieldOffset = offset + padding;
        }
        out->fields.push_back({name, type, fieldOffset, info.size, info.alignment});
        if (unionLayout) offset = std::max(offset, info.size);
        else {
            if (fieldOffset > std::numeric_limits<size_t>::max() - info.size) {
                PyErr_SetString(PyExc_OverflowError, "struct layout size overflow");
                return false;
            }
            offset = fieldOffset + info.size;
        }
        alignment = std::max(alignment, info.alignment);
    }
    size_t remainder = offset % alignment;
    size_t padding = remainder == 0 ? 0 : alignment - remainder;
    if (offset > std::numeric_limits<size_t>::max() - padding) {
        PyErr_SetString(PyExc_OverflowError, "struct layout size overflow");
        return false;
    }
    out->size = offset + padding;
    return true;
}

bool layoutFromObject(PyObject *object, StructLayout *out) {
    const char *raw;
    if (!PyArg_Parse(object, "s", &raw)) return false;
    return layoutFromText(raw, out, false);
}

bool blockField(PyObject *object, PyObject *indexObject, void **ptr, size_t *offset,
                std::string *type) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    void *raw;
    unsigned long long index;
    if (!pointerFromPy(object, &raw) ||
        !PyArg_Parse(indexObject, "K", &index)) return false;
    auto block = typedBlocks.find(raw);
    if (block == typedBlocks.end() || index >= block->second.count) {
        PyErr_SetString(PyExc_RuntimeError, "typed memory block index is out of bounds");
        return false;
    }
    MemoryType info;
    memoryType(block->second.type, &info);
    *ptr = raw;
    *offset = static_cast<size_t>(index) * info.size;
    *type = block->second.type;
    return true;
}

bool validateMemory(void *ptr, size_t offset, size_t bytes) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    if (freedAllocations.find(ptr) != freedAllocations.end()) {
        PyErr_SetString(PyExc_RuntimeError, "address refers to freed memory");
        return false;
    }
    auto allocation = allocations.find(ptr);
    if (allocation == allocations.end()) {
        PyErr_SetString(PyExc_RuntimeError, "invalid native memory address");
        return false;
    }
    if (offset > allocation->second || bytes > allocation->second - offset) {
        PyErr_SetString(PyExc_RuntimeError, "memory access is out of bounds");
        return false;
    }
    return true;
}

bool atomicLocation(PyObject *addressObject, unsigned long long offset,
                    const char *type, void **out) {
    MemoryType info;
    if (std::strcmp(type, "int32") != 0 && std::strcmp(type, "uint32") != 0 &&
        std::strcmp(type, "int64") != 0 && std::strcmp(type, "uint64") != 0) {
        PyErr_SetString(PyExc_ValueError, "atomic operations support int32, uint32, int64, and uint64");
        return false;
    }
    memoryType(type, &info);
    void *raw;
    if (!pointerFromPy(addressObject, &raw) ||
        !validateMemory(raw, static_cast<size_t>(offset), info.size)) return false;
    uintptr_t value = reinterpret_cast<uintptr_t>(raw) + static_cast<size_t>(offset);
    if (value % info.alignment != 0) {
        PyErr_SetString(PyExc_RuntimeError, "atomic address is not properly aligned");
        return false;
    }
    *out = reinterpret_cast<void *>(value);
    return true;
}

PyObject *pyAtomicLoad(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *addressObject; unsigned long long offset; const char *type;
    if (!PyArg_ParseTuple(args, "OKs", &addressObject, &offset, &type)) return nullptr;
    void *location;
    if (!atomicLocation(addressObject, offset, type, &location)) return nullptr;
    if (std::strcmp(type, "int32") == 0)
        return PyLong_FromLong(__atomic_load_n(static_cast<std::int32_t *>(location), __ATOMIC_SEQ_CST));
    if (std::strcmp(type, "uint32") == 0)
        return PyLong_FromUnsignedLong(__atomic_load_n(static_cast<std::uint32_t *>(location), __ATOMIC_SEQ_CST));
    if (std::strcmp(type, "int64") == 0)
        return PyLong_FromLongLong(__atomic_load_n(static_cast<std::int64_t *>(location), __ATOMIC_SEQ_CST));
    return PyLong_FromUnsignedLongLong(__atomic_load_n(static_cast<std::uint64_t *>(location), __ATOMIC_SEQ_CST));
}

PyObject *pyAtomicStore(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *addressObject; unsigned long long offset; const char *type; long long value;
    if (!PyArg_ParseTuple(args, "OKsL", &addressObject, &offset, &type, &value)) return nullptr;
    void *location;
    if (!atomicLocation(addressObject, offset, type, &location)) return nullptr;
    if (std::strcmp(type, "int32") == 0) __atomic_store_n(static_cast<std::int32_t *>(location), static_cast<std::int32_t>(value), __ATOMIC_SEQ_CST);
    else if (std::strcmp(type, "uint32") == 0) __atomic_store_n(static_cast<std::uint32_t *>(location), static_cast<std::uint32_t>(value), __ATOMIC_SEQ_CST);
    else if (std::strcmp(type, "int64") == 0) __atomic_store_n(static_cast<std::int64_t *>(location), static_cast<std::int64_t>(value), __ATOMIC_SEQ_CST);
    else __atomic_store_n(static_cast<std::uint64_t *>(location), static_cast<std::uint64_t>(value), __ATOMIC_SEQ_CST);
    Py_RETURN_NONE;
}

PyObject *pyAtomicAdd(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *addressObject; unsigned long long offset; const char *type; long long value;
    if (!PyArg_ParseTuple(args, "OKsL", &addressObject, &offset, &type, &value)) return nullptr;
    void *location;
    if (!atomicLocation(addressObject, offset, type, &location)) return nullptr;
    if (std::strcmp(type, "int32") == 0)
        return PyLong_FromLong(__atomic_add_fetch(static_cast<std::int32_t *>(location), static_cast<std::int32_t>(value), __ATOMIC_SEQ_CST));
    if (std::strcmp(type, "uint32") == 0)
        return PyLong_FromUnsignedLong(__atomic_add_fetch(static_cast<std::uint32_t *>(location), static_cast<std::uint32_t>(value), __ATOMIC_SEQ_CST));
    if (std::strcmp(type, "int64") == 0)
        return PyLong_FromLongLong(__atomic_add_fetch(static_cast<std::int64_t *>(location), static_cast<std::int64_t>(value), __ATOMIC_SEQ_CST));
    return PyLong_FromUnsignedLongLong(__atomic_add_fetch(static_cast<std::uint64_t *>(location), static_cast<std::uint64_t>(value), __ATOMIC_SEQ_CST));
}

PyObject *pyVolatileRead(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *addressObject; unsigned long long offset; const char *type;
    if (!PyArg_ParseTuple(args, "OKs", &addressObject, &offset, &type)) return nullptr;
    MemoryType info;
    if (!memoryType(type, &info) || info.size > 8) {
        PyErr_SetString(PyExc_ValueError, "unsupported volatile memory type");
        return nullptr;
    }
    void *raw;
    if (!pointerFromPy(addressObject, &raw) ||
        !validateMemory(raw, static_cast<size_t>(offset), info.size)) return nullptr;
    volatile unsigned char *location = static_cast<volatile unsigned char *>(raw) + offset;
    unsigned long long result = 0;
    for (size_t i = 0; i < info.size; ++i) {
        result |= static_cast<unsigned long long>(location[i]) << (i * 8);
    }
    return PyLong_FromUnsignedLongLong(result);
}

PyObject *pyVolatileWrite(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *addressObject; unsigned long long offset, value; const char *type;
    if (!PyArg_ParseTuple(args, "OKsK", &addressObject, &offset, &type, &value)) return nullptr;
    MemoryType info;
    if (!memoryType(type, &info) || info.size > 8) {
        PyErr_SetString(PyExc_ValueError, "unsupported volatile memory type");
        return nullptr;
    }
    void *raw;
    if (!pointerFromPy(addressObject, &raw) ||
        !validateMemory(raw, static_cast<size_t>(offset), info.size)) return nullptr;
    volatile unsigned char *location = static_cast<volatile unsigned char *>(raw) + offset;
    for (size_t i = 0; i < info.size; ++i) location[i] = static_cast<unsigned char>(value >> (i * 8));
    Py_RETURN_NONE;
}

PyObject *pyMemoryProtect(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *addressObject; unsigned long long size; const char *mode;
    if (!PyArg_ParseTuple(args, "OKs", &addressObject, &size, &mode)) return nullptr;
    void *raw;
    if (!pointerFromPy(addressObject, &raw) || !validateMemory(raw, 0, static_cast<size_t>(size))) return nullptr;
#if defined(__unix__) || defined(__APPLE__)
    long page = sysconf(_SC_PAGESIZE);
    uintptr_t start = reinterpret_cast<uintptr_t>(raw) & ~(static_cast<uintptr_t>(page) - 1);
    uintptr_t end = (reinterpret_cast<uintptr_t>(raw) + size + page - 1) & ~(static_cast<uintptr_t>(page) - 1);
    int protection = 0;
    if (std::strcmp(mode, "read") == 0) protection = PROT_READ;
    else if (std::strcmp(mode, "readwrite") == 0) protection = PROT_READ | PROT_WRITE;
    else if (std::strcmp(mode, "execute") == 0) protection = PROT_READ | PROT_EXEC;
    else if (std::strcmp(mode, "none") != 0) {
        PyErr_SetString(PyExc_ValueError, "memory protection must be read, readwrite, execute, or none");
        return nullptr;
    }
    if (mprotect(reinterpret_cast<void *>(start), end - start, protection) != 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return nullptr;
    }
    Py_RETURN_NONE;
#else
    PyErr_SetString(PyExc_NotImplementedError, "memory protection is not supported on this platform");
    return nullptr;
#endif
}

void trackAllocation(void *ptr, size_t size) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    if (ptr != nullptr) {
        allocations[ptr] = size;
        freedAllocations.erase(ptr);
    }
}

PyObject *pyRefCreate(PyObject *, PyObject *args) {
    PyObject *value;
    if (!PyArg_ParseTuple(args, "O", &value)) return nullptr;
    std::unique_ptr<ReferenceCell> cell;
    try {
        cell = std::make_unique<ReferenceCell>();
    } catch (const std::exception &) {
        PyErr_NoMemory();
        return nullptr;
    }
    Py_INCREF(value);
    cell->value = value;
    std::lock_guard<std::mutex> lock(referenceCellsMutex);
    if (nextReferenceHandle == 0) {
        Py_DECREF(value);
        PyErr_SetString(PyExc_OverflowError, "reference handle space is exhausted");
        return nullptr;
    }
    uintptr_t handle = nextReferenceHandle++;
    try {
        referenceCells.emplace(handle, std::move(cell));
    } catch (const std::exception &) {
        Py_DECREF(value);
        PyErr_NoMemory();
        return nullptr;
    }
    return PyLong_FromUnsignedLongLong(static_cast<unsigned long long>(handle));
}

PyObject *pyRefGet(PyObject *, PyObject *args) {
    PyObject *pointerObject;
    if (!PyArg_ParseTuple(args, "O", &pointerObject)) return nullptr;
    unsigned long long rawHandle;
    if (!PyArg_Parse(pointerObject, "K", &rawHandle)) return nullptr;
    if (rawHandle > static_cast<unsigned long long>(
                        std::numeric_limits<uintptr_t>::max())) {
        PyErr_SetString(PyExc_OverflowError, "reference handle does not fit this process");
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(referenceCellsMutex);
    auto it = referenceCells.find(static_cast<uintptr_t>(rawHandle));
    if (it == referenceCells.end() || it->second->value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "invalid Lynxer reference pointer");
        return nullptr;
    }
    Py_INCREF(it->second->value);
    return it->second->value;
}

PyObject *pyRefSet(PyObject *, PyObject *args) {
    PyObject *pointerObject;
    PyObject *value;
    if (!PyArg_ParseTuple(args, "OO", &pointerObject, &value)) return nullptr;
    unsigned long long rawHandle;
    if (!PyArg_Parse(pointerObject, "K", &rawHandle)) return nullptr;
    if (rawHandle > static_cast<unsigned long long>(
                        std::numeric_limits<uintptr_t>::max())) {
        PyErr_SetString(PyExc_OverflowError, "reference handle does not fit this process");
        return nullptr;
    }
    Py_INCREF(value);
    PyObject *oldValue = nullptr;
    {
        std::lock_guard<std::mutex> lock(referenceCellsMutex);
        auto it = referenceCells.find(static_cast<uintptr_t>(rawHandle));
        if (it == referenceCells.end() || it->second->value == nullptr) {
            Py_DECREF(value);
            PyErr_SetString(PyExc_RuntimeError, "invalid Lynxer reference pointer");
            return nullptr;
        }
        oldValue = it->second->value;
        it->second->value = value;
    }
    Py_XDECREF(oldValue);
    Py_RETURN_NONE;
}

PyObject *pyRefFree(PyObject *, PyObject *args) {
    PyObject *pointerObject;
    if (!PyArg_ParseTuple(args, "O", &pointerObject)) return nullptr;
    unsigned long long rawHandle;
    if (!PyArg_Parse(pointerObject, "K", &rawHandle)) return nullptr;
    if (rawHandle > static_cast<unsigned long long>(
                        std::numeric_limits<uintptr_t>::max())) {
        PyErr_SetString(PyExc_OverflowError, "reference handle does not fit this process");
        return nullptr;
    }
    std::unique_ptr<ReferenceCell> cell;
    {
        std::lock_guard<std::mutex> lock(referenceCellsMutex);
        auto it = referenceCells.find(static_cast<uintptr_t>(rawHandle));
        if (it == referenceCells.end()) {
            PyErr_SetString(PyExc_RuntimeError, "invalid or already freed Lynxer reference");
            return nullptr;
        }
        cell = std::move(it->second);
        referenceCells.erase(it);
    }
    Py_XDECREF(cell->value);
    cell->value = nullptr;
    Py_RETURN_NONE;
}

/*
 * Native calls are dispatched through Python's ctypes rather than by casting
 * an integer to an unrelated C++ function-pointer type.  ctypes/libffi owns
 * the platform ABI details (including register-vs-stack arguments and
 * floating-point returns) for the active architecture.
 */
PyObject *ctypesType(const std::string &name) {
    PyObject *ctypes = PyImport_ImportModule("ctypes");
    if (!ctypes) return nullptr;
    const char *typeName = nullptr;
    if (name == "int8") typeName = "c_int8";
    else if (name == "uint8") typeName = "c_uint8";
    else if (name == "int16") typeName = "c_int16";
    else if (name == "uint16") typeName = "c_uint16";
    else if (name == "int32") typeName = "c_int32";
    else if (name == "uint32") typeName = "c_uint32";
    else if (name == "int64") typeName = "c_int64";
    else if (name == "uint64") typeName = "c_uint64";
    else if (name == "uintptr") typeName = "c_size_t";
    else if (name == "float32") typeName = "c_float";
    else if (name == "float64") typeName = "c_double";
    else if (name == "cstring") typeName = "c_char_p";
    PyObject *result = typeName ? PyObject_GetAttrString(ctypes, typeName) : nullptr;
    Py_DECREF(ctypes);
    if (!result && !PyErr_Occurred()) {
        PyErr_Format(PyExc_ValueError, "unsupported native type '%s'", name.c_str());
    }
    return result;
}

PyObject *ctypesFunctionType(const std::string &convention,
                             const std::string &resultType,
                             const std::vector<std::string> &parameterTypes) {
    PyObject *ctypes = PyImport_ImportModule("ctypes");
    if (!ctypes) return nullptr;
    const char *factoryName = convention == "stdcall" ? "WINFUNCTYPE" : "CFUNCTYPE";
    PyObject *factory = PyObject_GetAttrString(ctypes, factoryName);
    PyObject *result = resultType == "void" ? Py_NewRef(Py_None) : ctypesType(resultType);
    if (!factory || !result) {
        Py_XDECREF(factory);
        Py_XDECREF(result);
        Py_DECREF(ctypes);
        return nullptr;
    }
    PyObject *types = PyTuple_New(static_cast<Py_ssize_t>(parameterTypes.size()) + 1);
    if (!types) {
        Py_DECREF(factory); Py_DECREF(result); Py_DECREF(ctypes);
        return nullptr;
    }
    PyTuple_SET_ITEM(types, 0, result);
    for (size_t index = 0; index < parameterTypes.size(); ++index) {
        PyObject *type = ctypesType(parameterTypes[index]);
        if (!type) {
            Py_DECREF(types); Py_DECREF(factory); Py_DECREF(ctypes);
            return nullptr;
        }
        PyTuple_SET_ITEM(types, static_cast<Py_ssize_t>(index + 1), type);
    }
    PyObject *functionType = PyObject_CallObject(factory, types);
    Py_DECREF(types);
    Py_DECREF(factory);
    Py_DECREF(ctypes);
    return functionType;
}

PyObject *ctypesAddress(PyObject *callable) {
    PyObject *ctypes = PyImport_ImportModule("ctypes");
    if (!ctypes) return nullptr;
    PyObject *cast = PyObject_GetAttrString(ctypes, "cast");
    PyObject *voidType = PyObject_GetAttrString(ctypes, "c_void_p");
    PyObject *castResult = cast && voidType
        ? PyObject_CallFunctionObjArgs(cast, callable, voidType, nullptr) : nullptr;
    PyObject *value = castResult ? PyObject_GetAttrString(castResult, "value") : nullptr;
    Py_XDECREF(castResult);
    Py_XDECREF(cast);
    Py_XDECREF(voidType);
    Py_DECREF(ctypes);
    return value;
}

PyObject *nativeCallViaCtypes(PyObject *addressObject, const char *signature,
                              PyObject *valuesObject) {
    std::string resultType;
    std::vector<std::string> parameterTypes;
    std::string convention;
    if (!parseNativeSignature(signature, &resultType, &parameterTypes, &convention)) {
        return nullptr;
    }
    Py_ssize_t count = PyList_GET_SIZE(valuesObject);
    if (count != static_cast<Py_ssize_t>(parameterTypes.size())) {
        PyErr_SetString(PyExc_ValueError,
                        "native call argument count does not match its signature");
        return nullptr;
    }
#if !defined(_WIN32)
    if (convention == "stdcall") {
        PyErr_SetString(PyExc_ValueError,
                        "stdcall signatures are only supported on Windows");
        return nullptr;
    }
#endif
    PyObject *functionType = ctypesFunctionType(convention, resultType, parameterTypes);
    if (!functionType) return nullptr;
    PyObject *address = nullptr;
    void *rawAddress = nullptr;
    if (!pointerFromPy(addressObject, &rawAddress) || !rawAddress) {
        Py_DECREF(functionType);
        if (!PyErr_Occurred()) {
            PyErr_SetString(PyExc_ValueError, "native function address must be non-zero");
        }
        return nullptr;
    }
    address = PyLong_FromUnsignedLongLong(
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(rawAddress)));
    PyObject *function = address
        ? PyObject_CallFunctionObjArgs(functionType, address, nullptr) : nullptr;
    Py_XDECREF(address);
    Py_DECREF(functionType);
    if (!function) return nullptr;

    PyObject *callArgs = PyTuple_New(count);
    if (!callArgs) {
        Py_DECREF(function);
        return nullptr;
    }
    for (Py_ssize_t index = 0; index < count; ++index) {
        PyObject *value = PyList_GET_ITEM(valuesObject, index);
        Py_INCREF(value);
        if (parameterTypes[static_cast<size_t>(index)] == "cstring") {
            if (!PyUnicode_Check(value)) {
                Py_DECREF(value); Py_DECREF(callArgs); Py_DECREF(function);
                PyErr_SetString(PyExc_TypeError, "cstring arguments must be strings");
                return nullptr;
            }
            PyObject *encoded = PyUnicode_AsUTF8String(value);
            Py_DECREF(value);
            if (!encoded) {
                Py_DECREF(callArgs); Py_DECREF(function);
                return nullptr;
            }
            value = encoded;
        }
        PyTuple_SET_ITEM(callArgs, index, value);
    }
    PyObject *result = PyObject_CallObject(function, callArgs);
    Py_DECREF(callArgs);
    Py_DECREF(function);
    if (!result) return nullptr;
    if (resultType == "cstring" && result != Py_None) {
        Py_ssize_t length = 0;
        char *bytes = nullptr;
        if (PyBytes_AsStringAndSize(result, &bytes, &length) < 0) {
            Py_DECREF(result);
            return nullptr;
        }
        PyObject *text = PyUnicode_DecodeUTF8(bytes, length, "replace");
        Py_DECREF(result);
        return text;
    }
    if (resultType == "cstring" && result == Py_None) {
        Py_DECREF(result);
        return PyUnicode_FromString("");
    }
    return result;
}

PyObject *pyNativeCall(PyObject *, PyObject *args) {
    PyObject *addressObject;
    PyObject *signatureObject;
    PyObject *valuesObject;
    if (!PyArg_ParseTuple(args, "OOO", &addressObject, &signatureObject,
                          &valuesObject)) {
        return nullptr;
    }
    const char *signature;
    if (!PyArg_Parse(signatureObject, "s", &signature)) return nullptr;

    void *address = nullptr;
    if (!pointerFromPy(addressObject, &address) || !address) {
        if (!PyErr_Occurred()) {
            PyErr_SetString(PyExc_ValueError,
                            "native function address must be non-zero");
        }
        return nullptr;
    }
    if (!PyList_Check(valuesObject)) {
        PyErr_SetString(PyExc_TypeError, "native call arguments must be a list");
        return nullptr;
    }
    std::shared_ptr<NativeLibrary> owner =
        acquireFunctionOwner(reinterpret_cast<uintptr_t>(address));
    if (PyErr_Occurred()) return nullptr;
    FunctionOwnerLease lease{owner};
    return nativeCallViaCtypes(addressObject, signature, valuesObject);

#if 0
    std::string resultType;
    std::vector<std::string> parameterTypes;
    if (!parseNativeSignature(signature, &resultType, &parameterTypes)) return nullptr;
    Py_ssize_t count = PyList_GET_SIZE(valuesObject);
    if (count != static_cast<Py_ssize_t>(parameterTypes.size())) {
        PyErr_SetString(PyExc_ValueError,
            "native call argument count does not match its signature");
        return nullptr;
    }

    std::vector<uintptr_t> values;
    std::vector<PyObject *> stringKeepalive;
    values.reserve(static_cast<size_t>(count));
    for (Py_ssize_t index = 0; index < count; ++index) {
        PyObject *value = PyList_GET_ITEM(valuesObject, index);
        unsigned long long converted;
        const std::string &parameterType = parameterTypes[static_cast<size_t>(index)];
        if (parameterType == "cstring") {
            if (!PyUnicode_Check(value)) {
                PyErr_SetString(PyExc_TypeError, "cstring arguments must be strings");
                return nullptr;
            }
            PyObject *encoded = PyUnicode_AsUTF8String(value);
            if (!encoded) return nullptr;
            stringKeepalive.push_back(encoded);
            converted = reinterpret_cast<uintptr_t>(PyBytes_AsString(encoded));
        } else if (parameterType == "uint8" || parameterType == "uint16" ||
            parameterType == "uint32" || parameterType == "uint64" ||
            parameterType == "uintptr") {
            converted = PyLong_AsUnsignedLongLong(value);
        } else {
            long long signedValue = PyLong_AsLongLong(value);
            if (!PyErr_Occurred()) converted = static_cast<uintptr_t>(signedValue);
        }
        if (PyErr_Occurred()) {
            PyErr_Clear();
            PyErr_SetString(PyExc_TypeError,
                "native call arguments must be integers compatible with their types");
            return nullptr;
        }
        values.push_back(static_cast<uintptr_t>(converted));
    }

    using NativeFn0 = uintptr_t (*)();
    using NativeFn1 = uintptr_t (*)(uintptr_t);
    using NativeFn2 = uintptr_t (*)(uintptr_t, uintptr_t);
    using NativeFn3 = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t);
    using NativeFn4 = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
    using NativeFn5 = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
    using NativeFn6 = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
    using NativeVoid0 = void (*)();
    using NativeVoid1 = void (*)(uintptr_t);
    using NativeVoid2 = void (*)(uintptr_t, uintptr_t);
    using NativeVoid3 = void (*)(uintptr_t, uintptr_t, uintptr_t);
    using NativeVoid4 = void (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
    using NativeVoid5 = void (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
    using NativeVoid6 = void (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);

    uintptr_t rawAddress = reinterpret_cast<uintptr_t>(address);
    uintptr_t result = 0;
    if (resultType == "void") {
        switch (count) {
        case 0: reinterpret_cast<NativeVoid0>(rawAddress)(); break;
        case 1: reinterpret_cast<NativeVoid1>(rawAddress)(values[0]); break;
        case 2: reinterpret_cast<NativeVoid2>(rawAddress)(values[0], values[1]); break;
        case 3: reinterpret_cast<NativeVoid3>(rawAddress)(values[0], values[1], values[2]); break;
        case 4: reinterpret_cast<NativeVoid4>(rawAddress)(values[0], values[1], values[2], values[3]); break;
        case 5: reinterpret_cast<NativeVoid5>(rawAddress)(values[0], values[1], values[2], values[3], values[4]); break;
        case 6: reinterpret_cast<NativeVoid6>(rawAddress)(values[0], values[1], values[2], values[3], values[4], values[5]); break;
        }
        Py_RETURN_NONE;
    }
    switch (count) {
    case 0: result = reinterpret_cast<NativeFn0>(rawAddress)(); break;
    case 1: result = reinterpret_cast<NativeFn1>(rawAddress)(values[0]); break;
    case 2: result = reinterpret_cast<NativeFn2>(rawAddress)(values[0], values[1]); break;
    case 3: result = reinterpret_cast<NativeFn3>(rawAddress)(values[0], values[1], values[2]); break;
    case 4: result = reinterpret_cast<NativeFn4>(rawAddress)(values[0], values[1], values[2], values[3]); break;
    case 5: result = reinterpret_cast<NativeFn5>(rawAddress)(values[0], values[1], values[2], values[3], values[4]); break;
    case 6: result = reinterpret_cast<NativeFn6>(rawAddress)(values[0], values[1], values[2], values[3], values[4], values[5]); break;
    }
    if (resultType == "int8") return PyLong_FromLong(static_cast<std::int8_t>(result));
    if (resultType == "int16") return PyLong_FromLong(static_cast<std::int16_t>(result));
    if (resultType == "int32") return PyLong_FromLong(static_cast<std::int32_t>(result));
    if (resultType == "int64") return PyLong_FromLongLong(static_cast<std::int64_t>(result));
    if (resultType == "cstring") {
        if (!result) return PyUnicode_FromString("");
        return PyUnicode_FromString(reinterpret_cast<const char *>(result));
    }
    return PyLong_FromUnsignedLongLong(static_cast<unsigned long long>(result));
#endif
}

PyObject *pyFfiCall(PyObject *self, PyObject *args) {
    PyObject *addressObject;
    PyObject *signatureObject;
    PyObject *valuesObject;
    if (!PyArg_ParseTuple(args, "OOO", &addressObject, &signatureObject,
                          &valuesObject)) return nullptr;
    void *address = nullptr;
    if (!pointerFromPy(addressObject, &address) || !address) {
        if (!PyErr_Occurred()) {
            PyErr_SetString(PyExc_ValueError,
                            "native function address must be non-zero");
        }
        return nullptr;
    }
    if (!PyList_Check(valuesObject)) {
        PyErr_SetString(PyExc_TypeError, "native call arguments must be a list");
        return nullptr;
    }
    const char *signature;
    if (!PyArg_Parse(signatureObject, "s", &signature)) return nullptr;
    std::shared_ptr<NativeLibrary> owner =
        acquireFunctionOwner(reinterpret_cast<uintptr_t>(address));
    if (PyErr_Occurred()) return nullptr;
    FunctionOwnerLease lease{owner};
    PyObject *result = nativeCallViaCtypes(addressObject, signature, valuesObject);
    return result;
}

#if 0
PyObject *legacyFfiCallback(PyObject *, PyObject *args) {
    const char *signature;
    PyObject *target;
    if (!PyArg_ParseTuple(args, "sO", &signature, &target)) return nullptr;
    if (std::strcmp(signature, "cdecl:int32(int32,int32)") != 0 &&
        std::strcmp(signature, "int32(int32,int32)") != 0) {
        PyErr_SetString(PyExc_ValueError,
            "native ffiCallback currently supports cdecl:int32(int32,int32)");
        return nullptr;
    }
    int hasExecute = PyObject_HasAttrString(target, "execute");
    if (hasExecute < 0) return nullptr;
    if (!hasExecute) {
        PyErr_SetString(PyExc_TypeError, "ffiCallback target must be executable");
        return nullptr;
    }
    uintptr_t pointer = reinterpret_cast<uintptr_t>(&ffiCallbackInt32Int32);
    std::lock_guard<std::mutex> lock(ffiCallbackMutex);
    auto existing = ffiCallbacks.find(pointer);
    if (existing != ffiCallbacks.end()) Py_DECREF(existing->second);
    Py_INCREF(target);
    ffiCallbacks[pointer] = target;
    return PyLong_FromUnsignedLongLong(pointer);
}

PyObject *legacyFfiFreeCallback(PyObject *, PyObject *args) {
    unsigned long long pointer;
    if (!PyArg_ParseTuple(args, "K", &pointer)) return nullptr;
    std::lock_guard<std::mutex> lock(ffiCallbackMutex);
    auto it = ffiCallbacks.find(static_cast<uintptr_t>(pointer));
    if (it == ffiCallbacks.end()) {
        PyErr_SetString(PyExc_RuntimeError, "unknown native callback");
        return nullptr;
    }
    Py_DECREF(it->second);
    ffiCallbacks.erase(it);
    Py_RETURN_NONE;
}
#endif

PyObject *ffiDefaultResult(const std::string &resultType) {
    if (resultType == "void") return Py_NewRef(Py_None);
    if (resultType == "float32" || resultType == "float64") {
        return PyFloat_FromDouble(0.0);
    }
    if (resultType == "cstring") return PyBytes_FromString("");
    return PyLong_FromLong(0);
}

void ffiCallbackCallableDealloc(FfiCallbackCallable *self) {
    Py_XDECREF(self->target);
    delete self->resultType;
    delete self->parameterTypes;
    Py_TYPE(self)->tp_free(reinterpret_cast<PyObject *>(self));
}

PyObject *ffiCallbackCallableCallImpl(FfiCallbackCallable *self, PyObject *args,
                                      PyObject *) {
    if (self->target == nullptr) return ffiDefaultResult(*self->resultType);
    PyObject *values = PyList_New(PyTuple_GET_SIZE(args));
    if (!values) return nullptr;
    PyObject *runtime = PyImport_ImportModule("lynxer.lynxer");
    PyObject *numberType = runtime ? PyObject_GetAttrString(runtime, "Number") : nullptr;
    if (!numberType) {
        Py_XDECREF(runtime);
        Py_DECREF(values);
        return nullptr;
    }
    for (Py_ssize_t index = 0; index < PyTuple_GET_SIZE(args); ++index) {
        PyObject *raw = PyTuple_GET_ITEM(args, index);
        const std::string &type =
            self->parameterTypes->at(static_cast<size_t>(index));
        PyObject *value = nullptr;
        if (type == "cstring") {
            if (raw == Py_None) {
                value = PyUnicode_FromString("");
            } else if (PyBytes_Check(raw)) {
                Py_ssize_t length = 0;
                char *data = nullptr;
                if (PyBytes_AsStringAndSize(raw, &data, &length) == 0) {
                    value = PyUnicode_DecodeUTF8(data, length, "replace");
                }
            }
        } else {
            value = PyObject_CallFunctionObjArgs(numberType, raw, nullptr);
        }
        if (!value) {
            Py_DECREF(numberType);
            Py_DECREF(runtime);
            Py_DECREF(values);
            return nullptr;
        }
        PyList_SET_ITEM(values, index, value);
    }
    PyObject *result = PyObject_CallMethod(self->target, "execute", "O", values);
    Py_DECREF(numberType);
    Py_DECREF(runtime);
    Py_DECREF(values);
    if (!result) {
        PyErr_WriteUnraisable(reinterpret_cast<PyObject *>(self));
        return ffiDefaultResult(*self->resultType);
    }
    PyObject *error = PyObject_GetAttrString(result, "error");
    bool failed = error && PyObject_IsTrue(error) == 1;
    if (!error) PyErr_Clear();
    Py_XDECREF(error);
    if (failed) {
        PyErr_WriteUnraisable(result);
        Py_DECREF(result);
        return ffiDefaultResult(*self->resultType);
    }
    PyObject *value = PyObject_GetAttrString(result, "value");
    Py_DECREF(result);
    if (!value) {
        PyErr_Clear();
        return ffiDefaultResult(*self->resultType);
    }
    PyObject *rawValue = value == Py_None ? Py_NewRef(Py_None)
                                          : PyObject_GetAttrString(value, "value");
    Py_DECREF(value);
    if (!rawValue) {
        PyErr_Clear();
        return ffiDefaultResult(*self->resultType);
    }
    if (*self->resultType == "cstring" && PyUnicode_Check(rawValue)) {
        PyObject *encoded = PyUnicode_AsUTF8String(rawValue);
        Py_DECREF(rawValue);
        return encoded;
    }
    return rawValue;
}

PyObject *ffiCallbackCallableCall(FfiCallbackCallable *self, PyObject *args,
                                  PyObject *keywords) noexcept {
    try {
        return ffiCallbackCallableCallImpl(self, args, keywords);
    } catch (const std::exception &) {
        PyErr_Clear();
        return ffiDefaultResult(*self->resultType);
    } catch (...) {
        PyErr_Clear();
        return ffiDefaultResult(*self->resultType);
    }
}

PyObject *pyFfiCallback(PyObject *, PyObject *args) {
    const char *signature;
    PyObject *target;
    if (!PyArg_ParseTuple(args, "sO", &signature, &target)) return nullptr;
    if (!PyObject_HasAttrString(target, "execute")) {
        PyErr_SetString(PyExc_TypeError, "ffiCallback target must be executable");
        return nullptr;
    }
    std::string resultType;
    std::vector<std::string> parameterTypes;
    std::string convention;
    if (!parseNativeSignature(signature, &resultType, &parameterTypes, &convention)) {
        return nullptr;
    }
#if !defined(_WIN32)
    if (convention == "stdcall") {
        PyErr_SetString(PyExc_ValueError,
                        "stdcall callbacks are only supported on Windows");
        return nullptr;
    }
#endif
    if (resultType == "cstring") {
        PyErr_SetString(PyExc_ValueError,
                        "cstring callback return values are not supported because "
                        "the native caller would retain a dangling pointer");
        return nullptr;
    }
    auto resultTypeStorage = std::make_unique<std::string>(resultType);
    auto parameterTypeStorage =
        std::make_unique<std::vector<std::string>>(parameterTypes);
    auto *thunk = PyObject_New(FfiCallbackCallable, &FfiCallbackCallableType);
    if (!thunk) return nullptr;
    thunk->target = Py_NewRef(target);
    thunk->resultType = resultTypeStorage.release();
    thunk->parameterTypes = parameterTypeStorage.release();
    PyObject *functionType =
        ctypesFunctionType(convention, resultType, parameterTypes);
    PyObject *callback = functionType
        ? PyObject_CallFunctionObjArgs(functionType, thunk, nullptr) : nullptr;
    Py_XDECREF(functionType);
    if (!callback) {
        Py_DECREF(reinterpret_cast<PyObject *>(thunk));
        return nullptr;
    }
    PyObject *addressObject = ctypesAddress(callback);
    void *rawAddress = nullptr;
    if (!addressObject || addressObject == Py_None ||
        !pointerFromPy(addressObject, &rawAddress) || !rawAddress) {
        Py_XDECREF(addressObject);
        Py_DECREF(callback);
        if (!PyErr_Occurred()) {
            PyErr_SetString(PyExc_RuntimeError,
                            "ctypes did not return a callback address");
        }
        return nullptr;
    }
    uintptr_t pointer = reinterpret_cast<uintptr_t>(rawAddress);
    {
        std::lock_guard<std::mutex> lock(ffiCallbackMutex);
        try {
            auto inserted = ffiCallbacks.emplace(
                pointer, FfiCallbackRecord{callback, thunk});
            if (!inserted.second) {
                Py_DECREF(callback);
                Py_DECREF(reinterpret_cast<PyObject *>(thunk));
                Py_DECREF(addressObject);
                PyErr_SetString(PyExc_RuntimeError,
                                "ctypes returned a duplicate callback address");
                return nullptr;
            }
        } catch (...) {
            Py_DECREF(callback);
            Py_DECREF(reinterpret_cast<PyObject *>(thunk));
            Py_DECREF(addressObject);
            PyErr_NoMemory();
            return nullptr;
        }
        Py_INCREF(thunk);
    }
    Py_DECREF(reinterpret_cast<PyObject *>(thunk));
    Py_DECREF(addressObject);
    return PyLong_FromUnsignedLongLong(static_cast<unsigned long long>(pointer));
}

PyObject *pyFfiFreeCallback(PyObject *, PyObject *args) {
    unsigned long long pointer;
    if (!PyArg_ParseTuple(args, "K", &pointer)) return nullptr;
    if (pointer > static_cast<unsigned long long>(
                       std::numeric_limits<uintptr_t>::max())) {
        PyErr_SetString(PyExc_OverflowError,
                        "callback address does not fit this process");
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(ffiCallbackMutex);
    auto it = ffiCallbacks.find(static_cast<uintptr_t>(pointer));
    if (it == ffiCallbacks.end()) {
        PyErr_SetString(PyExc_RuntimeError, "unknown native callback");
        return nullptr;
    }
    try {
        retiredFfiCallbacks.push_back(it->second);
    } catch (const std::exception &) {
        PyErr_NoMemory();
        return nullptr;
    }
    Py_XDECREF(it->second.thunk->target);
    it->second.thunk->target = nullptr;
    ffiCallbacks.erase(it);
    Py_RETURN_NONE;
}

#if 0
PyObject *legacyThreadStart(PyObject *, PyObject *args) {
    PyObject *callback;
    PyObject *callbackArgs;
    if (!PyArg_ParseTuple(args, "OO", &callback, &callbackArgs)) return nullptr;
    if (!PyList_Check(callbackArgs)) {
        PyErr_SetString(PyExc_TypeError, "nativeThreadStart arguments must be a list");
        return nullptr;
    }
    PyObject *execute = PyObject_GetAttrString(callback, "execute");
    if (execute == nullptr || !PyCallable_Check(execute)) {
        Py_XDECREF(execute);
        PyErr_SetString(PyExc_TypeError, "nativeThreadStart callback must be a Lynxer function");
        return nullptr;
    }
    Py_INCREF(callbackArgs);

    auto *thread = new (std::nothrow) NativeThread;
    if (thread == nullptr) {
        Py_DECREF(execute);
        Py_DECREF(callbackArgs);
        PyErr_SetString(PyExc_MemoryError, "could not allocate native thread");
        return nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(nativeThreadsMutex);
        nativeThreads.insert(thread);
    }
    thread->worker = std::thread([thread, execute, callbackArgs]() {
        PyGILState_STATE state = PyGILState_Ensure();
        PyObject *result = nullptr;
        try {
            result = PyObject_CallFunctionObjArgs(execute, callbackArgs, nullptr);
            if (result != nullptr) {
                PyObject *error = PyObject_GetAttrString(result, "error");
                bool failed = error != nullptr && PyObject_IsTrue(error) == 1;
                if (failed) {
                    PyObject *message = PyObject_CallMethod(error, "as_string", nullptr);
                    if (message == nullptr) {
                        PyErr_Clear();
                        message = PyObject_Str(error);
                    }
                    const char *text = message == nullptr ? "native thread callback failed"
                                                           : PyUnicode_AsUTF8(message);
                    std::lock_guard<std::mutex> statusLock(thread->statusMutex);
                    thread->status = text == nullptr ? "native thread callback failed" : text;
                    Py_XDECREF(message);
                } else {
                    std::lock_guard<std::mutex> statusLock(thread->statusMutex);
                    thread->status = "completed";
                }
                Py_XDECREF(error);
            } else {
                std::lock_guard<std::mutex> statusLock(thread->statusMutex);
                thread->status = "native thread callback raised an exception";
            }
        } catch (const std::exception &exception) {
            std::lock_guard<std::mutex> statusLock(thread->statusMutex);
            thread->status = exception.what();
        } catch (...) {
            std::lock_guard<std::mutex> statusLock(thread->statusMutex);
            thread->status = "native thread callback failed";
        }
        Py_XDECREF(result);
        PyErr_Clear();
        Py_DECREF(execute);
        Py_DECREF(callbackArgs);
        PyGILState_Release(state);
        thread->alive.store(false);
        thread->done.store(true);
        std::lock_guard<std::mutex> lock(nativeThreadsMutex);
        if (thread->detached.load()) {
            nativeThreads.erase(thread);
            delete thread;
        }
    });
    return PyLong_FromVoidPtr(thread);
}

NativeThread *legacyFindNativeThread(PyObject *handle) {
    void *raw = PyLong_AsVoidPtr(handle);
    if (PyErr_Occurred()) return nullptr;
    auto *thread = static_cast<NativeThread *>(raw);
    std::lock_guard<std::mutex> lock(nativeThreadsMutex);
    if (nativeThreads.find(thread) == nativeThreads.end()) {
        PyErr_SetString(PyExc_ValueError, "unknown or already released native thread");
        return nullptr;
    }
    return thread;
}

PyObject *legacyThreadJoin(PyObject *, PyObject *args) {
    PyObject *handle;
    if (!PyArg_ParseTuple(args, "O", &handle)) return nullptr;
    NativeThread *thread = findNativeThread(handle);
    if (thread == nullptr) return nullptr;
    if (thread->detached.load()) {
        PyErr_SetString(PyExc_RuntimeError, "cannot join a detached native thread");
        return nullptr;
    }
    Py_BEGIN_ALLOW_THREADS
    thread->worker.join();
    Py_END_ALLOW_THREADS
    {
        std::lock_guard<std::mutex> lock(nativeThreadsMutex);
        nativeThreads.erase(thread);
    }
    std::string status;
    {
        std::lock_guard<std::mutex> statusLock(thread->statusMutex);
        status = thread->status;
    }
    delete thread;
    return PyUnicode_FromString(status.c_str());
}

PyObject *legacyThreadIsAlive(PyObject *, PyObject *args) {
    PyObject *handle;
    if (!PyArg_ParseTuple(args, "O", &handle)) return nullptr;
    NativeThread *thread = findNativeThread(handle);
    if (thread == nullptr) return nullptr;
    if (thread->alive.load()) Py_RETURN_TRUE;
    Py_RETURN_FALSE;
}

PyObject *legacyThreadStatus(PyObject *, PyObject *args) {
    PyObject *handle;
    if (!PyArg_ParseTuple(args, "O", &handle)) return nullptr;
    NativeThread *thread = findNativeThread(handle);
    if (thread == nullptr) return nullptr;
    std::lock_guard<std::mutex> statusLock(thread->statusMutex);
    return PyUnicode_FromString(thread->status.c_str());
}

PyObject *legacyThreadDetach(PyObject *, PyObject *args) {
    PyObject *handle;
    if (!PyArg_ParseTuple(args, "O", &handle)) return nullptr;
    NativeThread *thread = findNativeThread(handle);
    if (thread == nullptr) return nullptr;
    std::lock_guard<std::mutex> lock(nativeThreadsMutex);
    if (thread->detached.exchange(true)) {
        PyErr_SetString(PyExc_RuntimeError, "native thread is already detached");
        return nullptr;
    }
    thread->worker.detach();
    if (thread->done.load()) {
        nativeThreads.erase(thread);
        delete thread;
    }
    Py_RETURN_NONE;
}
#endif

std::shared_ptr<NativeThread> findNativeThread(PyObject *handle) {
    unsigned long long rawHandle;
    if (!PyArg_Parse(handle, "K", &rawHandle)) return nullptr;
    if (rawHandle == 0 ||
        rawHandle > static_cast<unsigned long long>(
                        std::numeric_limits<uintptr_t>::max())) {
        PyErr_SetString(PyExc_ValueError, "invalid native thread handle");
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(nativeThreadsMutex);
    auto it = nativeThreads.find(static_cast<uintptr_t>(rawHandle));
    if (it == nativeThreads.end()) {
        PyErr_SetString(PyExc_ValueError, "unknown or already released native thread");
        return nullptr;
    }
    return it->second;
}

void finishDetachedThread(const std::shared_ptr<NativeThread> &thread) {
    bool detached;
    {
        std::lock_guard<std::mutex> lifecycleLock(thread->lifecycleMutex);
        detached = thread->detached;
    }
    if (!detached) return;
    std::lock_guard<std::mutex> lock(nativeThreadsMutex);
    auto it = nativeThreads.find(thread->handle);
    if (it != nativeThreads.end() && it->second == thread) {
        nativeThreads.erase(it);
    }
}

PyObject *pyThreadStart(PyObject *, PyObject *args) {
    PyObject *callback;
    PyObject *callbackArgs;
    if (!PyArg_ParseTuple(args, "OO", &callback, &callbackArgs)) return nullptr;
    if (!PyList_Check(callbackArgs)) {
        PyErr_SetString(PyExc_TypeError, "nativeThreadStart arguments must be a list");
        return nullptr;
    }
    PyObject *execute = PyObject_GetAttrString(callback, "execute");
    if (!execute || !PyCallable_Check(execute)) {
        Py_XDECREF(execute);
        PyErr_SetString(PyExc_TypeError,
                        "nativeThreadStart callback must be a Lynxer function");
        return nullptr;
    }
    Py_INCREF(callbackArgs);
    std::shared_ptr<NativeThread> thread;
    try {
        thread = std::make_shared<NativeThread>();
    } catch (const std::exception &) {
        Py_DECREF(execute);
        Py_DECREF(callbackArgs);
        PyErr_NoMemory();
        return nullptr;
    }
    try {
        std::lock_guard<std::mutex> lock(nativeThreadsMutex);
        if (nextNativeThreadHandle == 0) {
            Py_DECREF(execute);
            Py_DECREF(callbackArgs);
            PyErr_SetString(PyExc_OverflowError,
                            "native thread handle space is exhausted");
            return nullptr;
        }
        thread->handle = nextNativeThreadHandle++;
        nativeThreads.emplace(thread->handle, thread);
    } catch (const std::exception &) {
        Py_DECREF(execute);
        Py_DECREF(callbackArgs);
        PyErr_NoMemory();
        return nullptr;
    }
    try {
        thread->worker = std::thread([thread, execute, callbackArgs]() {
            PyGILState_STATE state = PyGILState_Ensure();
            PyObject *result = nullptr;
            try {
                result = PyObject_CallFunctionObjArgs(execute, callbackArgs, nullptr);
                if (result != nullptr) {
                    PyObject *error = PyObject_GetAttrString(result, "error");
                    bool failed = error && PyObject_IsTrue(error) == 1;
                    if (failed) {
                        PyObject *message = PyObject_CallMethod(error, "as_string", nullptr);
                        if (!message) {
                            PyErr_Clear();
                            message = PyObject_Str(error);
                        }
                        const char *text = message ? PyUnicode_AsUTF8(message) : nullptr;
                        {
                            std::lock_guard<std::mutex> statusLock(thread->statusMutex);
                            thread->status = text ? text : "native thread callback failed";
                        }
                        Py_XDECREF(message);
                    } else {
                        std::lock_guard<std::mutex> statusLock(thread->statusMutex);
                        thread->status = "completed";
                    }
                    Py_XDECREF(error);
                } else {
                    std::lock_guard<std::mutex> statusLock(thread->statusMutex);
                    thread->status = "native thread callback raised an exception";
                    PyErr_Clear();
                }
            } catch (const std::exception &error) {
                std::lock_guard<std::mutex> statusLock(thread->statusMutex);
                thread->status = error.what();
            } catch (...) {
                std::lock_guard<std::mutex> statusLock(thread->statusMutex);
                thread->status = "native thread callback failed";
            }
            Py_XDECREF(result);
            PyErr_Clear();
            Py_DECREF(execute);
            Py_DECREF(callbackArgs);
            PyGILState_Release(state);
            thread->alive.store(false);
            thread->done.store(true);
            finishDetachedThread(thread);
        });
    } catch (const std::exception &error) {
        Py_DECREF(execute);
        Py_DECREF(callbackArgs);
        std::lock_guard<std::mutex> lock(nativeThreadsMutex);
        nativeThreads.erase(thread->handle);
        PyErr_Format(PyExc_RuntimeError, "could not start native thread: %s",
                     error.what());
        return nullptr;
    }
    return PyLong_FromUnsignedLongLong(
        static_cast<unsigned long long>(thread->handle));
}

PyObject *pyThreadJoin(PyObject *, PyObject *args) {
    PyObject *handle;
    if (!PyArg_ParseTuple(args, "O", &handle)) return nullptr;
    auto thread = findNativeThread(handle);
    if (!thread) return nullptr;
    std::unique_lock<std::mutex> lifecycleLock(thread->lifecycleMutex);
    if (thread->detached) {
        PyErr_SetString(PyExc_RuntimeError, "cannot join a detached native thread");
        return nullptr;
    }
    if (thread->joined) {
        PyErr_SetString(PyExc_RuntimeError, "native thread has already been joined");
        return nullptr;
    }
    if (thread->worker.get_id() == std::this_thread::get_id()) {
        PyErr_SetString(PyExc_RuntimeError, "native thread cannot join itself");
        return nullptr;
    }
    thread->joined = true;
    std::string joinError;
    PyThreadState *savedState = PyEval_SaveThread();
    try {
        thread->worker.join();
    } catch (const std::exception &error) {
        joinError = error.what();
    } catch (...) {
        joinError = "unknown native thread join failure";
    }
    PyEval_RestoreThread(savedState);
    if (!joinError.empty()) {
        thread->joined = false;
        PyErr_SetString(PyExc_RuntimeError, joinError.c_str());
        return nullptr;
    }
    lifecycleLock.unlock();
    {
        std::lock_guard<std::mutex> lock(nativeThreadsMutex);
        auto it = nativeThreads.find(thread->handle);
        if (it != nativeThreads.end() && it->second == thread) {
            nativeThreads.erase(it);
        }
    }
    std::lock_guard<std::mutex> statusLock(thread->statusMutex);
    return PyUnicode_FromString(thread->status.c_str());
}

PyObject *pyThreadIsAlive(PyObject *, PyObject *args) {
    PyObject *handle;
    if (!PyArg_ParseTuple(args, "O", &handle)) return nullptr;
    auto thread = findNativeThread(handle);
    if (!thread) return nullptr;
    if (thread->alive.load()) Py_RETURN_TRUE;
    Py_RETURN_FALSE;
}

PyObject *pyThreadStatus(PyObject *, PyObject *args) {
    PyObject *handle;
    if (!PyArg_ParseTuple(args, "O", &handle)) return nullptr;
    auto thread = findNativeThread(handle);
    if (!thread) return nullptr;
    std::lock_guard<std::mutex> statusLock(thread->statusMutex);
    return PyUnicode_FromString(thread->status.c_str());
}

PyObject *pyThreadDetach(PyObject *, PyObject *args) {
    PyObject *handle;
    if (!PyArg_ParseTuple(args, "O", &handle)) return nullptr;
    auto thread = findNativeThread(handle);
    if (!thread) return nullptr;
    std::unique_lock<std::mutex> lifecycleLock(thread->lifecycleMutex);
    if (thread->joined) {
        PyErr_SetString(PyExc_RuntimeError, "native thread has already been joined");
        return nullptr;
    }
    if (thread->detached) {
        PyErr_SetString(PyExc_RuntimeError, "native thread is already detached");
        return nullptr;
    }
    try {
        thread->worker.detach();
        thread->detached = true;
    } catch (const std::exception &error) {
        PyErr_SetString(PyExc_RuntimeError, error.what());
        return nullptr;
    }
    bool done = thread->done.load();
    lifecycleLock.unlock();
    if (done) finishDetachedThread(thread);
    Py_RETURN_NONE;
}

PyObject *pyMalloc(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    unsigned long long size;
    if (!PyArg_ParseTuple(args, "K", &size)) return nullptr;
    if (size > static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
        PyErr_SetString(PyExc_OverflowError, "allocation size is too large");
        return nullptr;
    }
    void *ptr = std::malloc(static_cast<size_t>(size));
    if (ptr == nullptr && size != 0) return PyErr_NoMemory();
    trackAllocation(ptr, static_cast<size_t>(size));
    return PyLong_FromUnsignedLongLong(
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(ptr))
    );
}

PyObject *pyCalloc(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    unsigned long long count;
    unsigned long long size;
    if (!PyArg_ParseTuple(args, "KK", &count, &size)) return nullptr;
    if (count != 0 && size > std::numeric_limits<size_t>::max() / count) {
        PyErr_SetString(PyExc_OverflowError, "allocation size is too large");
        return nullptr;
    }
    void *ptr = std::calloc(static_cast<size_t>(count), static_cast<size_t>(size));
    if (ptr == nullptr && count != 0 && size != 0) return PyErr_NoMemory();
    trackAllocation(ptr, static_cast<size_t>(count * size));
    return PyLong_FromUnsignedLongLong(
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(ptr))
    );
}

PyObject *pyRealloc(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *ptrObject;
    unsigned long long size;
    if (!PyArg_ParseTuple(args, "OK", &ptrObject, &size)) return nullptr;
    void *ptr;
    if (!pointerFromPy(ptrObject, &ptr)) return nullptr;
    if (size > static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
        PyErr_SetString(PyExc_OverflowError, "allocation size is too large");
        return nullptr;
    }
    if (ptr != nullptr && !validateMemory(ptr, 0, 0)) return nullptr;
    void *newPtr = std::realloc(ptr, static_cast<size_t>(size));
    if (newPtr == nullptr && size != 0) return PyErr_NoMemory();
    if (ptr != nullptr) {
        allocations.erase(ptr);
        typedBlocks.erase(ptr);
        structBlocks.erase(ptr);
        if (newPtr != ptr) freedAllocations.insert(ptr);
    }
    trackAllocation(newPtr, static_cast<size_t>(size));
    return PyLong_FromUnsignedLongLong(
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(newPtr))
    );
}

PyObject *pyFree(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *ptrObject;
    if (!PyArg_ParseTuple(args, "O", &ptrObject)) return nullptr;
    void *ptr;
    if (!pointerFromPy(ptrObject, &ptr)) return nullptr;
    if (!validateMemory(ptr, 0, 0)) return nullptr;
    std::free(ptr);
    allocations.erase(ptr);
    typedBlocks.erase(ptr);
    structBlocks.erase(ptr);
    freedAllocations.insert(ptr);
    Py_RETURN_NONE;
}

PyObject *pyMemset(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *ptrObject;
    int value;
    unsigned long long size;
    if (!PyArg_ParseTuple(args, "OiK", &ptrObject, &value, &size)) return nullptr;
    if (size > static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
        PyErr_SetString(PyExc_OverflowError, "memset size is too large");
        return nullptr;
    }
    void *ptr;
    if (!pointerFromPy(ptrObject, &ptr)) return nullptr;
    if (!validateMemory(ptr, 0, static_cast<size_t>(size))) return nullptr;
    std::memset(ptr, value, static_cast<size_t>(size));
    Py_RETURN_NONE;
}

PyObject *pyMemcpy(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *destinationObject;
    PyObject *sourceObject;
    unsigned long long size;
    if (!PyArg_ParseTuple(args, "OOK", &destinationObject, &sourceObject, &size)) {
        return nullptr;
    }
    if (size > static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
        PyErr_SetString(PyExc_OverflowError, "memcpy size is too large");
        return nullptr;
    }
    void *destination;
    void *source;
    if (!pointerFromPy(destinationObject, &destination) ||
        !pointerFromPy(sourceObject, &source)) return nullptr;
    if (!validateMemory(destination, 0, static_cast<size_t>(size)) ||
        !validateMemory(source, 0, static_cast<size_t>(size))) return nullptr;
    std::memcpy(destination, source, static_cast<size_t>(size));
    Py_RETURN_NONE;
}

PyObject *pyReadByte(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *ptrObject;
    unsigned long long offset;
    if (!PyArg_ParseTuple(args, "OK", &ptrObject, &offset)) return nullptr;
    void *ptr;
    if (!pointerFromPy(ptrObject, &ptr)) return nullptr;
    if (!validateMemory(ptr, static_cast<size_t>(offset), 1)) return nullptr;
    auto *bytes = static_cast<unsigned char *>(ptr);
    return PyLong_FromUnsignedLong(static_cast<unsigned long>(bytes[offset]));
}

PyObject *pyWriteByte(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *ptrObject;
    unsigned long long offset;
    unsigned int value;
    if (!PyArg_ParseTuple(args, "OKI", &ptrObject, &offset, &value)) return nullptr;
    if (value > 255) {
        PyErr_SetString(PyExc_ValueError, "byte value must be between 0 and 255");
        return nullptr;
    }
    void *ptr;
    if (!pointerFromPy(ptrObject, &ptr)) return nullptr;
    if (!validateMemory(ptr, static_cast<size_t>(offset), 1)) return nullptr;
    static_cast<unsigned char *>(ptr)[offset] = static_cast<unsigned char>(value);
    Py_RETURN_NONE;
}

template <typename T>
PyObject *readFixed(PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *ptrObject;
    unsigned long long offset;
    if (!PyArg_ParseTuple(args, "OK", &ptrObject, &offset)) return nullptr;
    void *ptr;
    if (!pointerFromPy(ptrObject, &ptr)) return nullptr;
    if (!validateMemory(ptr, static_cast<size_t>(offset), sizeof(T))) return nullptr;
    T value;
    std::memcpy(&value, static_cast<unsigned char *>(ptr) + offset, sizeof(T));
    if constexpr (std::is_signed_v<T>) {
        return PyLong_FromLongLong(static_cast<long long>(value));
    }
    return PyLong_FromUnsignedLongLong(static_cast<unsigned long long>(value));
}

template <typename T>
PyObject *writeFixed(PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *ptrObject;
    unsigned long long offset;
    PyObject *valueObject;
    if (!PyArg_ParseTuple(args, "OKO", &ptrObject, &offset, &valueObject)) {
        return nullptr;
    }
    void *ptr;
    if (!pointerFromPy(ptrObject, &ptr)) return nullptr;
    if (!validateMemory(ptr, static_cast<size_t>(offset), sizeof(T))) return nullptr;
    T value;
    if constexpr (std::is_signed_v<T>) {
        long long raw = PyLong_AsLongLong(valueObject);
        if (PyErr_Occurred()) return nullptr;
        if (raw < static_cast<long long>(std::numeric_limits<T>::min()) ||
            raw > static_cast<long long>(std::numeric_limits<T>::max())) {
            PyErr_SetString(PyExc_OverflowError, "value is outside the range for typed memory");
            return nullptr;
        }
        value = static_cast<T>(raw);
    } else {
        unsigned long long raw = PyLong_AsUnsignedLongLong(valueObject);
        if (PyErr_Occurred()) return nullptr;
        if (raw > static_cast<unsigned long long>(std::numeric_limits<T>::max())) {
            PyErr_SetString(PyExc_OverflowError, "value is outside the range for typed memory");
            return nullptr;
        }
        value = static_cast<T>(raw);
    }
    std::memcpy(static_cast<unsigned char *>(ptr) + offset, &value, sizeof(T));
    Py_RETURN_NONE;
}

template <typename T>
PyObject *readFloat(PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *ptrObject;
    unsigned long long offset;
    if (!PyArg_ParseTuple(args, "OK", &ptrObject, &offset)) return nullptr;
    void *ptr;
    if (!pointerFromPy(ptrObject, &ptr)) return nullptr;
    if (!validateMemory(ptr, static_cast<size_t>(offset), sizeof(T))) return nullptr;
    T value;
    std::memcpy(&value, static_cast<unsigned char *>(ptr) + offset, sizeof(T));
    return PyFloat_FromDouble(static_cast<double>(value));
}

template <typename T>
PyObject *writeFloat(PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *ptrObject;
    unsigned long long offset;
    double rawValue;
    if (!PyArg_ParseTuple(args, "OKd", &ptrObject, &offset, &rawValue)) return nullptr;
    T value = static_cast<T>(rawValue);
    if (!std::isfinite(rawValue) || !std::isfinite(static_cast<double>(value))) {
        PyErr_SetString(PyExc_OverflowError, "floating-point value is out of range");
        return nullptr;
    }
    void *ptr;
    if (!pointerFromPy(ptrObject, &ptr)) return nullptr;
    if (!validateMemory(ptr, static_cast<size_t>(offset), sizeof(T))) return nullptr;
    std::memcpy(static_cast<unsigned char *>(ptr) + offset, &value, sizeof(T));
    Py_RETURN_NONE;
}

PyObject *pyReadFloat32(PyObject *, PyObject *args) { return readFloat<float>(args); }
PyObject *pyWriteFloat32(PyObject *, PyObject *args) { return writeFloat<float>(args); }
PyObject *pyReadFloat64(PyObject *, PyObject *args) { return readFloat<double>(args); }
PyObject *pyWriteFloat64(PyObject *, PyObject *args) { return writeFloat<double>(args); }

PyObject *pyMemoryReadEndian(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *addressObject;
    unsigned long long offset;
    const char *type;
    const char *order;
    if (!PyArg_ParseTuple(args, "OKss", &addressObject, &offset, &type, &order)) {
        return nullptr;
    }
    MemoryType info;
    bool little;
    if (!memoryType(type, &info)) {
        PyErr_SetString(PyExc_ValueError, "unsupported typed memory type");
        return nullptr;
    }
    if (!parseByteOrder(order, &little)) {
        PyErr_SetString(PyExc_ValueError, "byte order must be 'little' or 'big'");
        return nullptr;
    }
    void *raw;
    if (!pointerFromPy(addressObject, &raw) ||
        !validateMemory(raw, static_cast<size_t>(offset), info.size)) {
        return nullptr;
    }
    auto *bytes = static_cast<unsigned char *>(raw) + offset;
    if (std::strcmp(type, "float32") == 0) {
        return PyFloat_FromDouble(loadOrdered<float>(bytes, little));
    }
    if (std::strcmp(type, "float64") == 0) {
        return PyFloat_FromDouble(loadOrdered<double>(bytes, little));
    }
    if (std::strcmp(type, "byte") == 0 || std::strcmp(type, "uint8") == 0) {
        return PyLong_FromUnsignedLong(loadOrdered<std::uint8_t>(bytes, little));
    }
    if (std::strcmp(type, "int8") == 0) {
        return PyLong_FromLong(loadOrdered<std::int8_t>(bytes, little));
    }
    if (std::strcmp(type, "int16") == 0) {
        return PyLong_FromLong(loadOrdered<std::int16_t>(bytes, little));
    }
    if (std::strcmp(type, "uint16") == 0) {
        return PyLong_FromUnsignedLong(loadOrdered<std::uint16_t>(bytes, little));
    }
    if (std::strcmp(type, "int32") == 0) {
        return PyLong_FromLongLong(loadOrdered<std::int32_t>(bytes, little));
    }
    if (std::strcmp(type, "uint32") == 0) {
        return PyLong_FromUnsignedLongLong(loadOrdered<std::uint32_t>(bytes, little));
    }
    if (std::strcmp(type, "int64") == 0) {
        return PyLong_FromLongLong(loadOrdered<std::int64_t>(bytes, little));
    }
    if (std::strcmp(type, "uintptr") == 0 ||
        std::strcmp(type, "pointer") == 0 ||
        std::strcmp(type, "functionPointer") == 0) {
        return PyLong_FromUnsignedLongLong(loadOrdered<uintptr_t>(bytes, little));
    }
    return PyLong_FromUnsignedLongLong(loadOrdered<std::uint64_t>(bytes, little));
}

PyObject *pyMemoryWriteEndian(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *addressObject;
    unsigned long long offset;
    const char *type;
    const char *order;
    PyObject *valueObject;
    if (!PyArg_ParseTuple(
            args, "OKssO", &addressObject, &offset, &type, &order, &valueObject)) {
        return nullptr;
    }
    MemoryType info;
    bool little;
    if (!memoryType(type, &info)) {
        PyErr_SetString(PyExc_ValueError, "unsupported typed memory type");
        return nullptr;
    }
    if (!parseByteOrder(order, &little)) {
        PyErr_SetString(PyExc_ValueError, "byte order must be 'little' or 'big'");
        return nullptr;
    }
    void *raw;
    if (!pointerFromPy(addressObject, &raw) ||
        !validateMemory(raw, static_cast<size_t>(offset), info.size)) {
        return nullptr;
    }
    auto *bytes = static_cast<unsigned char *>(raw) + offset;
    if (std::strcmp(type, "float32") == 0 ||
        std::strcmp(type, "float64") == 0) {
        double value = PyFloat_AsDouble(valueObject);
        if (PyErr_Occurred() || !std::isfinite(value)) {
            if (!PyErr_Occurred()) {
                PyErr_SetString(PyExc_OverflowError, "floating-point value is out of range");
            }
            return nullptr;
        }
        if (std::strcmp(type, "float32") == 0) {
            float narrowed = static_cast<float>(value);
            if (!std::isfinite(narrowed)) {
                PyErr_SetString(PyExc_OverflowError, "floating-point value is out of range");
                return nullptr;
            }
            storeOrdered(bytes, narrowed, little);
        } else {
            storeOrdered(bytes, value, little);
        }
        Py_RETURN_NONE;
    }
    if (std::strcmp(type, "byte") == 0 || std::strcmp(type, "uint8") == 0) {
        unsigned long long value = PyLong_AsUnsignedLongLong(valueObject);
        if (PyErr_Occurred() || value > 255) {
            if (!PyErr_Occurred()) {
                PyErr_SetString(PyExc_OverflowError, "value is outside the range for typed memory");
            }
            return nullptr;
        }
        storeOrdered(bytes, static_cast<std::uint8_t>(value), little);
    } else if (std::strcmp(type, "int8") == 0) {
        long long value = PyLong_AsLongLong(valueObject);
        if (PyErr_Occurred() || value < INT8_MIN || value > INT8_MAX) {
            if (!PyErr_Occurred()) {
                PyErr_SetString(PyExc_OverflowError, "value is outside the range for typed memory");
            }
            return nullptr;
        }
        storeOrdered(bytes, static_cast<std::int8_t>(value), little);
    } else if (std::strcmp(type, "int16") == 0) {
        long long value = PyLong_AsLongLong(valueObject);
        if (PyErr_Occurred() || value < INT16_MIN || value > INT16_MAX) {
            if (!PyErr_Occurred()) {
                PyErr_SetString(PyExc_OverflowError, "value is outside the range for typed memory");
            }
            return nullptr;
        }
        storeOrdered(bytes, static_cast<std::int16_t>(value), little);
    } else if (std::strcmp(type, "uint16") == 0) {
        unsigned long long value = PyLong_AsUnsignedLongLong(valueObject);
        if (PyErr_Occurred() || value > UINT16_MAX) {
            if (!PyErr_Occurred()) {
                PyErr_SetString(PyExc_OverflowError, "value is outside the range for typed memory");
            }
            return nullptr;
        }
        storeOrdered(bytes, static_cast<std::uint16_t>(value), little);
    } else if (std::strcmp(type, "int32") == 0) {
        long long value = PyLong_AsLongLong(valueObject);
        if (PyErr_Occurred() || value < INT32_MIN || value > INT32_MAX) {
            if (!PyErr_Occurred()) {
                PyErr_SetString(PyExc_OverflowError, "value is outside the range for typed memory");
            }
            return nullptr;
        }
        storeOrdered(bytes, static_cast<std::int32_t>(value), little);
    } else if (std::strcmp(type, "uint32") == 0) {
        unsigned long long value = PyLong_AsUnsignedLongLong(valueObject);
        if (PyErr_Occurred() || value > UINT32_MAX) {
            if (!PyErr_Occurred()) {
                PyErr_SetString(PyExc_OverflowError, "value is outside the range for typed memory");
            }
            return nullptr;
        }
        storeOrdered(bytes, static_cast<std::uint32_t>(value), little);
    } else if (std::strcmp(type, "int64") == 0) {
        long long value = PyLong_AsLongLong(valueObject);
        if (PyErr_Occurred()) return nullptr;
        storeOrdered(bytes, static_cast<std::int64_t>(value), little);
    } else if (std::strcmp(type, "uintptr") == 0 ||
               std::strcmp(type, "pointer") == 0 ||
               std::strcmp(type, "functionPointer") == 0) {
        unsigned long long value = PyLong_AsUnsignedLongLong(valueObject);
        if (PyErr_Occurred() ||
            value > static_cast<unsigned long long>(
                        std::numeric_limits<uintptr_t>::max())) {
            if (!PyErr_Occurred()) {
                PyErr_SetString(PyExc_OverflowError,
                                "pointer value does not fit this process");
            }
            return nullptr;
        }
        storeOrdered(bytes, static_cast<uintptr_t>(value), little);
    } else {
        unsigned long long value = PyLong_AsUnsignedLongLong(valueObject);
        if (PyErr_Occurred()) return nullptr;
        storeOrdered(bytes, static_cast<std::uint64_t>(value), little);
    }
    Py_RETURN_NONE;
}

PyObject *pyMemoryTypeSize(PyObject *, PyObject *args) {
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return nullptr;
    MemoryType info;
    if (!memoryType(name, &info)) {
        PyErr_SetString(PyExc_ValueError, "unsupported typed memory type");
        return nullptr;
    }
    return PyLong_FromSize_t(info.size);
}

PyObject *pyMemoryTypeAlignment(PyObject *, PyObject *args) {
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return nullptr;
    MemoryType info;
    if (!memoryType(name, &info)) {
        PyErr_SetString(PyExc_ValueError, "unsupported typed memory type");
        return nullptr;
    }
    return PyLong_FromSize_t(info.alignment);
}

PyObject *pyMemoryBlockAllocate(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    const char *name;
    unsigned long long count;
    if (!PyArg_ParseTuple(args, "sK", &name, &count)) return nullptr;
    MemoryType info;
    if (!memoryType(name, &info)) {
        PyErr_SetString(PyExc_ValueError, "unsupported typed memory type");
        return nullptr;
    }
    if (count > std::numeric_limits<size_t>::max() / info.size) {
        PyErr_SetString(PyExc_OverflowError, "typed allocation size is too large");
        return nullptr;
    }
    void *ptr = std::malloc(static_cast<size_t>(count) * info.size);
    if (!ptr && count) return PyErr_NoMemory();
    trackAllocation(ptr, static_cast<size_t>(count) * info.size);
    typedBlocks[ptr] = {name, static_cast<size_t>(count)};
    return PyLong_FromUnsignedLongLong(reinterpret_cast<uintptr_t>(ptr));
}

PyObject *pyMemoryBlockView(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *addressObject;
    const char *name;
    unsigned long long count;
    if (!PyArg_ParseTuple(args, "OsK", &addressObject, &name, &count)) return nullptr;
    void *ptr;
    if (!pointerFromPy(addressObject, &ptr)) return nullptr;
    MemoryType info;
    if (!memoryType(name, &info)) {
        PyErr_SetString(PyExc_ValueError, "unsupported typed memory type");
        return nullptr;
    }
    if (count > std::numeric_limits<size_t>::max() / info.size) {
        PyErr_SetString(PyExc_OverflowError, "typed view size is too large");
        return nullptr;
    }
    if (!validateMemory(ptr, 0, static_cast<size_t>(count) * info.size)) {
        return nullptr;
    }
    typedBlocks[ptr] = {name, static_cast<size_t>(count)};
    return PyLong_FromUnsignedLongLong(reinterpret_cast<uintptr_t>(ptr));
}

PyObject *pyMemoryBlockLength(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *addressObject;
    if (!PyArg_ParseTuple(args, "O", &addressObject)) return nullptr;
    void *ptr;
    if (!pointerFromPy(addressObject, &ptr)) return nullptr;
    auto it = typedBlocks.find(ptr);
    if (it == typedBlocks.end()) {
        PyErr_SetString(PyExc_RuntimeError, "address is not a typed memory block");
        return nullptr;
    }
    return PyLong_FromSize_t(it->second.count);
}

PyObject *pyMemoryBlockGet(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *addressObject, *indexObject;
    if (!PyArg_ParseTuple(args, "OO", &addressObject, &indexObject)) return nullptr;
    void *ptr; size_t offset; std::string type;
    if (!blockField(addressObject, indexObject, &ptr, &offset, &type)) return nullptr;
    MemoryType info;
    memoryType(type, &info);
    if (!validateMemory(ptr, offset, info.size)) return nullptr;
    PyObject *pair = Py_BuildValue("(OK)", addressObject, static_cast<unsigned long long>(offset));
    if (!pair) return nullptr;
    PyObject *result = nullptr;
    if (type == "float32") result = readFloat<float>(pair);
    else if (type == "float64") result = readFloat<double>(pair);
    else if (type == "int8") result = readFixed<std::int8_t>(pair);
    else if (type == "uint8" || type == "byte") result = readFixed<std::uint8_t>(pair);
    else if (type == "int16") result = readFixed<std::int16_t>(pair);
    else if (type == "uint16") result = readFixed<std::uint16_t>(pair);
    else if (type == "int32") result = readFixed<std::int32_t>(pair);
    else if (type == "uint32") result = readFixed<std::uint32_t>(pair);
    else if (type == "int64") result = readFixed<std::int64_t>(pair);
    else if (type == "uintptr" || type == "pointer" ||
             type == "functionPointer") result = readFixed<uintptr_t>(pair);
    else result = readFixed<std::uint64_t>(pair);
    Py_DECREF(pair);
    return result;
}

PyObject *pyMemoryBlockSet(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *addressObject, *indexObject, *valueObject;
    if (!PyArg_ParseTuple(args, "OOO", &addressObject, &indexObject, &valueObject)) return nullptr;
    void *ptr; size_t offset; std::string type;
    if (!blockField(addressObject, indexObject, &ptr, &offset, &type)) return nullptr;
    MemoryType info; memoryType(type, &info);
    if (!validateMemory(ptr, offset, info.size)) return nullptr;
    PyObject *pair = Py_BuildValue("(OKO)", addressObject, offset, valueObject);
    if (!pair) return nullptr;
    PyObject *result = nullptr;
    if (type == "float32") result = writeFloat<float>(pair);
    else if (type == "float64") result = writeFloat<double>(pair);
    else if (type == "int8") result = writeFixed<std::int8_t>(pair);
    else if (type == "uint8" || type == "byte") result = writeFixed<std::uint8_t>(pair);
    else if (type == "int16") result = writeFixed<std::int16_t>(pair);
    else if (type == "uint16") result = writeFixed<std::uint16_t>(pair);
    else if (type == "int32") result = writeFixed<std::int32_t>(pair);
    else if (type == "uint32") result = writeFixed<std::uint32_t>(pair);
    else if (type == "int64") result = writeFixed<std::int64_t>(pair);
    else if (type == "uintptr" || type == "pointer" ||
             type == "functionPointer") result = writeFixed<uintptr_t>(pair);
    else result = writeFixed<std::uint64_t>(pair);
    Py_DECREF(pair);
    return result;
}

PyObject *pyMemoryStructSize(PyObject *, PyObject *args) {
    PyObject *layoutObject;
    if (!PyArg_ParseTuple(args, "O", &layoutObject)) return nullptr;
    StructLayout layout;
    if (!layoutFromObject(layoutObject, &layout)) return nullptr;
    return PyLong_FromSize_t(layout.size);
}

PyObject *pyMemoryStructAlignment(PyObject *, PyObject *args) {
    PyObject *layoutObject;
    if (!PyArg_ParseTuple(args, "O", &layoutObject)) return nullptr;
    StructLayout layout;
    if (!layoutFromObject(layoutObject, &layout)) return nullptr;
    return PyLong_FromSize_t(layoutAlignment(layout));
}

PyObject *pyMemoryStructFieldCount(PyObject *, PyObject *args) {
    PyObject *layoutObject;
    if (!PyArg_ParseTuple(args, "O", &layoutObject)) return nullptr;
    StructLayout layout;
    if (!layoutFromObject(layoutObject, &layout)) return nullptr;
    return PyLong_FromSize_t(layout.fields.size());
}

PyObject *pyMemoryStructFieldType(PyObject *, PyObject *args) {
    PyObject *layoutObject;
    const char *field;
    if (!PyArg_ParseTuple(args, "Os", &layoutObject, &field)) return nullptr;
    StructLayout layout;
    if (!layoutFromObject(layoutObject, &layout)) return nullptr;
    for (const auto &item : layout.fields) {
        if (item.name == field) {
            return PyUnicode_FromString(item.type.c_str());
        }
    }
    PyErr_SetString(PyExc_ValueError, "struct field is not present");
    return nullptr;
}

PyObject *pyMemoryStructAllocate(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *layoutObject;
    if (!PyArg_ParseTuple(args, "O", &layoutObject)) return nullptr;
    StructLayout layout;
    if (!layoutFromObject(layoutObject, &layout)) return nullptr;
    void *ptr = std::calloc(1, layout.size);
    if (!ptr && layout.size) return PyErr_NoMemory();
    trackAllocation(ptr, layout.size);
    structBlocks[ptr] = layout;
    return PyLong_FromUnsignedLongLong(reinterpret_cast<uintptr_t>(ptr));
}

PyObject *pyMemoryStructField(PyObject *, PyObject *args, bool wantSize) {
    PyObject *layoutObject; const char *field;
    if (!PyArg_ParseTuple(args, "Os", &layoutObject, &field)) return nullptr;
    StructLayout layout;
    if (!layoutFromObject(layoutObject, &layout)) return nullptr;
    for (size_t i = 0; i < layout.fields.size(); ++i) {
        if (layout.fields[i].name == field) {
            return PyLong_FromSize_t(
                wantSize ? layout.fields[i].size : layout.fields[i].offset);
        }
    }
    PyErr_SetString(PyExc_ValueError, "struct field is not present");
    return nullptr;
}

PyObject *pyMemoryStructGet(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *addressObject; const char *field;
    if (!PyArg_ParseTuple(args, "Os", &addressObject, &field)) return nullptr;
    void *ptr; if (!pointerFromPy(addressObject, &ptr)) return nullptr;
    auto block = structBlocks.find(ptr);
    if (block == structBlocks.end()) {
        PyErr_SetString(PyExc_RuntimeError, "address is not a memory struct");
        return nullptr;
    }
    for (const auto &item : block->second.fields) {
        if (item.name == field) {
            MemoryType info;
            if (!memoryType(item.type, &info) ||
                !validateMemory(ptr, item.offset, info.size)) {
                if (!memoryType(item.type, &info)) {
                    PyErr_SetString(PyExc_ValueError,
                        "struct field is an aggregate; access its native address and size");
                }
                return nullptr;
            }
            PyObject *pair = Py_BuildValue("(OK)", addressObject,
                static_cast<unsigned long long>(item.offset));
            if (!pair) return nullptr;
            PyObject *result = nullptr;
            if (item.type == "float32") result = readFloat<float>(pair);
            else if (item.type == "float64") result = readFloat<double>(pair);
            else if (item.type == "int32") result = readFixed<std::int32_t>(pair);
            else if (item.type == "uint32") result = readFixed<std::uint32_t>(pair);
            else if (item.type == "int64") result = readFixed<std::int64_t>(pair);
            else if (item.type == "uint64") result = readFixed<std::uint64_t>(pair);
            else if (item.type == "int16") result = readFixed<std::int16_t>(pair);
            else if (item.type == "uint16") result = readFixed<std::uint16_t>(pair);
            else if (item.type == "int8") result = readFixed<std::int8_t>(pair);
            else if (item.type == "uintptr" || item.type == "pointer" ||
                     item.type == "functionPointer")
                result = readFixed<uintptr_t>(pair);
            else result = readFixed<std::uint8_t>(pair);
            Py_DECREF(pair);
            return result;
        }
    }
    PyErr_SetString(PyExc_ValueError, "struct field is not present");
    return nullptr;
}

PyObject *pyMemoryStructSet(PyObject *, PyObject *args) {
    std::lock_guard<std::recursive_mutex> memoryLock(memoryMutex);
    PyObject *addressObject, *valueObject; const char *field;
    if (!PyArg_ParseTuple(args, "OsO", &addressObject, &field, &valueObject)) return nullptr;
    void *ptr; if (!pointerFromPy(addressObject, &ptr)) return nullptr;
    auto block = structBlocks.find(ptr);
    if (block == structBlocks.end()) {
        PyErr_SetString(PyExc_RuntimeError, "address is not a memory struct");
        return nullptr;
    }
    for (const auto &item : block->second.fields) {
        if (item.name == field) {
            MemoryType info;
            if (!memoryType(item.type, &info) ||
                !validateMemory(ptr, item.offset, info.size)) {
                if (!memoryType(item.type, &info)) {
                    PyErr_SetString(PyExc_ValueError,
                        "struct field is an aggregate; write its native address directly");
                }
                return nullptr;
            }
            PyObject *triple = Py_BuildValue("(OKO)", addressObject,
                static_cast<unsigned long long>(item.offset), valueObject);
            if (!triple) return nullptr;
            PyObject *result = nullptr;
            if (item.type == "float32") result = writeFloat<float>(triple);
            else if (item.type == "float64") result = writeFloat<double>(triple);
            else if (item.type == "int32") result = writeFixed<std::int32_t>(triple);
            else if (item.type == "uint32") result = writeFixed<std::uint32_t>(triple);
            else if (item.type == "int64") result = writeFixed<std::int64_t>(triple);
            else if (item.type == "uint64") result = writeFixed<std::uint64_t>(triple);
            else if (item.type == "int16") result = writeFixed<std::int16_t>(triple);
            else if (item.type == "uint16") result = writeFixed<std::uint16_t>(triple);
            else if (item.type == "int8") result = writeFixed<std::int8_t>(triple);
            else if (item.type == "uintptr" || item.type == "pointer" ||
                     item.type == "functionPointer")
                result = writeFixed<uintptr_t>(triple);
            else result = writeFixed<std::uint8_t>(triple);
            Py_DECREF(triple);
            return result;
        }
    }
    PyErr_SetString(PyExc_ValueError, "struct field is not present");
    return nullptr;
}

#define FIXED_INTEGER_FUNCTIONS(NAME, TYPE) \
    PyObject *pyRead##NAME(PyObject *, PyObject *args) { return readFixed<TYPE>(args); } \
    PyObject *pyWrite##NAME(PyObject *, PyObject *args) { return writeFixed<TYPE>(args); }

FIXED_INTEGER_FUNCTIONS(Int8, std::int8_t)
FIXED_INTEGER_FUNCTIONS(Int16, std::int16_t)
FIXED_INTEGER_FUNCTIONS(Int32, std::int32_t)
FIXED_INTEGER_FUNCTIONS(Int64, std::int64_t)
FIXED_INTEGER_FUNCTIONS(UInt8, std::uint8_t)
FIXED_INTEGER_FUNCTIONS(UInt16, std::uint16_t)
FIXED_INTEGER_FUNCTIONS(UInt32, std::uint32_t)
FIXED_INTEGER_FUNCTIONS(UInt64, std::uint64_t)

#undef FIXED_INTEGER_FUNCTIONS

PyObject *pySizeOf(PyObject *, PyObject *args) {
    const char *typeName;
    if (!PyArg_ParseTuple(args, "s", &typeName)) return nullptr;
    size_t size = 0;
    if (std::strcmp(typeName, "char") == 0) size = sizeof(char);
    else if (std::strcmp(typeName, "short") == 0) size = sizeof(short);
    else if (std::strcmp(typeName, "int") == 0) size = sizeof(int);
    else if (std::strcmp(typeName, "long") == 0) size = sizeof(long);
    else if (std::strcmp(typeName, "long long") == 0) size = sizeof(long long);
    else if (std::strcmp(typeName, "float") == 0) size = sizeof(float);
    else if (std::strcmp(typeName, "double") == 0) size = sizeof(double);
    else if (std::strcmp(typeName, "void*") == 0) size = sizeof(void *);
    else if (std::strcmp(typeName, "size_t") == 0) size = sizeof(size_t);
    else if (std::strcmp(typeName, "uintptr_t") == 0) size = sizeof(uintptr_t);
    else if (std::strcmp(typeName, "int8") == 0) size = sizeof(std::int8_t);
    else if (std::strcmp(typeName, "int16") == 0) size = sizeof(std::int16_t);
    else if (std::strcmp(typeName, "int32") == 0) size = sizeof(std::int32_t);
    else if (std::strcmp(typeName, "int64") == 0) size = sizeof(std::int64_t);
    else if (std::strcmp(typeName, "uint8") == 0) size = sizeof(std::uint8_t);
    else if (std::strcmp(typeName, "uint16") == 0) size = sizeof(std::uint16_t);
    else if (std::strcmp(typeName, "uint32") == 0) size = sizeof(std::uint32_t);
    else if (std::strcmp(typeName, "uint64") == 0) size = sizeof(std::uint64_t);
    else {
        PyErr_Format(PyExc_ValueError, "unknown C type '%s'", typeName);
        return nullptr;
    }
    return PyLong_FromSize_t(size);
}

#define SAFE_NATIVE_METHOD(function) safeNativeMethod<function>

template <PyObject *(*Function)(PyObject *, PyObject *)>
PyObject *safeNativeMethod(PyObject *self, PyObject *args) noexcept {
    try {
        return Function(self, args);
    } catch (const std::bad_alloc &) {
        PyErr_NoMemory();
        return nullptr;
    } catch (const std::exception &error) {
        PyErr_Format(PyExc_RuntimeError, "native extension failure: %s",
                     error.what());
        return nullptr;
    } catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "native extension failure");
        return nullptr;
    }
}

PyObject *pyMemoryStructFieldOffset(PyObject *self, PyObject *args) {
    return pyMemoryStructField(self, args, false);
}

PyObject *pyMemoryStructFieldSize(PyObject *self, PyObject *args) {
    return pyMemoryStructField(self, args, true);
}

PyMethodDef methods[] = {
    {"refCreate", SAFE_NATIVE_METHOD(pyRefCreate), METH_VARARGS, "Create a native Lynxer reference cell."},
    {"refGet", SAFE_NATIVE_METHOD(pyRefGet), METH_VARARGS, "Read a native Lynxer reference cell."},
    {"refSet", SAFE_NATIVE_METHOD(pyRefSet), METH_VARARGS, "Write a native Lynxer reference cell."},
    {"refFree", SAFE_NATIVE_METHOD(pyRefFree), METH_VARARGS, "Free a native Lynxer reference cell."},
    {"nativeCall", SAFE_NATIVE_METHOD(pyNativeCall), METH_VARARGS, "Call a low-level native function address."},
    {"ffiCall", SAFE_NATIVE_METHOD(pyFfiCall), METH_VARARGS, "Call a typed native function address."},
    {"ffiCallback", SAFE_NATIVE_METHOD(pyFfiCallback), METH_VARARGS, "Create a native callback trampoline."},
    {"ffiFreeCallback", SAFE_NATIVE_METHOD(pyFfiFreeCallback), METH_VARARGS, "Release a native callback trampoline."},
    {"nativeModuleLoad", SAFE_NATIVE_METHOD(pyNativeModuleLoad), METH_VARARGS, "Load and initialize a Lynxer native module."},
    {"nativeModuleClose", SAFE_NATIVE_METHOD(pyNativeModuleClose), METH_VARARGS, "Close a Lynxer native module."},
    {"ffiLoadLibrary", SAFE_NATIVE_METHOD(pyFfiLoadLibrary), METH_VARARGS, "Load a dynamic library."},
    {"ffiLookup", SAFE_NATIVE_METHOD(pyFfiLookup), METH_VARARGS, "Resolve a dynamic library symbol."},
    {"ffiCloseLibrary", SAFE_NATIVE_METHOD(pyFfiCloseLibrary), METH_VARARGS, "Close a dynamic library."},
    {"nativeThreadStart", SAFE_NATIVE_METHOD(pyThreadStart), METH_VARARGS, "Start a native thread running a Lynxer function."},
    {"nativeThreadJoin", SAFE_NATIVE_METHOD(pyThreadJoin), METH_VARARGS, "Join a native Lynxer thread."},
    {"nativeThreadIsAlive", SAFE_NATIVE_METHOD(pyThreadIsAlive), METH_VARARGS, "Check whether a native Lynxer thread is running."},
    {"nativeThreadStatus", SAFE_NATIVE_METHOD(pyThreadStatus), METH_VARARGS, "Get the status of a native Lynxer thread."},
    {"nativeThreadDetach", SAFE_NATIVE_METHOD(pyThreadDetach), METH_VARARGS, "Detach a native Lynxer thread."},
    {"atomicLoad", SAFE_NATIVE_METHOD(pyAtomicLoad), METH_VARARGS, "Atomically load a native integer."},
    {"atomicStore", SAFE_NATIVE_METHOD(pyAtomicStore), METH_VARARGS, "Atomically store a native integer."},
    {"atomicAdd", SAFE_NATIVE_METHOD(pyAtomicAdd), METH_VARARGS, "Atomically add to a native integer."},
    {"volatileRead", SAFE_NATIVE_METHOD(pyVolatileRead), METH_VARARGS, "Read native memory as volatile."},
    {"volatileWrite", SAFE_NATIVE_METHOD(pyVolatileWrite), METH_VARARGS, "Write native memory as volatile."},
    {"memoryProtect", SAFE_NATIVE_METHOD(pyMemoryProtect), METH_VARARGS, "Change native memory protection."},
    {"memoryAllocate", SAFE_NATIVE_METHOD(pyMalloc), METH_VARARGS, "Allocate raw memory."},
    {"memoryAllocateZeroed", SAFE_NATIVE_METHOD(pyCalloc), METH_VARARGS, "Allocate zero-initialized raw memory."},
    {"memoryReallocate", SAFE_NATIVE_METHOD(pyRealloc), METH_VARARGS, "Resize raw memory."},
    {"memoryFree", SAFE_NATIVE_METHOD(pyFree), METH_VARARGS, "Free raw memory."},
    {"memorySet", SAFE_NATIVE_METHOD(pyMemset), METH_VARARGS, "Fill raw memory."},
    {"memoryCopy", SAFE_NATIVE_METHOD(pyMemcpy), METH_VARARGS, "Copy raw memory."},
    {"memoryReadByte", SAFE_NATIVE_METHOD(pyReadByte), METH_VARARGS, "Read one byte."},
    {"memoryWriteByte", SAFE_NATIVE_METHOD(pyWriteByte), METH_VARARGS, "Write one byte."},
    {"memoryReadInt8", SAFE_NATIVE_METHOD(pyReadInt8), METH_VARARGS, "Read signed 8-bit integer."},
    {"memoryWriteInt8", SAFE_NATIVE_METHOD(pyWriteInt8), METH_VARARGS, "Write signed 8-bit integer."},
    {"memoryReadInt16", SAFE_NATIVE_METHOD(pyReadInt16), METH_VARARGS, "Read signed 16-bit integer."},
    {"memoryWriteInt16", SAFE_NATIVE_METHOD(pyWriteInt16), METH_VARARGS, "Write signed 16-bit integer."},
    {"memoryReadInt32", SAFE_NATIVE_METHOD(pyReadInt32), METH_VARARGS, "Read signed 32-bit integer."},
    {"memoryWriteInt32", SAFE_NATIVE_METHOD(pyWriteInt32), METH_VARARGS, "Write signed 32-bit integer."},
    {"memoryReadInt64", SAFE_NATIVE_METHOD(pyReadInt64), METH_VARARGS, "Read signed 64-bit integer."},
    {"memoryWriteInt64", SAFE_NATIVE_METHOD(pyWriteInt64), METH_VARARGS, "Write signed 64-bit integer."},
    {"memoryReadUInt8", SAFE_NATIVE_METHOD(pyReadUInt8), METH_VARARGS, "Read unsigned 8-bit integer."},
    {"memoryWriteUInt8", SAFE_NATIVE_METHOD(pyWriteUInt8), METH_VARARGS, "Write unsigned 8-bit integer."},
    {"memoryReadUInt16", SAFE_NATIVE_METHOD(pyReadUInt16), METH_VARARGS, "Read unsigned 16-bit integer."},
    {"memoryWriteUInt16", SAFE_NATIVE_METHOD(pyWriteUInt16), METH_VARARGS, "Write unsigned 16-bit integer."},
    {"memoryReadUInt32", SAFE_NATIVE_METHOD(pyReadUInt32), METH_VARARGS, "Read unsigned 32-bit integer."},
    {"memoryWriteUInt32", SAFE_NATIVE_METHOD(pyWriteUInt32), METH_VARARGS, "Write unsigned 32-bit integer."},
    {"memoryReadUInt64", SAFE_NATIVE_METHOD(pyReadUInt64), METH_VARARGS, "Read unsigned 64-bit integer."},
    {"memoryWriteUInt64", SAFE_NATIVE_METHOD(pyWriteUInt64), METH_VARARGS, "Write unsigned 64-bit integer."},
    {"memoryReadFloat32", SAFE_NATIVE_METHOD(pyReadFloat32), METH_VARARGS, "Read 32-bit float."},
    {"memoryWriteFloat32", SAFE_NATIVE_METHOD(pyWriteFloat32), METH_VARARGS, "Write 32-bit float."},
    {"memoryReadFloat64", SAFE_NATIVE_METHOD(pyReadFloat64), METH_VARARGS, "Read 64-bit float."},
    {"memoryWriteFloat64", SAFE_NATIVE_METHOD(pyWriteFloat64), METH_VARARGS, "Write 64-bit float."},
    {"memoryReadEndian", SAFE_NATIVE_METHOD(pyMemoryReadEndian), METH_VARARGS, "Read a value with explicit byte order."},
    {"memoryWriteEndian", SAFE_NATIVE_METHOD(pyMemoryWriteEndian), METH_VARARGS, "Write a value with explicit byte order."},
    {"memoryTypeSize", SAFE_NATIVE_METHOD(pyMemoryTypeSize), METH_VARARGS, "Return a typed memory size."},
    {"memoryTypeAlignment", SAFE_NATIVE_METHOD(pyMemoryTypeAlignment), METH_VARARGS, "Return a typed memory alignment."},
    {"memoryBlockAllocate", SAFE_NATIVE_METHOD(pyMemoryBlockAllocate), METH_VARARGS, "Allocate a typed block."},
    {"memoryBlockView", SAFE_NATIVE_METHOD(pyMemoryBlockView), METH_VARARGS, "Create a typed view."},
    {"memoryBlockLength", SAFE_NATIVE_METHOD(pyMemoryBlockLength), METH_VARARGS, "Return typed block length."},
    {"memoryBlockGet", SAFE_NATIVE_METHOD(pyMemoryBlockGet), METH_VARARGS, "Read typed block element."},
    {"memoryBlockSet", SAFE_NATIVE_METHOD(pyMemoryBlockSet), METH_VARARGS, "Write typed block element."},
    {"memoryStructSize", SAFE_NATIVE_METHOD(pyMemoryStructSize), METH_VARARGS, "Return native struct size."},
    {"memoryStructAlignment", SAFE_NATIVE_METHOD(pyMemoryStructAlignment), METH_VARARGS, "Return native struct alignment."},
    {"memoryStructFieldCount", SAFE_NATIVE_METHOD(pyMemoryStructFieldCount), METH_VARARGS, "Return native struct field count."},
    {"memoryStructFieldType", SAFE_NATIVE_METHOD(pyMemoryStructFieldType), METH_VARARGS, "Return native struct field type."},
    {"memoryStructAllocate", SAFE_NATIVE_METHOD(pyMemoryStructAllocate), METH_VARARGS, "Allocate native struct."},
    {"memoryStructFieldOffset", SAFE_NATIVE_METHOD(pyMemoryStructFieldOffset), METH_VARARGS, "Return native struct field offset."},
    {"memoryStructFieldSize", SAFE_NATIVE_METHOD(pyMemoryStructFieldSize), METH_VARARGS, "Return native struct field size."},
    {"memoryStructGet", SAFE_NATIVE_METHOD(pyMemoryStructGet), METH_VARARGS, "Read native struct field."},
    {"memoryStructSet", SAFE_NATIVE_METHOD(pyMemoryStructSet), METH_VARARGS, "Write native struct field."},
    {"sizeOf", SAFE_NATIVE_METHOD(pySizeOf), METH_VARARGS, "Return the size of a C type."},
    {nullptr, nullptr, 0, nullptr}
};

PyModuleDef module = {
    PyModuleDef_HEAD_INIT,
    "cpp",
    "Low-level C++ memory primitives for Lynxer.",
    -1,
    methods,
};

} // namespace

PyMODINIT_FUNC PyInit_cpp() {
    FfiCallbackCallableType.tp_name = "lynxer.cpp.FfiCallbackCallable";
    FfiCallbackCallableType.tp_basicsize = sizeof(FfiCallbackCallable);
    FfiCallbackCallableType.tp_flags = Py_TPFLAGS_DEFAULT;
    FfiCallbackCallableType.tp_dealloc =
        reinterpret_cast<destructor>(ffiCallbackCallableDealloc);
    FfiCallbackCallableType.tp_call =
        reinterpret_cast<ternaryfunc>(ffiCallbackCallableCall);
    FfiCallbackCallableType.tp_free = PyObject_Free;
    if (PyType_Ready(&FfiCallbackCallableType) < 0) return nullptr;
    PyObject *created = PyModule_Create(&module);
    if (!created) return nullptr;
    Py_INCREF(&FfiCallbackCallableType);
    if (PyModule_AddObject(created, "_FfiCallbackCallable",
                           reinterpret_cast<PyObject *>(&FfiCallbackCallableType)) < 0) {
        Py_DECREF(&FfiCallbackCallableType);
        Py_DECREF(created);
        return nullptr;
    }
    return created;
}