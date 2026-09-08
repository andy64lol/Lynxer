#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stdint.h>
#include <string.h>

/*
 * Native executor for Lynxer's postfix instruction stream.
 *
 * The instruction format is intentionally duplicated here instead of
 * importing implementation details from Python.  These values are part of
 * the .lynxc v9 format and must stay in lockstep with bytecode.py.
 */
#define OP_NONE            0x20
#define OP_FALSE           0x21
#define OP_TRUE            0x22
#define OP_INT             0x23
#define OP_FLOAT           0x24
#define OP_COMPLEX         0x25
#define OP_STR             0x26
#define OP_BYTES           0x27
#define OP_BUILD_LIST      0x28
#define OP_BUILD_TUPLE     0x29
#define OP_BUILD_DICT      0x2A
#define OP_BUILD_SET       0x2B
#define OP_BUILD_FROZENSET 0x2C
#define OP_BUILD_POSITION  0x2D
#define OP_BUILD_NODE      0x2E

#define MAX_VARINT_BITS    (1U << 16)
#define MAX_INSTRUCTIONS   (16U * 1024U * 1024U)
#define MAX_OBJECT_ATTRS   4096U

typedef struct {
    const unsigned char *code;
    Py_ssize_t length;
    Py_ssize_t position;
    PyObject **stack;
    Py_ssize_t stack_size;
    Py_ssize_t stack_capacity;
    PyObject *classes;
    Py_ssize_t class_count;
    PyObject *position_type;
} BytecodeVM;

static int
vm_error(const char *message)
{
    PyErr_SetString(PyExc_ValueError, message);
    return 0;
}

static int
vm_truncated(void)
{
    return vm_error("truncated bytecode instruction stream");
}

static int
read_byte(BytecodeVM *vm, unsigned char *result)
{
    if (vm->position >= vm->length) {
        return vm_truncated();
    }
    *result = vm->code[vm->position++];
    return 1;
}

static int
read_bytes(BytecodeVM *vm, Py_ssize_t count, const unsigned char **result)
{
    if (count < 0 || count > vm->length - vm->position) {
        return vm_truncated();
    }
    *result = vm->code + vm->position;
    vm->position += count;
    return 1;
}

static PyObject *
varint_or_part(PyObject *value, unsigned int part, unsigned int shift)
{
    PyObject *part_object;
    PyObject *shift_object = NULL;
    PyObject *shifted;
    PyObject *result;

    part_object = PyLong_FromUnsignedLong((unsigned long)part);
    if (part_object == NULL) {
        return NULL;
    }
    if (shift == 0) {
        result = PyNumber_Or(value, part_object);
        Py_DECREF(part_object);
        return result;
    }
    shift_object = PyLong_FromUnsignedLong((unsigned long)shift);
    if (shift_object == NULL) {
        Py_DECREF(part_object);
        return NULL;
    }
    shifted = PyNumber_Lshift(part_object, shift_object);
    Py_DECREF(part_object);
    Py_DECREF(shift_object);
    if (shifted == NULL) {
        return NULL;
    }
    result = PyNumber_Or(value, shifted);
    Py_DECREF(shifted);
    return result;
}

/*
 * Read a varint with a native fast path.  The common case (all bytecode
 * lengths, class IDs, positions, and ordinary integer literals) stays in a
 * uint64_t and creates only one Python integer.  Arbitrary-precision literals
 * transparently fall back to Python integers after the first overflowing
 * group.
 */
static PyObject *
read_varint(BytecodeVM *vm)
{
    uint64_t fast_value = 0;
    unsigned int shift = 0;
    unsigned char byte;

    for (;;) {
        if (!read_byte(vm, &byte)) {
            return NULL;
        }

        if (shift < 64 && !(shift == 63 && (byte & 0x7fU) > 1)) {
            fast_value |= ((uint64_t)(byte & 0x7fU)) << shift;
            if ((byte & 0x80U) == 0) {
                return PyLong_FromUnsignedLongLong(fast_value);
            }
        } else {
            PyObject *value;
            PyObject *next;

            value = PyLong_FromUnsignedLongLong(fast_value);
            if (value == NULL) {
                return NULL;
            }
            next = varint_or_part(value, byte & 0x7fU, shift);
            Py_DECREF(value);
            if (next == NULL) {
                return NULL;
            }
            value = next;
            if ((byte & 0x80U) == 0) {
                return value;
            }
            shift += 7;
            for (;;) {
                if (!read_byte(vm, &byte)) {
                    Py_DECREF(value);
                    return NULL;
                }
                next = varint_or_part(value, byte & 0x7fU, shift);
                Py_DECREF(value);
                if (next == NULL) {
                    return NULL;
                }
                value = next;
                if ((byte & 0x80U) == 0) {
                    return value;
                }
                if (shift > MAX_VARINT_BITS) {
                    Py_DECREF(value);
                    vm_error("integer in bytecode instruction stream is too large");
                    return NULL;
                }
                shift += 7;
            }
        }
        if (shift > MAX_VARINT_BITS) {
            vm_error("integer in bytecode instruction stream is too large");
            return NULL;
        }
        shift += 7;
    }
}

