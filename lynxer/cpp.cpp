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
#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif
#if defined(__linux__)
#include <cerrno>
#include <sys/syscall.h>
#endif
#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace {

struct ReferenceCell {
    PyObject *value;
};

struct NativeThread {
    std::thread worker;
    std::atomic<bool> alive{true};
    std::atomic<bool> done{false};
    std::atomic<bool> detached{false};
    std::string status{"running"};
    std::mutex statusMutex;
};

std::mutex nativeThreadsMutex;
std::unordered_set<NativeThread *> nativeThreads;

bool pointerFromPy(PyObject *obj, void **out) {
    unsigned long long value = PyLong_AsUnsignedLongLong(obj);
    if (PyErr_Occurred()) {
        return false;
    }
    *out = reinterpret_cast<void *>(static_cast<uintptr_t>(value));
    return true;
}

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
    void *handle;
    std::string path;
    std::unordered_map<std::string, std::pair<uintptr_t, std::string>> functions;
    std::unordered_map<std::string, std::int64_t> constants;
    std::unordered_map<std::string, std::string> types;
};

std::unordered_map<std::int64_t, NativeLibrary *> nativeLibraries;
std::int64_t nextNativeLibrary = 1;
std::unordered_map<uintptr_t, PyObject *> ffiCallbacks;
std::int64_t nextFfiCallback = 1;

bool memoryType(const std::string &name, MemoryType *out) {
    if (name == "byte" || name == "int8" || name == "uint8") *out = {1, 1};
    else if (name == "int16" || name == "uint16") *out = {2, 2};
    else if (name == "int32" || name == "uint32" || name == "float32") *out = {4, 4};
    else if (name == "int64" || name == "uint64" || name == "float64") *out = {8, 8};
    else if (name == "uintptr" || name == "pointer" || name == "functionPointer")
        *out = {sizeof(uintptr_t), alignof(uintptr_t)};
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
           name == "uintptr";
}

bool parseNativeSignature(const char *signature, std::string *result,
                          std::vector<std::string> *parameters) {
    std::string text(signature);
    size_t open = text.find('(');
    if (open == std::string::npos || text.back() != ')' || open == 0) {
        PyErr_SetString(PyExc_ValueError,
            "native signature must be returnType(type,...)");
        return false;
    }
    *result = text.substr(0, open);
    if (*result != "void" && !nativeIntegerType(*result)) {
        PyErr_SetString(PyExc_ValueError,
            "native call supports void and integer/pointer return types");
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
        if (!nativeIntegerType(parameter)) {
            PyErr_SetString(PyExc_ValueError,
                "native call parameters must be integer or pointer types");
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
        else if (character == '}') --braces;
        else if (character == '[') ++brackets;
        else if (character == ']') --brackets;
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
        size_t fieldOffset = unionLayout ? 0 :
            (offset + info.alignment - 1) / info.alignment * info.alignment;
        out->fields.push_back({name, type, fieldOffset, info.size, info.alignment});
        if (unionLayout) offset = std::max(offset, info.size);
        else offset = fieldOffset + info.size;
        alignment = std::max(alignment, info.alignment);
    }
    out->size = (offset + alignment - 1) / alignment * alignment;
    return true;
}

bool layoutFromObject(PyObject *object, StructLayout *out) {
    const char *raw;
    if (!PyArg_Parse(object, "s", &raw)) return false;
    return layoutFromText(raw, out, false);
}

bool blockField(PyObject *object, PyObject *indexObject, void **ptr, size_t *offset,
                std::string *type) {
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
    if (ptr != nullptr) {
        allocations[ptr] = size;
        freedAllocations.erase(ptr);
    }
}

PyObject *pyRefCreate(PyObject *, PyObject *args) {
    PyObject *value;
    if (!PyArg_ParseTuple(args, "O", &value)) return nullptr;
    auto *cell = static_cast<ReferenceCell *>(std::malloc(sizeof(ReferenceCell)));
    if (cell == nullptr) return PyErr_NoMemory();
    Py_INCREF(value);
    cell->value = value;
    return PyLong_FromUnsignedLongLong(
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(cell))
    );
}

