#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <limits>
#include <type_traits>

namespace {

bool pointerFromPy(PyObject *obj, void **out) {
    unsigned long long value = PyLong_AsUnsignedLongLong(obj);
    if (PyErr_Occurred()) {
        return false;
    }
    *out = reinterpret_cast<void *>(static_cast<uintptr_t>(value));
    return true;
}

PyObject *pyMalloc(PyObject *, PyObject *args) {
    unsigned long long size;
    if (!PyArg_ParseTuple(args, "K", &size)) {
        return nullptr;
    }

    if (size > static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
        PyErr_SetString(PyExc_OverflowError, "allocation size is too large");
        return nullptr;
    }

    void *ptr = std::malloc(static_cast<size_t>(size));
    if (ptr == nullptr && size != 0) {
        return PyErr_NoMemory();
    }
    return PyLong_FromUnsignedLongLong(
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(ptr))
    );
}

PyObject *pyCalloc(PyObject *, PyObject *args) {
    unsigned long long count;
    unsigned long long size;
    if (!PyArg_ParseTuple(args, "KK", &count, &size)) {
        return nullptr;
    }

    if (count != 0 && size > std::numeric_limits<size_t>::max() / count) {
        PyErr_SetString(PyExc_OverflowError, "allocation size is too large");
        return nullptr;
    }

    void *ptr = std::calloc(static_cast<size_t>(count), static_cast<size_t>(size));
    if (ptr == nullptr && count != 0 && size != 0) {
        return PyErr_NoMemory();
    }
    return PyLong_FromUnsignedLongLong(
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(ptr))
    );
}

PyObject *pyRealloc(PyObject *, PyObject *args) {
    PyObject *ptrObject;
    unsigned long long size;
    if (!PyArg_ParseTuple(args, "OK", &ptrObject, &size)) {
        return nullptr;
    }

    void *ptr;
    if (!pointerFromPy(ptrObject, &ptr)) {
        return nullptr;
    }

    if (size > static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
        PyErr_SetString(PyExc_OverflowError, "allocation size is too large");
        return nullptr;
    }

    void *newPtr = std::realloc(ptr, static_cast<size_t>(size));
    if (newPtr == nullptr && size != 0) {
        return PyErr_NoMemory();
    }
    return PyLong_FromUnsignedLongLong(
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(newPtr))
    );
}

PyObject *pyFree(PyObject *, PyObject *args) {
    PyObject *ptrObject;
    if (!PyArg_ParseTuple(args, "O", &ptrObject)) {
        return nullptr;
    }

    void *ptr;
    if (!pointerFromPy(ptrObject, &ptr)) {
        return nullptr;
    }
    std::free(ptr);
    Py_RETURN_NONE;
}

PyObject *pyMemset(PyObject *, PyObject *args) {
    PyObject *ptrObject;
    int value;
    unsigned long long size;
    if (!PyArg_ParseTuple(args, "OiK", &ptrObject, &value, &size)) {
        return nullptr;
    }

    void *ptr;
    if (!pointerFromPy(ptrObject, &ptr)) {
        return nullptr;
    }
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

    void *destination;
    void *source;
    if (!pointerFromPy(destinationObject, &destination) ||
        !pointerFromPy(sourceObject, &source)) {
        return nullptr;
    }
    std::memcpy(destination, source, static_cast<size_t>(size));
    Py_RETURN_NONE;
}

PyObject *pyReadByte(PyObject *, PyObject *args) {
    PyObject *ptrObject;
    unsigned long long offset;
    if (!PyArg_ParseTuple(args, "OK", &ptrObject, &offset)) {
        return nullptr;
    }

    void *ptr;
    if (!pointerFromPy(ptrObject, &ptr)) {
        return nullptr;
    }
    auto *bytes = static_cast<unsigned char *>(ptr);
    return PyLong_FromUnsignedLong(static_cast<unsigned long>(bytes[offset]));
}