static int
varint_to_size(BytecodeVM *vm, Py_ssize_t *result, const char *message)
{
    PyObject *value = read_varint(vm);
    Py_ssize_t converted;

    if (value == NULL) {
        return 0;
    }
    converted = PyLong_AsSsize_t(value);
    Py_DECREF(value);
    if (converted == -1 && PyErr_Occurred()) {
        PyErr_Clear();
        return vm_error(message);
    }
    if (converted < 0) {
        return vm_error(message);
    }
    *result = converted;
    return 1;
}

static int
stack_reserve(BytecodeVM *vm, Py_ssize_t additional)
{
    Py_ssize_t required;
    Py_ssize_t capacity;
    PyObject **new_stack;

    if (additional < 0 || vm->stack_size > PY_SSIZE_T_MAX - additional) {
        PyErr_NoMemory();
        return 0;
    }
    required = vm->stack_size + additional;
    if (required <= vm->stack_capacity) {
        return 1;
    }
    capacity = vm->stack_capacity > 0 ? vm->stack_capacity : 64;
    while (capacity < required) {
        if (capacity > PY_SSIZE_T_MAX / 2) {
            capacity = required;
            break;
        }
        capacity *= 2;
    }
    new_stack = PyMem_Realloc(
        vm->stack, (size_t)capacity * sizeof(*new_stack));
    if (new_stack == NULL) {
        PyErr_NoMemory();
        return 0;
    }
    vm->stack = new_stack;
    vm->stack_capacity = capacity;
    return 1;
}

static int
push_owned(BytecodeVM *vm, PyObject *value)
{
    if (!stack_reserve(vm, 1)) {
        Py_DECREF(value);
        return 0;
    }
    vm->stack[vm->stack_size++] = value;
    return 1;
}

static int
stack_has(BytecodeVM *vm, Py_ssize_t count)
{
    if (count < 0 || count > vm->stack_size) {
        return vm_error("bytecode instruction stack underflow");
    }
    return 1;
}

static void
stack_discard_from(BytecodeVM *vm, Py_ssize_t start)
{
    Py_ssize_t index;

    for (index = start; index < vm->stack_size; index++) {
        Py_XDECREF(vm->stack[index]);
        vm->stack[index] = NULL;
    }
    vm->stack_size = start;
}

static void
stack_release(BytecodeVM *vm)
{
    if (vm->stack != NULL) {
        stack_discard_from(vm, 0);
        PyMem_Free(vm->stack);
        vm->stack = NULL;
    }
}

static int
read_double(BytecodeVM *vm, double *result)
{
    const unsigned char *raw;
    uint64_t bits = 0;
    unsigned int index;

    if (!read_bytes(vm, 8, &raw)) {
        return 0;
    }
    for (index = 0; index < 8; index++) {
        bits |= ((uint64_t)raw[index]) << (index * 8U);
    }
    memcpy(result, &bits, sizeof(*result));
    return 1;
}

static PyObject *
read_instruction_string(BytecodeVM *vm)
{
    Py_ssize_t length;
    const unsigned char *raw;

    if (!varint_to_size(
            vm, &length, "string in bytecode instruction stream is too large")) {
        return NULL;
    }
    if (!read_bytes(vm, length, &raw)) {
        return NULL;
    }
    return PyUnicode_DecodeUTF8((const char *)raw, length, "strict");
}

static PyObject *
read_instruction_bytes(BytecodeVM *vm)
{
    Py_ssize_t length;
    const unsigned char *raw;

    if (!varint_to_size(
            vm, &length, "bytes value in bytecode instruction stream is too large")) {
        return NULL;
    }
    if (!read_bytes(vm, length, &raw)) {
        return NULL;
    }
    return PyBytes_FromStringAndSize((const char *)raw, length);
}

