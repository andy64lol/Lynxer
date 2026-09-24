#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <cstdint>
#include <cstring>
#include <new>
#include <utility>
#include <vector>

/*
 * Native executor for Lynxer's postfix instruction stream.
 *
 * The instruction format is intentionally duplicated here instead of
 * importing implementation details from Python.  These values are part of
 * the .lynxc v9 format and must stay in lockstep with bytecode.py.
 */

namespace {

constexpr unsigned char OP_NONE            = 0x20;
constexpr unsigned char OP_FALSE           = 0x21;
constexpr unsigned char OP_TRUE            = 0x22;
constexpr unsigned char OP_INT             = 0x23;
constexpr unsigned char OP_FLOAT           = 0x24;
constexpr unsigned char OP_COMPLEX         = 0x25;
constexpr unsigned char OP_STR             = 0x26;
constexpr unsigned char OP_BYTES           = 0x27;
constexpr unsigned char OP_BUILD_LIST      = 0x28;
constexpr unsigned char OP_BUILD_TUPLE     = 0x29;
constexpr unsigned char OP_BUILD_DICT      = 0x2A;
constexpr unsigned char OP_BUILD_SET       = 0x2B;
constexpr unsigned char OP_BUILD_FROZENSET = 0x2C;
constexpr unsigned char OP_BUILD_POSITION  = 0x2D;
constexpr unsigned char OP_BUILD_NODE      = 0x2E;

constexpr unsigned int MAX_VARINT_BITS = 1U << 16;
constexpr Py_ssize_t MAX_INSTRUCTIONS  = 16 * 1024 * 1024;
constexpr size_t MAX_OBJECT_ATTRS      = 4096;

/*
 * Unique-ownership handle over a Python reference: releases the reference on
 * destruction so dense error paths cannot leak.  release() hands ownership
 * back to raw C API calls that steal their arguments (SET_ITEM, returns).
 */
class PyObjectPtr {
public:
    PyObjectPtr() = default;

    /* Adopts a reference the caller already owns (a "stolen" new ref). */
    explicit PyObjectPtr(PyObject *object) : object_(object) {}

    static PyObjectPtr borrowed(PyObject *object) {
        Py_XINCREF(object);
        return PyObjectPtr(object);
    }

    PyObjectPtr(PyObjectPtr &&other) noexcept : object_(other.object_) {
        other.object_ = nullptr;
    }

    PyObjectPtr &operator=(PyObjectPtr &&other) noexcept {
        if (this != &other) {
            Py_XDECREF(object_);
            object_ = other.object_;
            other.object_ = nullptr;
        }
        return *this;
    }

    PyObjectPtr(const PyObjectPtr &) = delete;
    PyObjectPtr &operator=(const PyObjectPtr &) = delete;

    ~PyObjectPtr() { Py_XDECREF(object_); }

    PyObject *get() const { return object_; }
    explicit operator bool() const { return object_ != nullptr; }

    PyObject *release() {
        PyObject *object = object_;
        object_ = nullptr;
        return object;
    }

