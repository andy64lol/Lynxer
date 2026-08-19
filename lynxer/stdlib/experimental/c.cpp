#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

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