static PyObject *
zigzag_integer(PyObject *encoded)
{
    unsigned long long fast_value;
    PyObject *one = NULL;
    PyObject *shifted = NULL;
    PyObject *odd = NULL;
    PyObject *result = NULL;
    int is_odd;

    fast_value = PyLong_AsUnsignedLongLong(encoded);
    if (!(fast_value == (unsigned long long)-1 && PyErr_Occurred())) {
        if ((fast_value & 1U) == 0) {
            return PyLong_FromUnsignedLongLong(fast_value >> 1);
        }
        return PyLong_FromLongLong(
            -(long long)(fast_value >> 1) - 1LL);
    }
    PyErr_Clear();

    one = PyLong_FromLong(1);
    if (one == NULL) {
        return NULL;
    }
    shifted = PyNumber_Rshift(encoded, one);
    if (shifted == NULL) {
        Py_DECREF(one);
        return NULL;
    }
    odd = PyNumber_And(encoded, one);
    Py_DECREF(one);
    if (odd == NULL) {
        Py_DECREF(shifted);
        return NULL;
    }
    is_odd = PyObject_IsTrue(odd);
    Py_DECREF(odd);
    if (is_odd < 0) {
        Py_DECREF(shifted);
        return NULL;
    }
    if (!is_odd) {
        return shifted;
    }

    result = PyNumber_Negative(shifted);
    Py_DECREF(shifted);
    if (result == NULL) {
        return NULL;
    }
    one = PyLong_FromLong(1);
    if (one == NULL) {
        Py_DECREF(result);
        return NULL;
    }
    shifted = PyNumber_Subtract(result, one);
    Py_DECREF(result);
    Py_DECREF(one);
    return shifted;
}

static int
is_unicode(PyObject *value)
{
    return PyUnicode_Check(value);
}

static int
is_integer(PyObject *value)
{
    return PyLong_Check(value);
}

static PyObject *
build_position(BytecodeVM *vm)
{
    PyObject *empty = NULL;
    PyObject *position = NULL;
    PyObject *index;
    PyObject *line;
    PyObject *column;
    PyObject *filename;
    Py_ssize_t start;

    if (!stack_has(vm, 4)) {
        return NULL;
    }
    start = vm->stack_size - 4;
    index = vm->stack[start];
    line = vm->stack[start + 1];
    column = vm->stack[start + 2];
    filename = vm->stack[start + 3];
    if (!is_integer(index) || !is_integer(line) || !is_integer(column) ||
        !is_unicode(filename)) {
        vm_error("bytecode built an invalid source position");
        return NULL;
    }
    empty = PyUnicode_FromString("");
    if (empty == NULL) {
        return NULL;
    }
    position = PyObject_CallFunctionObjArgs(
        vm->position_type, index, line, column, filename, empty, NULL);
    Py_DECREF(empty);
    if (position != NULL) {
        stack_discard_from(vm, start);
    }
    return position;
}

static PyObject *
new_node(PyObject *node_class)
{
    PyTypeObject *node_type;

    if (!PyType_Check(node_class)) {
        PyErr_SetString(
            PyExc_ValueError, "bytecode AST class table contains a non-type");
        return NULL;
    }
    node_type = (PyTypeObject *)node_class;
    if (node_type->tp_alloc == NULL) {
        PyErr_SetString(
            PyExc_ValueError, "bytecode AST class cannot be allocated");
        return NULL;
    }
    return node_type->tp_alloc(node_type, 0);
}