    void reset(PyObject *object = nullptr) {
        Py_XDECREF(object_);
        object_ = object;
    }

private:
    PyObject *object_ = nullptr;
};

void vm_error(const char *message) {
    PyErr_SetString(PyExc_ValueError, message);
}

bool vm_truncated() {
    vm_error("truncated bytecode instruction stream");
    return false;
}

/*
 * Decode a zigzag-encoded integer.  Fast path for values that fit in an
 * unsigned long long; arbitrary-precision values fall back to Python
 * integer arithmetic.
 */
PyObjectPtr zigzag_integer(PyObject *encoded) {
    unsigned long long fast_value = PyLong_AsUnsignedLongLong(encoded);
    if (!(fast_value == static_cast<unsigned long long>(-1) && PyErr_Occurred())) {
        if ((fast_value & 1U) == 0) {
            return PyObjectPtr(PyLong_FromUnsignedLongLong(fast_value >> 1));
        }
        return PyObjectPtr(
            PyLong_FromLongLong(-static_cast<long long>(fast_value >> 1) - 1LL));
    }
    PyErr_Clear();

    PyObjectPtr one(PyLong_FromLong(1));
    if (!one) {
        return {};
    }
    PyObjectPtr shifted(PyNumber_Rshift(encoded, one.get()));
    if (!shifted) {
        return {};
    }
    PyObjectPtr odd(PyNumber_And(encoded, one.get()));
    if (!odd) {
        return {};
    }
    int is_odd = PyObject_IsTrue(odd.get());
    if (is_odd < 0) {
        return {};
    }
    if (!is_odd) {
        return shifted;
    }

    PyObjectPtr negative(PyNumber_Negative(shifted.get()));
    if (!negative) {
        return {};
    }
    return PyObjectPtr(PyNumber_Subtract(negative.get(), one.get()));
}

PyObjectPtr new_node(PyObject *node_class) {
    if (!PyType_Check(node_class)) {
        PyErr_SetString(PyExc_ValueError, "bytecode AST class table contains a non-type");
        return {};
    }
    PyTypeObject *node_type = reinterpret_cast<PyTypeObject *>(node_class);
    if (node_type->tp_alloc == NULL) {
        PyErr_SetString(PyExc_ValueError, "bytecode AST class cannot be allocated");
        return {};
    }
    return PyObjectPtr(node_type->tp_alloc(node_type, 0));
}

class BytecodeVM {
public:
    BytecodeVM(
        const unsigned char *code,
        Py_ssize_t length,
        PyObject *classes,
        Py_ssize_t class_count,
        PyObject *position_type)
        : code_(code),
          length_(length),
          classes_(classes),
          class_count_(class_count),
          position_type_(position_type) {}

    PyObjectPtr run() {
        Py_ssize_t instruction_count = 0;

        while (position_ < length_) {
            instruction_count++;
            if (instruction_count > MAX_INSTRUCTIONS) {
                vm_error("bytecode instruction stream contains too many instructions");
                return {};
            }
            unsigned char opcode;
            if (!read_byte(opcode)) {
                return {};
            }

            PyObjectPtr value;
            switch (opcode) {
            case OP_NONE:
                value = PyObjectPtr::borrowed(Py_None);
                break;
            case OP_FALSE:
                value = PyObjectPtr::borrowed(Py_False);
                break;
            case OP_TRUE:
                value = PyObjectPtr::borrowed(Py_True);
                break;
            case OP_INT: {
                PyObjectPtr encoded = read_varint();
                if (!encoded) {
                    return {};
                }
                value = zigzag_integer(encoded.get());
                break;
            }
            case OP_FLOAT: {
                double number;
                if (!read_double(&number)) {
                    return {};
                }
                value.reset(PyFloat_FromDouble(number));
                break;
            }
            case OP_COMPLEX: {
                double real;
                double imaginary;
                if (!read_double(&real) || !read_double(&imaginary)) {
                    return {};
                }
                value.reset(PyComplex_FromDoubles(real, imaginary));
                break;
            }
            case OP_STR:
                value = read_instruction_string();
                break;
            case OP_BYTES:
                value = read_instruction_bytes();
                break;
            case OP_BUILD_LIST:
            case OP_BUILD_TUPLE:
            case OP_BUILD_DICT:
            case OP_BUILD_SET:
            case OP_BUILD_FROZENSET:
                value = build_container(opcode);
                break;
            case OP_BUILD_POSITION:
                value = build_position();
                break;
            case OP_BUILD_NODE:
                value = build_node();
                break;
            default:
                vm_error("unknown bytecode instruction");
                return {};
            }

            if (!value || !push(std::move(value))) {
                return {};
            }
        }

        if (static_cast<Py_ssize_t>(stack_.size()) != 1) {
            vm_error("bytecode instruction stream did not produce one program");
            return {};
        }
        return PyObjectPtr::borrowed(stack_[0].get());
    }

private:
    bool read_byte(unsigned char &result) {
        if (position_ >= length_) {
            return vm_truncated();
        }
        result = code_[position_++];
        return true;
    }

    bool read_bytes(Py_ssize_t count, const unsigned char **result) {
        if (count < 0 || count > length_ - position_) {
            return vm_truncated();
        }
        *result = code_ + position_;
        position_ += count;
        return true;
    }