PyObject *pyWriteByte(PyObject *, PyObject *args) {
    PyObject *ptrObject;
    unsigned long long offset;
    unsigned int value;
    if (!PyArg_ParseTuple(args, "OKI", &ptrObject, &offset, &value)) {
        return nullptr;
    }
    if (value > 255) {
        PyErr_SetString(PyExc_ValueError, "byte value must be between 0 and 255");
        return nullptr;
    }

    void *ptr;
    if (!pointerFromPy(ptrObject, &ptr)) {
        return nullptr;
    }
    auto *bytes = static_cast<unsigned char *>(ptr);
    bytes[offset] = static_cast<unsigned char>(value);
    Py_RETURN_NONE;
}

template <typename T>
PyObject *readFixed(PyObject *args) {
    PyObject *ptrObject;
    unsigned long long offset;
    if (!PyArg_ParseTuple(args, "OK", &ptrObject, &offset)) {
        return nullptr;
    }

    void *ptr;
    if (!pointerFromPy(ptrObject, &ptr)) {
        return nullptr;
    }

    T value;
    // memcpy avoids undefined behaviour from unaligned addresses.
    std::memcpy(&value, static_cast<unsigned char *>(ptr) + offset, sizeof(T));
    if constexpr (std::is_signed_v<T>) {
        return PyLong_FromLongLong(static_cast<long long>(value));
    } else {
        return PyLong_FromUnsignedLongLong(
            static_cast<unsigned long long>(value)
        );
    }
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
    if (!pointerFromPy(ptrObject, &ptr)) {
        return nullptr;
    }

    T value;
    if constexpr (std::is_signed_v<T>) {
        long long raw = PyLong_AsLongLong(valueObject);
        if (PyErr_Occurred()) {
            return nullptr;
        }
        if (raw < static_cast<long long>(std::numeric_limits<T>::min()) ||
            raw > static_cast<long long>(std::numeric_limits<T>::max())) {
            PyErr_SetString(PyExc_OverflowError, "signed integer value is out of range");
            return nullptr;
        }
        value = static_cast<T>(raw);
    } else {
        unsigned long long raw = PyLong_AsUnsignedLongLong(valueObject);
        if (PyErr_Occurred()) {
            return nullptr;
        }
        if (raw > static_cast<unsigned long long>(std::numeric_limits<T>::max())) {
            PyErr_SetString(PyExc_OverflowError, "unsigned integer value is out of range");
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
    if (!PyArg_ParseTuple(args, "OK", &ptrObject, &offset)) {
        return nullptr;
    }

    void *ptr;
    if (!pointerFromPy(ptrObject, &ptr)) {
        return nullptr;
    }

    T value;
    std::memcpy(&value, static_cast<unsigned char *>(ptr) + offset, sizeof(T));
    return PyFloat_FromDouble(static_cast<double>(value));
}

template <typename T>
PyObject *writeFloat(PyObject *args) {
    PyObject *ptrObject;
    unsigned long long offset;
    double rawValue;
    if (!PyArg_ParseTuple(args, "OKd", &ptrObject, &offset, &rawValue)) {
        return nullptr;
    }

    T value = static_cast<T>(rawValue);
    if (!std::isfinite(rawValue) || !std::isfinite(static_cast<double>(value))) {
        PyErr_SetString(PyExc_OverflowError, "floating-point value is out of range");
        return nullptr;
    }

    void *ptr;
    if (!pointerFromPy(ptrObject, &ptr)) {
        return nullptr;
    }
    std::memcpy(static_cast<unsigned char *>(ptr) + offset, &value, sizeof(T));
    Py_RETURN_NONE;
}

PyObject *pyReadFloat32(PyObject *, PyObject *args) {
    return readFloat<float>(args);
}

PyObject *pyWriteFloat32(PyObject *, PyObject *args) {
    return writeFloat<float>(args);
}

PyObject *pyReadFloat64(PyObject *, PyObject *args) {
    return readFloat<double>(args);
}

PyObject *pyWriteFloat64(PyObject *, PyObject *args) {
    return writeFloat<double>(args);
}

#define FIXED_INTEGER_FUNCTIONS(NAME, TYPE) \
    PyObject *pyRead##NAME(PyObject *, PyObject *args) { \
        return readFixed<TYPE>(args); \
    } \
    PyObject *pyWrite##NAME(PyObject *, PyObject *args) { \
        return writeFixed<TYPE>(args); \
    }

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
    if (!PyArg_ParseTuple(args, "s", &typeName)) {
        return nullptr;
    }

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
    else if (std::strcmp(typeName, "uint32") == 0 ||
             std::strcmp(typeName, "unit32") == 0) size = sizeof(std::uint32_t);
    else if (std::strcmp(typeName, "uint64") == 0) size = sizeof(std::uint64_t);
    else {
        PyErr_Format(PyExc_ValueError, "unknown C type '%s'", typeName);
        return nullptr;
    }

    return PyLong_FromSize_t(size);
}