static PyObject *
build_node(BytecodeVM *vm)
{
    PyObject *class_id = NULL;
    PyObject *attr_count_object = NULL;
    PyObject *node_class = NULL;
    PyObject *node = NULL;
    PyObject *node_dict = NULL;
    Py_ssize_t class_index;
    Py_ssize_t attr_count;
    Py_ssize_t start;
    Py_ssize_t index;

    class_id = read_varint(vm);
    if (class_id == NULL) {
        return NULL;
    }
    class_index = PyLong_AsSsize_t(class_id);
    Py_DECREF(class_id);
    if (class_index == -1 && PyErr_Occurred()) {
        PyErr_Clear();
        vm_error("unknown AST node type id in bytecode instruction stream");
        return NULL;
    }
    if (class_index < 0 || class_index >= vm->class_count) {
        vm_error("unknown AST node type id in bytecode instruction stream");
        return NULL;
    }

    attr_count_object = read_varint(vm);
    if (attr_count_object == NULL) {
        return NULL;
    }
    attr_count = PyLong_AsSsize_t(attr_count_object);
    Py_DECREF(attr_count_object);
    if (attr_count == -1 && PyErr_Occurred()) {
        PyErr_Clear();
        vm_error("AST node declares too many attributes");
        return NULL;
    }
    if (attr_count < 0 || (size_t)attr_count > MAX_OBJECT_ATTRS) {
        vm_error("AST node declares too many attributes");
        return NULL;
    }
    if (attr_count > (PY_SSIZE_T_MAX / 2)) {
        vm_error("AST node declares too many attributes");
        return NULL;
    }
    if (!stack_has(vm, attr_count * 2)) {
        return NULL;
    }
    start = vm->stack_size - attr_count * 2;

    node_class = PySequence_GetItem(vm->classes, class_index);
    if (node_class == NULL) {
        return NULL;
    }
    node = new_node(node_class);
    Py_DECREF(node_class);
    if (node == NULL) {
        return NULL;
    }
    node_dict = PyObject_GetAttrString(node, "__dict__");
    if (node_dict == NULL || !PyDict_Check(node_dict)) {
        Py_XDECREF(node_dict);
        Py_DECREF(node);
        vm_error("bytecode AST node does not expose a dictionary");
        return NULL;
    }

    for (index = 0; index < attr_count * 2; index += 2) {
        PyObject *name = vm->stack[start + index];
        PyObject *value = vm->stack[start + index + 1];
        if (!is_unicode(name)) {
            Py_DECREF(node_dict);
            Py_DECREF(node);
            vm_error("malformed AST node attribute name");
            return NULL;
        }
        if (PyDict_SetItem(node_dict, name, value) < 0) {
            Py_DECREF(node_dict);
            Py_DECREF(node);
            return NULL;
        }
    }
    Py_DECREF(node_dict);
    stack_discard_from(vm, start);
    return node;
}

static PyObject *
build_container(BytecodeVM *vm, unsigned char opcode)
{
    Py_ssize_t count;
    PyObject *result = NULL;
    Py_ssize_t item_count;
    Py_ssize_t start;
    Py_ssize_t index;

    if (!varint_to_size(
            vm, &count, "instruction declares more values than the bytecode holds")) {
        return NULL;
    }
    if (opcode == OP_BUILD_DICT) {
        if (count > PY_SSIZE_T_MAX / 2) {
            vm_error("instruction declares too many dictionary entries");
            return NULL;
        }
        item_count = count * 2;
    } else {
        item_count = count;
    }
    if (count > vm->length || !stack_has(vm, item_count)) {
        return NULL;
    }
    start = vm->stack_size - item_count;

    switch (opcode) {
    case OP_BUILD_LIST:
        result = PyList_New(count);
        if (result != NULL) {
            for (index = 0; index < count; index++) {
                PyList_SET_ITEM(result, index, vm->stack[start + index]);
                vm->stack[start + index] = NULL;
            }
        }
        break;
    case OP_BUILD_TUPLE:
        result = PyTuple_New(count);
        if (result != NULL) {
            for (index = 0; index < count; index++) {
                PyTuple_SET_ITEM(result, index, vm->stack[start + index]);
                vm->stack[start + index] = NULL;
            }
        }
        break;
    case OP_BUILD_SET:
        result = PySet_New(NULL);
        if (result != NULL) {
            for (index = 0; index < count; index++) {
                if (PySet_Add(result, vm->stack[start + index]) < 0) {
                    Py_CLEAR(result);
                    break;
                }
            }
        }
        break;
    case OP_BUILD_FROZENSET:
        result = PyFrozenSet_New(NULL);
        if (result != NULL) {
            for (index = 0; index < count; index++) {
                if (PySet_Add(result, vm->stack[start + index]) < 0) {
                    Py_CLEAR(result);
                    break;
                }
            }
        }
        break;
    case OP_BUILD_DICT:
        result = PyDict_New();
        if (result != NULL) {
            for (index = 0; index < count; index++) {
                PyObject *key = vm->stack[start + index * 2];
                PyObject *value = vm->stack[start + index * 2 + 1];
                if (PyDict_SetItem(result, key, value) < 0) {
                    Py_CLEAR(result);
                    break;
                }
            }
        }
        break;
    default:
        vm_error("unknown container opcode");
        break;
    }
    if (result != NULL) {
        for (index = start; index < vm->stack_size; index++) {
            Py_XDECREF(vm->stack[index]);
            vm->stack[index] = NULL;
        }
        vm->stack_size = start;
    }
    return result;
}