PyObject *pyRefGet(PyObject *, PyObject *args) {
    PyObject *pointerObject;
    if (!PyArg_ParseTuple(args, "O", &pointerObject)) return nullptr;
    void *raw;
    if (!pointerFromPy(pointerObject, &raw)) return nullptr;
    auto *cell = static_cast<ReferenceCell *>(raw);
    if (cell == nullptr || cell->value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "invalid Lynxer reference pointer");
        return nullptr;
    }
    Py_INCREF(cell->value);
    return cell->value;
}

PyObject *pyRefSet(PyObject *, PyObject *args) {
    PyObject *pointerObject;
    PyObject *value;
    if (!PyArg_ParseTuple(args, "OO", &pointerObject, &value)) return nullptr;
    void *raw;
    if (!pointerFromPy(pointerObject, &raw)) return nullptr;
    auto *cell = static_cast<ReferenceCell *>(raw);
    if (cell == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "invalid Lynxer reference pointer");
        return nullptr;
    }
    Py_INCREF(value);
    Py_XDECREF(cell->value);
    cell->value = value;
    Py_RETURN_NONE;
}

PyObject *pyRefFree(PyObject *, PyObject *args) {
    PyObject *pointerObject;
    if (!PyArg_ParseTuple(args, "O", &pointerObject)) return nullptr;
    void *raw;
    if (!pointerFromPy(pointerObject, &raw)) return nullptr;
    auto *cell = static_cast<ReferenceCell *>(raw);
    if (cell != nullptr) {
        Py_XDECREF(cell->value);
        std::free(cell);
    }
    Py_RETURN_NONE;
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

    void *address;
    if (!pointerFromPy(addressObject, &address)) return nullptr;
    if (address == nullptr) {
        PyErr_SetString(PyExc_ValueError, "native function address must be non-zero");
        return nullptr;
    }
    if (!PyList_Check(valuesObject)) {
        PyErr_SetString(PyExc_TypeError, "native call arguments must be a list");
        return nullptr;
    }

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
    values.reserve(static_cast<size_t>(count));
    for (Py_ssize_t index = 0; index < count; ++index) {
        PyObject *value = PyList_GET_ITEM(valuesObject, index);
        unsigned long long converted;
        const std::string &parameterType = parameterTypes[static_cast<size_t>(index)];
        if (parameterType == "uint8" || parameterType == "uint16" ||
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
    return PyLong_FromUnsignedLongLong(static_cast<unsigned long long>(result));
}

PyObject *invokeLinuxSyscall(long number, PyObject *args) {
#if !defined(__linux__)
    PyErr_SetString(PyExc_NotImplementedError,
        "named syscalls are only available on Linux");
    return nullptr;
#else
    PyObject *valuesObject;
    if (!PyArg_ParseTuple(args, "O", &valuesObject)) return nullptr;
    if (!PyList_Check(valuesObject)) {
        PyErr_SetString(PyExc_TypeError, "syscall arguments must be a list");
        return nullptr;
    }
    Py_ssize_t count = PyList_GET_SIZE(valuesObject);
    if (count > 6) {
        PyErr_SetString(PyExc_ValueError, "syscalls accept at most six arguments");
        return nullptr;
    }
    unsigned long long values[6] = {};
    for (Py_ssize_t index = 0; index < count; ++index) {
        values[index] = PyLong_AsUnsignedLongLongMask(PyList_GET_ITEM(valuesObject, index));
        if (PyErr_Occurred()) return nullptr;
    }
    errno = 0;
    long result;
    switch (count) {
    case 0: result = ::syscall(number); break;
    case 1: result = ::syscall(number, values[0]); break;
    case 2: result = ::syscall(number, values[0], values[1]); break;
    case 3: result = ::syscall(number, values[0], values[1], values[2]); break;
    case 4: result = ::syscall(number, values[0], values[1], values[2], values[3]); break;
    case 5: result = ::syscall(number, values[0], values[1], values[2], values[3], values[4]); break;
    default: result = ::syscall(number, values[0], values[1], values[2], values[3], values[4], values[5]); break;
    }
    if (result == -1 && errno != 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return nullptr;
    }
    return PyLong_FromLong(result);
#endif
}

#if defined(__linux__)
#define NAMED_SYSCALL(NAME, NUMBER) \
    PyObject *py##NAME(PyObject *, PyObject *args) { return invokeLinuxSyscall(NUMBER, args); }
#else
#define NAMED_SYSCALL(NAME, NUMBER) \
    PyObject *py##NAME(PyObject *, PyObject *args) { return invokeLinuxSyscall(0, args); }
#endif

NAMED_SYSCALL(SyscallRead, SYS_read)
NAMED_SYSCALL(SyscallWrite, SYS_write)
NAMED_SYSCALL(SyscallOpenAt, SYS_openat)
NAMED_SYSCALL(SyscallClose, SYS_close)
NAMED_SYSCALL(SyscallReadVector, SYS_readv)
NAMED_SYSCALL(SyscallWriteVector, SYS_writev)
NAMED_SYSCALL(SyscallSeekFile, SYS_lseek)
NAMED_SYSCALL(SyscallGetFileStatus, SYS_fstat)
NAMED_SYSCALL(SyscallGetFileStatusAt, SYS_newfstatat)
NAMED_SYSCALL(SyscallTruncateFile, SYS_ftruncate)
NAMED_SYSCALL(SyscallSynchronizeFile, SYS_fsync)
NAMED_SYSCALL(SyscallSynchronizeFileData, SYS_fdatasync)
NAMED_SYSCALL(SyscallDuplicateFileDescriptor, SYS_dup)
NAMED_SYSCALL(SyscallDuplicateFileDescriptorAt, SYS_dup3)
NAMED_SYSCALL(SyscallCreatePipe, SYS_pipe2)
NAMED_SYSCALL(SyscallControlFileDescriptor, SYS_fcntl)
NAMED_SYSCALL(SyscallGetDirectoryEntries, SYS_getdents64)
NAMED_SYSCALL(SyscallReadSymbolicLink, SYS_readlinkat)
NAMED_SYSCALL(SyscallCreateDirectoryAt, SYS_mkdirat)
NAMED_SYSCALL(SyscallRemoveFileAt, SYS_unlinkat)
NAMED_SYSCALL(SyscallRenameFileAt, SYS_renameat)
NAMED_SYSCALL(SyscallCreateHardLinkAt, SYS_linkat)
NAMED_SYSCALL(SyscallCreateSymbolicLinkAt, SYS_symlinkat)
NAMED_SYSCALL(SyscallChangeFilePermissions, SYS_fchmodat)
NAMED_SYSCALL(SyscallChangeFileDescriptorPermissions, SYS_fchmod)
NAMED_SYSCALL(SyscallChangeFileOwner, SYS_fchownat)
NAMED_SYSCALL(SyscallChangeFileDescriptorOwner, SYS_fchown)
NAMED_SYSCALL(SyscallMemoryMap, SYS_mmap)
NAMED_SYSCALL(SyscallMemoryUnmap, SYS_munmap)
NAMED_SYSCALL(SyscallMemoryProtect, SYS_mprotect)
NAMED_SYSCALL(SyscallMemoryAdvise, SYS_madvise)
NAMED_SYSCALL(SyscallMemoryRemap, SYS_mremap)
NAMED_SYSCALL(SyscallAdjustProgramBreak, SYS_brk)
NAMED_SYSCALL(SyscallExecuteProgram, SYS_execve)
NAMED_SYSCALL(SyscallExecuteProgramAt, SYS_execveat)
NAMED_SYSCALL(SyscallExitProcess, SYS_exit)
NAMED_SYSCALL(SyscallExitAllThreads, SYS_exit_group)
NAMED_SYSCALL(SyscallWaitForProcess, SYS_wait4)
NAMED_SYSCALL(SyscallGetProcessId, SYS_getpid)
NAMED_SYSCALL(SyscallGetParentProcessId, SYS_getppid)
NAMED_SYSCALL(SyscallSendSignal, SYS_kill)
NAMED_SYSCALL(SyscallCreateThread, SYS_clone)
NAMED_SYSCALL(SyscallGetThreadId, SYS_gettid)
NAMED_SYSCALL(SyscallWaitOnMemory, SYS_futex)
NAMED_SYSCALL(SyscallSetThreadIdAddress, SYS_set_tid_address)
NAMED_SYSCALL(SyscallSetRobustThreadList, SYS_set_robust_list)
NAMED_SYSCALL(SyscallGetRobustThreadList, SYS_get_robust_list)
NAMED_SYSCALL(SyscallYieldProcessor, SYS_sched_yield)
NAMED_SYSCALL(SyscallGetClockTime, SYS_clock_gettime)
NAMED_SYSCALL(SyscallGetClockResolution, SYS_clock_getres)
NAMED_SYSCALL(SyscallSleep, SYS_nanosleep)
NAMED_SYSCALL(SyscallGetRandomBytes, SYS_getrandom)
NAMED_SYSCALL(SyscallCreateSocket, SYS_socket)
NAMED_SYSCALL(SyscallCreateSocketPair, SYS_socketpair)
NAMED_SYSCALL(SyscallBindSocket, SYS_bind)
NAMED_SYSCALL(SyscallListenSocket, SYS_listen)
NAMED_SYSCALL(SyscallAcceptConnection, SYS_accept)
NAMED_SYSCALL(SyscallConnectSocket, SYS_connect)
NAMED_SYSCALL(SyscallSendData, SYS_sendto)
NAMED_SYSCALL(SyscallReceiveData, SYS_recvfrom)
NAMED_SYSCALL(SyscallSendMessage, SYS_sendmsg)
NAMED_SYSCALL(SyscallReceiveMessage, SYS_recvmsg)
NAMED_SYSCALL(SyscallShutdownSocket, SYS_shutdown)
NAMED_SYSCALL(SyscallGetSocketAddress, SYS_getsockname)
NAMED_SYSCALL(SyscallGetPeerAddress, SYS_getpeername)
NAMED_SYSCALL(SyscallSetSocketOption, SYS_setsockopt)
NAMED_SYSCALL(SyscallGetSocketOption, SYS_getsockopt)
NAMED_SYSCALL(SyscallPollFileDescriptors, SYS_poll)
NAMED_SYSCALL(SyscallCreateEventPoll, SYS_epoll_create1)
NAMED_SYSCALL(SyscallControlEventPoll, SYS_epoll_ctl)
NAMED_SYSCALL(SyscallWaitForEvents, SYS_epoll_wait)
NAMED_SYSCALL(SyscallGetSystemInformation, SYS_sysinfo)
NAMED_SYSCALL(SyscallGetResourceUsage, SYS_getrusage)
NAMED_SYSCALL(SyscallGetResourceLimit, SYS_getrlimit)
NAMED_SYSCALL(SyscallSetResourceLimit, SYS_setrlimit)
NAMED_SYSCALL(SyscallControlProcess, SYS_prctl)

#undef NAMED_SYSCALL

PyObject *pyThreadStart(PyObject *, PyObject *args) {
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

NativeThread *findNativeThread(PyObject *handle) {
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

PyObject *pyThreadJoin(PyObject *, PyObject *args) {
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

PyObject *pyThreadIsAlive(PyObject *, PyObject *args) {
    PyObject *handle;
    if (!PyArg_ParseTuple(args, "O", &handle)) return nullptr;
    NativeThread *thread = findNativeThread(handle);
    if (thread == nullptr) return nullptr;
    if (thread->alive.load()) Py_RETURN_TRUE;
    Py_RETURN_FALSE;
}

PyObject *pyThreadStatus(PyObject *, PyObject *args) {
    PyObject *handle;
    if (!PyArg_ParseTuple(args, "O", &handle)) return nullptr;
    NativeThread *thread = findNativeThread(handle);
    if (thread == nullptr) return nullptr;
    std::lock_guard<std::mutex> statusLock(thread->statusMutex);
    return PyUnicode_FromString(thread->status.c_str());
}

PyObject *pyThreadDetach(PyObject *, PyObject *args) {
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

PyObject *pyMalloc(PyObject *, PyObject *args) {
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
    return PyLong_FromUnsignedLongLong(loadOrdered<std::uint64_t>(bytes, little));
}

PyObject *pyMemoryWriteEndian(PyObject *, PyObject *args) {
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
    else result = readFixed<std::uint64_t>(pair);
    Py_DECREF(pair);
    return result;
}

PyObject *pyMemoryBlockSet(PyObject *, PyObject *args) {
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

PyMethodDef methods[] = {
    {"refCreate", pyRefCreate, METH_VARARGS, "Create a native Lynxer reference cell."},
    {"refGet", pyRefGet, METH_VARARGS, "Read a native Lynxer reference cell."},
    {"refSet", pyRefSet, METH_VARARGS, "Write a native Lynxer reference cell."},
    {"refFree", pyRefFree, METH_VARARGS, "Free a native Lynxer reference cell."},
    {"nativeCall", pyNativeCall, METH_VARARGS, "Call an integer-ABI native function address."},
    {"syscallRead", pySyscallRead, METH_VARARGS, "Invoke read."},
    {"syscallWrite", pySyscallWrite, METH_VARARGS, "Invoke write."},
    {"syscallOpenAt", pySyscallOpenAt, METH_VARARGS, "Invoke openat."},
    {"syscallClose", pySyscallClose, METH_VARARGS, "Invoke close."},
    {"syscallReadVector", pySyscallReadVector, METH_VARARGS, "Invoke readv."},
    {"syscallWriteVector", pySyscallWriteVector, METH_VARARGS, "Invoke writev."},
    {"syscallSeekFile", pySyscallSeekFile, METH_VARARGS, "Invoke lseek."},
    {"syscallGetFileStatus", pySyscallGetFileStatus, METH_VARARGS, "Invoke fstat."},
    {"syscallGetFileStatusAt", pySyscallGetFileStatusAt, METH_VARARGS, "Invoke newfstatat."},
    {"syscallTruncateFile", pySyscallTruncateFile, METH_VARARGS, "Invoke ftruncate."},
    {"syscallSynchronizeFile", pySyscallSynchronizeFile, METH_VARARGS, "Invoke fsync."},
    {"syscallSynchronizeFileData", pySyscallSynchronizeFileData, METH_VARARGS, "Invoke fdatasync."},
    {"syscallDuplicateFileDescriptor", pySyscallDuplicateFileDescriptor, METH_VARARGS, "Invoke dup."},
    {"syscallDuplicateFileDescriptorAt", pySyscallDuplicateFileDescriptorAt, METH_VARARGS, "Invoke dup3."},
    {"syscallCreatePipe", pySyscallCreatePipe, METH_VARARGS, "Invoke pipe2."},
    {"syscallControlFileDescriptor", pySyscallControlFileDescriptor, METH_VARARGS, "Invoke fcntl."},
    {"syscallGetDirectoryEntries", pySyscallGetDirectoryEntries, METH_VARARGS, "Invoke getdents64."},
    {"syscallReadSymbolicLink", pySyscallReadSymbolicLink, METH_VARARGS, "Invoke readlinkat."},
    {"syscallCreateDirectoryAt", pySyscallCreateDirectoryAt, METH_VARARGS, "Invoke mkdirat."},
    {"syscallRemoveFileAt", pySyscallRemoveFileAt, METH_VARARGS, "Invoke unlinkat."},
    {"syscallRenameFileAt", pySyscallRenameFileAt, METH_VARARGS, "Invoke renameat."},
    {"syscallCreateHardLinkAt", pySyscallCreateHardLinkAt, METH_VARARGS, "Invoke linkat."},
    {"syscallCreateSymbolicLinkAt", pySyscallCreateSymbolicLinkAt, METH_VARARGS, "Invoke symlinkat."},
    {"syscallChangeFilePermissions", pySyscallChangeFilePermissions, METH_VARARGS, "Invoke fchmodat."},
    {"syscallChangeFileDescriptorPermissions", pySyscallChangeFileDescriptorPermissions, METH_VARARGS, "Invoke fchmod."},
    {"syscallChangeFileOwner", pySyscallChangeFileOwner, METH_VARARGS, "Invoke fchownat."},
    {"syscallChangeFileDescriptorOwner", pySyscallChangeFileDescriptorOwner, METH_VARARGS, "Invoke fchown."},
    {"syscallMemoryMap", pySyscallMemoryMap, METH_VARARGS, "Invoke mmap."},
    {"syscallMemoryUnmap", pySyscallMemoryUnmap, METH_VARARGS, "Invoke munmap."},
    {"syscallMemoryProtect", pySyscallMemoryProtect, METH_VARARGS, "Invoke mprotect."},
    {"syscallMemoryAdvise", pySyscallMemoryAdvise, METH_VARARGS, "Invoke madvise."},
    {"syscallMemoryRemap", pySyscallMemoryRemap, METH_VARARGS, "Invoke mremap."},
    {"syscallAdjustProgramBreak", pySyscallAdjustProgramBreak, METH_VARARGS, "Invoke brk."},
    {"syscallExecuteProgram", pySyscallExecuteProgram, METH_VARARGS, "Invoke execve."},
    {"syscallExecuteProgramAt", pySyscallExecuteProgramAt, METH_VARARGS, "Invoke execveat."},
    {"syscallExitProcess", pySyscallExitProcess, METH_VARARGS, "Invoke exit."},
    {"syscallExitAllThreads", pySyscallExitAllThreads, METH_VARARGS, "Invoke exit_group."},
    {"syscallWaitForProcess", pySyscallWaitForProcess, METH_VARARGS, "Invoke wait4."},
    {"syscallGetProcessId", pySyscallGetProcessId, METH_VARARGS, "Invoke getpid."},
    {"syscallGetParentProcessId", pySyscallGetParentProcessId, METH_VARARGS, "Invoke getppid."},
    {"syscallSendSignal", pySyscallSendSignal, METH_VARARGS, "Invoke kill."},
    {"syscallCreateThread", pySyscallCreateThread, METH_VARARGS, "Invoke clone."},
    {"syscallGetThreadId", pySyscallGetThreadId, METH_VARARGS, "Invoke gettid."},
    {"syscallWaitOnMemory", pySyscallWaitOnMemory, METH_VARARGS, "Invoke futex."},
    {"syscallSetThreadIdAddress", pySyscallSetThreadIdAddress, METH_VARARGS, "Invoke set_tid_address."},
    {"syscallSetRobustThreadList", pySyscallSetRobustThreadList, METH_VARARGS, "Invoke set_robust_list."},
    {"syscallGetRobustThreadList", pySyscallGetRobustThreadList, METH_VARARGS, "Invoke get_robust_list."},
    {"syscallYieldProcessor", pySyscallYieldProcessor, METH_VARARGS, "Invoke sched_yield."},
    {"syscallGetClockTime", pySyscallGetClockTime, METH_VARARGS, "Invoke clock_gettime."},
    {"syscallGetClockResolution", pySyscallGetClockResolution, METH_VARARGS, "Invoke clock_getres."},
    {"syscallSleep", pySyscallSleep, METH_VARARGS, "Invoke nanosleep."},
    {"syscallGetRandomBytes", pySyscallGetRandomBytes, METH_VARARGS, "Invoke getrandom."},
    {"syscallCreateSocket", pySyscallCreateSocket, METH_VARARGS, "Invoke socket."},
    {"syscallCreateSocketPair", pySyscallCreateSocketPair, METH_VARARGS, "Invoke socketpair."},
    {"syscallBindSocket", pySyscallBindSocket, METH_VARARGS, "Invoke bind."},
    {"syscallListenSocket", pySyscallListenSocket, METH_VARARGS, "Invoke listen."},
    {"syscallAcceptConnection", pySyscallAcceptConnection, METH_VARARGS, "Invoke accept."},
    {"syscallConnectSocket", pySyscallConnectSocket, METH_VARARGS, "Invoke connect."},
    {"syscallSendData", pySyscallSendData, METH_VARARGS, "Invoke sendto."},
    {"syscallReceiveData", pySyscallReceiveData, METH_VARARGS, "Invoke recvfrom."},
    {"syscallSendMessage", pySyscallSendMessage, METH_VARARGS, "Invoke sendmsg."},
    {"syscallReceiveMessage", pySyscallReceiveMessage, METH_VARARGS, "Invoke recvmsg."},
    {"syscallShutdownSocket", pySyscallShutdownSocket, METH_VARARGS, "Invoke shutdown."},
    {"syscallGetSocketAddress", pySyscallGetSocketAddress, METH_VARARGS, "Invoke getsockname."},
    {"syscallGetPeerAddress", pySyscallGetPeerAddress, METH_VARARGS, "Invoke getpeername."},
    {"syscallSetSocketOption", pySyscallSetSocketOption, METH_VARARGS, "Invoke setsockopt."},
    {"syscallGetSocketOption", pySyscallGetSocketOption, METH_VARARGS, "Invoke getsockopt."},
    {"syscallPollFileDescriptors", pySyscallPollFileDescriptors, METH_VARARGS, "Invoke poll."},
    {"syscallCreateEventPoll", pySyscallCreateEventPoll, METH_VARARGS, "Invoke epoll_create1."},
    {"syscallControlEventPoll", pySyscallControlEventPoll, METH_VARARGS, "Invoke epoll_ctl."},
    {"syscallWaitForEvents", pySyscallWaitForEvents, METH_VARARGS, "Invoke epoll_wait."},
    {"syscallGetSystemInformation", pySyscallGetSystemInformation, METH_VARARGS, "Invoke sysinfo."},
    {"syscallGetResourceUsage", pySyscallGetResourceUsage, METH_VARARGS, "Invoke getrusage."},
    {"syscallGetResourceLimit", pySyscallGetResourceLimit, METH_VARARGS, "Invoke getrlimit."},
    {"syscallSetResourceLimit", pySyscallSetResourceLimit, METH_VARARGS, "Invoke setrlimit."},
    {"syscallControlProcess", pySyscallControlProcess, METH_VARARGS, "Invoke prctl."},
    {"nativeThreadStart", pyThreadStart, METH_VARARGS, "Start a native thread running a Lynxer function."},
    {"nativeThreadJoin", pyThreadJoin, METH_VARARGS, "Join a native Lynxer thread."},
    {"nativeThreadIsAlive", pyThreadIsAlive, METH_VARARGS, "Check whether a native Lynxer thread is running."},
    {"nativeThreadStatus", pyThreadStatus, METH_VARARGS, "Get the status of a native Lynxer thread."},
    {"nativeThreadDetach", pyThreadDetach, METH_VARARGS, "Detach a native Lynxer thread."},
    {"atomicLoad", pyAtomicLoad, METH_VARARGS, "Atomically load a native integer."},
    {"atomicStore", pyAtomicStore, METH_VARARGS, "Atomically store a native integer."},
    {"atomicAdd", pyAtomicAdd, METH_VARARGS, "Atomically add to a native integer."},
    {"volatileRead", pyVolatileRead, METH_VARARGS, "Read native memory as volatile."},
    {"volatileWrite", pyVolatileWrite, METH_VARARGS, "Write native memory as volatile."},
    {"memoryProtect", pyMemoryProtect, METH_VARARGS, "Change native memory protection."},
    {"malloc", pyMalloc, METH_VARARGS, "Allocate raw memory."},
    {"calloc", pyCalloc, METH_VARARGS, "Allocate zero-initialized raw memory."},
    {"realloc", pyRealloc, METH_VARARGS, "Resize raw memory."},
    {"free", pyFree, METH_VARARGS, "Free raw memory."},
    {"memset", pyMemset, METH_VARARGS, "Fill raw memory."},
    {"memcpy", pyMemcpy, METH_VARARGS, "Copy raw memory."},
    {"readByte", pyReadByte, METH_VARARGS, "Read one byte."},
    {"writeByte", pyWriteByte, METH_VARARGS, "Write one byte."},
    {"readInt8", pyReadInt8, METH_VARARGS, "Read signed 8-bit integer."},
    {"writeInt8", pyWriteInt8, METH_VARARGS, "Write signed 8-bit integer."},
    {"readInt16", pyReadInt16, METH_VARARGS, "Read signed 16-bit integer."},
    {"writeInt16", pyWriteInt16, METH_VARARGS, "Write signed 16-bit integer."},
    {"readInt32", pyReadInt32, METH_VARARGS, "Read signed 32-bit integer."},
    {"writeInt32", pyWriteInt32, METH_VARARGS, "Write signed 32-bit integer."},
    {"readInt64", pyReadInt64, METH_VARARGS, "Read signed 64-bit integer."},
    {"writeInt64", pyWriteInt64, METH_VARARGS, "Write signed 64-bit integer."},
    {"readUInt8", pyReadUInt8, METH_VARARGS, "Read unsigned 8-bit integer."},
    {"writeUInt8", pyWriteUInt8, METH_VARARGS, "Write unsigned 8-bit integer."},
    {"readUInt16", pyReadUInt16, METH_VARARGS, "Read unsigned 16-bit integer."},
    {"writeUInt16", pyWriteUInt16, METH_VARARGS, "Write unsigned 16-bit integer."},
    {"readUInt32", pyReadUInt32, METH_VARARGS, "Read unsigned 32-bit integer."},
    {"writeUInt32", pyWriteUInt32, METH_VARARGS, "Write unsigned 32-bit integer."},
    {"readUInt64", pyReadUInt64, METH_VARARGS, "Read unsigned 64-bit integer."},
    {"writeUInt64", pyWriteUInt64, METH_VARARGS, "Write unsigned 64-bit integer."},
    {"readFloat32", pyReadFloat32, METH_VARARGS, "Read 32-bit float."},
    {"writeFloat32", pyWriteFloat32, METH_VARARGS, "Write 32-bit float."},
    {"readFloat64", pyReadFloat64, METH_VARARGS, "Read 64-bit float."},
    {"writeFloat64", pyWriteFloat64, METH_VARARGS, "Write 64-bit float."},
    {"memoryReadEndian", pyMemoryReadEndian, METH_VARARGS, "Read a value with explicit byte order."},
    {"memoryWriteEndian", pyMemoryWriteEndian, METH_VARARGS, "Write a value with explicit byte order."},
    {"memoryTypeSize", pyMemoryTypeSize, METH_VARARGS, "Return a typed memory size."},
    {"memoryTypeAlignment", pyMemoryTypeAlignment, METH_VARARGS, "Return a typed memory alignment."},
    {"memoryBlockAllocate", pyMemoryBlockAllocate, METH_VARARGS, "Allocate a typed block."},
    {"memoryBlockView", pyMemoryBlockView, METH_VARARGS, "Create a typed view."},
    {"memoryBlockLength", pyMemoryBlockLength, METH_VARARGS, "Return typed block length."},
    {"memoryBlockGet", pyMemoryBlockGet, METH_VARARGS, "Read typed block element."},
    {"memoryBlockSet", pyMemoryBlockSet, METH_VARARGS, "Write typed block element."},
    {"memoryStructSize", pyMemoryStructSize, METH_VARARGS, "Return native struct size."},
    {"memoryStructAlignment", pyMemoryStructAlignment, METH_VARARGS, "Return native struct alignment."},
    {"memoryStructFieldCount", pyMemoryStructFieldCount, METH_VARARGS, "Return native struct field count."},
    {"memoryStructFieldType", pyMemoryStructFieldType, METH_VARARGS, "Return native struct field type."},
    {"memoryStructAllocate", pyMemoryStructAllocate, METH_VARARGS, "Allocate native struct."},
    {"memoryStructFieldOffset", [](PyObject *, PyObject *args) {
        return pyMemoryStructField(nullptr, args, false);
    }, METH_VARARGS, "Return native struct field offset."},
    {"memoryStructFieldSize", [](PyObject *, PyObject *args) {
        return pyMemoryStructField(nullptr, args, true);
    }, METH_VARARGS, "Return native struct field size."},
    {"memoryStructGet", pyMemoryStructGet, METH_VARARGS, "Read native struct field."},
    {"memoryStructSet", pyMemoryStructSet, METH_VARARGS, "Write native struct field."},
    {"sizeof", pySizeOf, METH_VARARGS, "Return sizeof for a C type."},
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
    return PyModule_Create(&module);
}