PyMethodDef methods[] = {
    {"malloc", pyMalloc, METH_VARARGS, "Allocate raw memory and return its address."},
    {"calloc", pyCalloc, METH_VARARGS, "Allocate zero-initialized raw memory."},
    {"realloc", pyRealloc, METH_VARARGS, "Resize a raw memory allocation."},
    {"free", pyFree, METH_VARARGS, "Free a raw memory allocation."},
    {"memset", pyMemset, METH_VARARGS, "Fill raw memory with a byte value."},
    {"memcpy", pyMemcpy, METH_VARARGS, "Copy bytes between raw memory regions."},
    {"readByte", pyReadByte, METH_VARARGS, "Read one byte from a raw address."},
    {"writeByte", pyWriteByte, METH_VARARGS, "Write one byte to a raw address."},
    {"readInt8", pyReadInt8, METH_VARARGS, "Read a signed 8-bit integer."},
    {"writeInt8", pyWriteInt8, METH_VARARGS, "Write a signed 8-bit integer."},
    {"readInt16", pyReadInt16, METH_VARARGS, "Read a signed 16-bit integer."},
    {"writeInt16", pyWriteInt16, METH_VARARGS, "Write a signed 16-bit integer."},
    {"readInt32", pyReadInt32, METH_VARARGS, "Read a signed 32-bit integer."},
    {"writeInt32", pyWriteInt32, METH_VARARGS, "Write a signed 32-bit integer."},
    {"readInt64", pyReadInt64, METH_VARARGS, "Read a signed 64-bit integer."},
    {"writeInt64", pyWriteInt64, METH_VARARGS, "Write a signed 64-bit integer."},
    {"readUInt8", pyReadUInt8, METH_VARARGS, "Read an unsigned 8-bit integer."},
    {"writeUInt8", pyWriteUInt8, METH_VARARGS, "Write an unsigned 8-bit integer."},
    {"readUInt16", pyReadUInt16, METH_VARARGS, "Read an unsigned 16-bit integer."},
    {"writeUInt16", pyWriteUInt16, METH_VARARGS, "Write an unsigned 16-bit integer."},
    {"readUInt32", pyReadUInt32, METH_VARARGS, "Read an unsigned 32-bit integer."},
    {"writeUInt32", pyWriteUInt32, METH_VARARGS, "Write an unsigned 32-bit integer."},
    {"readUInt64", pyReadUInt64, METH_VARARGS, "Read an unsigned 64-bit integer."},
    {"writeUInt64", pyWriteUInt64, METH_VARARGS, "Write an unsigned 64-bit integer."},
    {"readFloat32", pyReadFloat32, METH_VARARGS, "Read a 32-bit float."},
    {"writeFloat32", pyWriteFloat32, METH_VARARGS, "Write a 32-bit float."},
    {"readFloat64", pyReadFloat64, METH_VARARGS, "Read a 64-bit float."},
    {"writeFloat64", pyWriteFloat64, METH_VARARGS, "Write a 64-bit float."},
    {"sizeof", pySizeOf, METH_VARARGS, "Return sizeof() for a supported C type."},
    {nullptr, nullptr, 0, nullptr}
};

PyModuleDef module = {
    PyModuleDef_HEAD_INIT,
    "c",
    "Experimental low-level C memory primitives for Lynxer.",
    -1,
    methods,
};

} // namespace

PyMODINIT_FUNC PyInit_c() {
    return PyModule_Create(&module);
}