static PyObject *
run_vm(BytecodeVM *vm)
{
    Py_ssize_t instruction_count = 0;
    PyObject *value;

    while (vm->position < vm->length) {
        unsigned char opcode;
        value = NULL;

        instruction_count++;
        if (instruction_count > MAX_INSTRUCTIONS) {
            vm_error("bytecode instruction stream contains too many instructions");
            return NULL;
        }
        if (!read_byte(vm, &opcode)) {
            return NULL;
        }

        switch (opcode) {
        case OP_NONE:
            value = Py_None;
            Py_INCREF(value);
            break;
        case OP_FALSE:
            value = Py_False;
            Py_INCREF(value);
            break;
        case OP_TRUE:
            value = Py_True;
            Py_INCREF(value);
            break;
        case OP_INT: {
            PyObject *encoded = read_varint(vm);
            if (encoded == NULL) {
                return NULL;
            }
            value = zigzag_integer(encoded);
            Py_DECREF(encoded);
            break;
        }
        case OP_FLOAT: {
            double number;
            if (!read_double(vm, &number)) {
                return NULL;
            }
            value = PyFloat_FromDouble(number);
            break;
        }
        case OP_COMPLEX: {
            double real;
            double imaginary;
            if (!read_double(vm, &real) || !read_double(vm, &imaginary)) {
                return NULL;
            }
            value = PyComplex_FromDoubles(real, imaginary);
            break;
        }
        case OP_STR:
            value = read_instruction_string(vm);
            break;
        case OP_BYTES:
            value = read_instruction_bytes(vm);
            break;
        case OP_BUILD_LIST:
        case OP_BUILD_TUPLE:
        case OP_BUILD_DICT:
        case OP_BUILD_SET:
        case OP_BUILD_FROZENSET:
            value = build_container(vm, opcode);
            break;
        case OP_BUILD_POSITION:
            value = build_position(vm);
            break;
        case OP_BUILD_NODE:
            value = build_node(vm);
            break;
        default:
            vm_error("unknown bytecode instruction");
            return NULL;
        }

        if (value == NULL || !push_owned(vm, value)) {
            return NULL;
        }
    }

    if (vm->stack_size != 1) {
        vm_error("bytecode instruction stream did not produce one program");
        return NULL;
    }
    value = vm->stack[0];
    Py_INCREF(value);
    return value;
}

static PyObject *
py_decode(PyObject *module, PyObject *args)
{
    PyObject *code_object;
    PyObject *classes;
    PyObject *position_type;
    BytecodeVM vm;
    PyObject *result;

    (void)module;
    if (!PyArg_ParseTuple(
            args, "O!OO:decode", &PyBytes_Type, &code_object, &classes,
            &position_type)) {
        return NULL;
    }
    if (!PySequence_Check(classes)) {
        PyErr_SetString(
            PyExc_TypeError, "bytecode VM class table must be a sequence");
        return NULL;
    }
    if (!PyCallable_Check(position_type)) {
        PyErr_SetString(
            PyExc_TypeError, "bytecode VM position type must be callable");
        return NULL;
    }

    vm.code = (const unsigned char *)PyBytes_AS_STRING(code_object);
    vm.length = PyBytes_GET_SIZE(code_object);
    vm.position = 0;
    vm.classes = classes;
    vm.class_count = PySequence_Size(classes);
    if (vm.class_count < 0) {
        return NULL;
    }
    vm.position_type = position_type;
    vm.stack = NULL;
    vm.stack_size = 0;
    vm.stack_capacity = 0;
    result = run_vm(&vm);
    stack_release(&vm);
    return result;
}

static PyMethodDef module_methods[] = {
    {
        "decode",
        py_decode,
        METH_VARARGS,
        "Execute a Lynxer postfix bytecode stream and return its program node.",
    },
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef module_definition = {
    PyModuleDef_HEAD_INIT,
    "bytecode_vm",
    "Native Lynxer bytecode stack-machine executor.",
    -1,
    module_methods,
};

PyMODINIT_FUNC
PyInit_bytecode_vm(void)
{
    return PyModule_Create(&module_definition);
}