    /*
     * OR one varint group into an arbitrary-precision Python integer.  Used
     * once the native uint64 fast path overflows.
     */
    PyObjectPtr varint_or_part(PyObject *value, unsigned int part, unsigned int shift) {
        PyObjectPtr part_object(PyLong_FromUnsignedLong(part));
        if (!part_object) {
            return {};
        }
        if (shift == 0) {
            return PyObjectPtr(PyNumber_Or(value, part_object.get()));
        }
        PyObjectPtr shift_object(PyLong_FromUnsignedLong(shift));
        if (!shift_object) {
            return {};
        }
        PyObjectPtr shifted(PyNumber_Lshift(part_object.get(), shift_object.get()));
        if (!shifted) {
            return {};
        }
        return PyObjectPtr(PyNumber_Or(value, shifted.get()));
    }

    /*
     * Read a varint with a native fast path.  The common case (all bytecode
     * lengths, class IDs, positions, and ordinary integer literals) stays in
     * a uint64_t and creates only one Python integer.  Arbitrary-precision
     * literals transparently fall back to Python integers after the first
     * overflowing group.
     */
    PyObjectPtr read_varint() {
        uint64_t fast_value = 0;
        unsigned int shift = 0;

        for (;;) {
            unsigned char byte;
            if (!read_byte(byte)) {
                return {};
            }

            if (shift < 64 && !(shift == 63 && (byte & 0x7fU) > 1)) {
                fast_value |= static_cast<uint64_t>(byte & 0x7fU) << shift;
                if ((byte & 0x80U) == 0) {
                    return PyObjectPtr(PyLong_FromUnsignedLongLong(fast_value));
                }
            } else {
                PyObjectPtr value(PyLong_FromUnsignedLongLong(fast_value));
                if (!value) {
                    return {};
                }
                PyObjectPtr next = varint_or_part(value.get(), byte & 0x7fU, shift);
                if (!next) {
                    return {};
                }
                if ((byte & 0x80U) == 0) {
                    return next;
                }
                value = std::move(next);
                shift += 7;
                for (;;) {
                    if (!read_byte(byte)) {
                        return {};
                    }
                    PyObjectPtr combined = varint_or_part(value.get(), byte & 0x7fU, shift);
                    if (!combined) {
                        return {};
                    }
                    value = std::move(combined);
                    if ((byte & 0x80U) == 0) {
                        return value;
                    }
                    if (shift > MAX_VARINT_BITS) {
                        vm_error("integer in bytecode instruction stream is too large");
                        return {};
                    }
                    shift += 7;
                }
            }
            if (shift > MAX_VARINT_BITS) {
                vm_error("integer in bytecode instruction stream is too large");
                return {};
            }
            shift += 7;
        }
    }

    bool varint_to_size(Py_ssize_t *result, const char *message) {
        PyObjectPtr value = read_varint();
        if (!value) {
            return false;
        }
        Py_ssize_t converted = PyLong_AsSsize_t(value.get());
        if (converted == -1 && PyErr_Occurred()) {
            PyErr_Clear();
            vm_error(message);
            return false;
        }
        if (converted < 0) {
            vm_error(message);
            return false;
        }
        *result = converted;
        return true;
    }

    bool read_double(double *result) {
        const unsigned char *raw;
        if (!read_bytes(8, &raw)) {
            return false;
        }
        uint64_t bits = 0;
        for (unsigned int index = 0; index < 8; index++) {
            bits |= static_cast<uint64_t>(raw[index]) << (index * 8U);
        }
        memcpy(result, &bits, sizeof(*result));
        return true;
    }

    PyObjectPtr read_instruction_string() {
        Py_ssize_t length;
        if (!varint_to_size(
                &length, "string in bytecode instruction stream is too large")) {
            return {};
        }
        const unsigned char *raw;
        if (!read_bytes(length, &raw)) {
            return {};
        }
        return PyObjectPtr(
            PyUnicode_DecodeUTF8(reinterpret_cast<const char *>(raw), length, "strict"));
    }

    PyObjectPtr read_instruction_bytes() {
        Py_ssize_t length;
        if (!varint_to_size(
                &length, "bytes value in bytecode instruction stream is too large")) {
            return {};
        }
        const unsigned char *raw;
        if (!read_bytes(length, &raw)) {
            return {};
        }
        return PyObjectPtr(PyBytes_FromStringAndSize(reinterpret_cast<const char *>(raw), length));
    }

    bool stack_has(Py_ssize_t count) const {
        if (count < 0 || count > static_cast<Py_ssize_t>(stack_.size())) {
            vm_error("bytecode instruction stack underflow");
            return false;
        }
        return true;
    }

    bool push(PyObjectPtr value) {
        try {
            stack_.push_back(std::move(value));
        } catch (const std::bad_alloc &) {
            PyErr_NoMemory();
            return false;
        }
        return true;
    }

    void discard_from(Py_ssize_t start) {
        stack_.resize(static_cast<size_t>(start));
    }

    PyObjectPtr build_position() {
        if (!stack_has(4)) {
            return {};
        }
        Py_ssize_t start = static_cast<Py_ssize_t>(stack_.size()) - 4;
        PyObject *index = stack_[start].get();
        PyObject *line = stack_[start + 1].get();
        PyObject *column = stack_[start + 2].get();
        PyObject *filename = stack_[start + 3].get();
        if (!PyLong_Check(index) || !PyLong_Check(line) || !PyLong_Check(column) ||
            !PyUnicode_Check(filename)) {
            vm_error("bytecode built an invalid source position");
            return {};
        }
        PyObjectPtr empty(PyUnicode_FromString(""));
        if (!empty) {
            return {};
        }
        PyObjectPtr position(PyObject_CallFunctionObjArgs(
            position_type_, index, line, column, filename, empty.get(), NULL));
        if (!position) {
            return {};
        }
        discard_from(start);
        return position;
    }

    PyObjectPtr build_node() {
        PyObjectPtr class_id = read_varint();
        if (!class_id) {
            return {};
        }
        Py_ssize_t class_index = PyLong_AsSsize_t(class_id.get());
        if (class_index == -1 && PyErr_Occurred()) {
            PyErr_Clear();
            vm_error("unknown AST node type id in bytecode instruction stream");
            return {};
        }
        if (class_index < 0 || class_index >= class_count_) {
            vm_error("unknown AST node type id in bytecode instruction stream");
            return {};
        }

        PyObjectPtr attr_count_object = read_varint();
        if (!attr_count_object) {
            return {};
        }
        Py_ssize_t attr_count = PyLong_AsSsize_t(attr_count_object.get());
        if (attr_count == -1 && PyErr_Occurred()) {
            PyErr_Clear();
            vm_error("AST node declares too many attributes");
            return {};
        }
        if (attr_count < 0 || static_cast<size_t>(attr_count) > MAX_OBJECT_ATTRS) {
            vm_error("AST node declares too many attributes");
            return {};
        }
        if (!stack_has(attr_count * 2)) {
            return {};
        }
        Py_ssize_t start = static_cast<Py_ssize_t>(stack_.size()) - attr_count * 2;

        PyObjectPtr node_class(PySequence_GetItem(classes_, class_index));
        if (!node_class) {
            return {};
        }
        PyObjectPtr node = new_node(node_class.get());
        if (!node) {
            return {};
        }
        PyObjectPtr node_dict(PyObject_GetAttrString(node.get(), "__dict__"));
        if (!node_dict || !PyDict_Check(node_dict.get())) {
            vm_error("bytecode AST node does not expose a dictionary");
            return {};
        }

        for (Py_ssize_t index = 0; index < attr_count * 2; index += 2) {
            PyObject *name = stack_[start + index].get();
            PyObject *value = stack_[start + index + 1].get();
            if (!PyUnicode_Check(name)) {
                vm_error("malformed AST node attribute name");
                return {};
            }
            if (PyDict_SetItem(node_dict.get(), name, value) < 0) {
                return {};
            }
        }
        discard_from(start);
        return node;
    }

    PyObjectPtr build_container(unsigned char opcode) {
        Py_ssize_t count;
        if (!varint_to_size(
                &count, "instruction declares more values than the bytecode holds")) {
            return {};
        }
        Py_ssize_t item_count;
        if (opcode == OP_BUILD_DICT) {
            if (count > PY_SSIZE_T_MAX / 2) {
                vm_error("instruction declares too many dictionary entries");
                return {};
            }
            item_count = count * 2;
        } else {
            item_count = count;
        }
        if (count > length_ || !stack_has(item_count)) {
            return {};
        }
        Py_ssize_t start = static_cast<Py_ssize_t>(stack_.size()) - item_count;

        PyObjectPtr result;
        switch (opcode) {
        case OP_BUILD_LIST:
            result.reset(PyList_New(count));
            if (result) {
                for (Py_ssize_t index = 0; index < count; index++) {
                    PyList_SET_ITEM(
                        result.get(), index,
                        stack_[static_cast<size_t>(start + index)].release());
                }
            }
            break;
        case OP_BUILD_TUPLE:
            result.reset(PyTuple_New(count));
            if (result) {
                for (Py_ssize_t index = 0; index < count; index++) {
                    PyTuple_SET_ITEM(
                        result.get(), index,
                        stack_[static_cast<size_t>(start + index)].release());
                }
            }
            break;
        case OP_BUILD_SET:
            result.reset(PySet_New(NULL));
            if (result) {
                for (Py_ssize_t index = 0; index < count; index++) {
                    if (PySet_Add(result.get(), stack_[static_cast<size_t>(start + index)].get()) < 0) {
                        result.reset();
                        break;
                    }
                }
            }
            break;
        case OP_BUILD_FROZENSET:
            result.reset(PyFrozenSet_New(NULL));
            if (result) {
                for (Py_ssize_t index = 0; index < count; index++) {
                    if (PySet_Add(result.get(), stack_[static_cast<size_t>(start + index)].get()) < 0) {
                        result.reset();
                        break;
                    }
                }
            }
            break;
        case OP_BUILD_DICT:
            result.reset(PyDict_New());
            if (result) {
                for (Py_ssize_t index = 0; index < count; index++) {
                    PyObject *key = stack_[static_cast<size_t>(start + index * 2)].get();
                    PyObject *value = stack_[static_cast<size_t>(start + index * 2 + 1)].get();
                    if (PyDict_SetItem(result.get(), key, value) < 0) {
                        result.reset();
                        break;
                    }
                }
            }
            break;
        default:
            vm_error("unknown container opcode");
            break;
        }
        if (!result) {
            return {};
        }
        discard_from(start);
        return result;
    }

    const unsigned char *code_;
    Py_ssize_t length_;
    Py_ssize_t position_ = 0;
    std::vector<PyObjectPtr> stack_;
    PyObject *classes_;
    Py_ssize_t class_count_;
    PyObject *position_type_;
};

PyObject *py_decode(PyObject *module, PyObject *args) {
    PyObject *code_object;
    PyObject *classes;
    PyObject *position_type;

    (void)module;
    if (!PyArg_ParseTuple(
            args, "O!OO:decode", &PyBytes_Type, &code_object, &classes,
            &position_type)) {
        return NULL;
    }
    if (!PySequence_Check(classes)) {
        PyErr_SetString(PyExc_TypeError, "bytecode VM class table must be a sequence");
        return NULL;
    }
    if (!PyCallable_Check(position_type)) {
        PyErr_SetString(PyExc_TypeError, "bytecode VM position type must be callable");
        return NULL;
    }

    Py_ssize_t class_count = PySequence_Size(classes);
    if (class_count < 0) {
        return NULL;
    }

    try {
        BytecodeVM vm(
            reinterpret_cast<const unsigned char *>(PyBytes_AS_STRING(code_object)),
            PyBytes_GET_SIZE(code_object),
            classes,
            class_count,
            position_type);
        return vm.run().release();
    } catch (const std::bad_alloc &) {
        return PyErr_NoMemory();
    }
}

PyMethodDef module_methods[] = {
    {
        "decode",
        py_decode,
        METH_VARARGS,
        "Execute a Lynxer postfix bytecode stream and return its program node.",
    },
    {NULL, NULL, 0, NULL},
};

PyModuleDef module_definition = {
    PyModuleDef_HEAD_INIT,
    "bytecode_vm",
    "Native Lynxer bytecode stack-machine executor.",
    -1,
    module_methods,
    NULL,
    NULL,
    NULL,
    NULL,
};

}  // namespace

PyMODINIT_FUNC
PyInit_bytecode_vm(void)
{
    return PyModule_Create(&module_definition);
}
