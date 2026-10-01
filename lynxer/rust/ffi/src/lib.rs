//! The Lynxer native-call engine.
//!
//! The interpreter performs every native call through the C ABI in
//! `lynxer/ffi_abi.h`; this crate implements it with `libffi`, so a call site
//! can name any `cdecl` signature rather than the fixed shapes the interpreter
//! used to hard-code. The crate is a `staticlib` linked into the interpreter
//! and is deliberately not a `lynxer_module_init_v1` cdylib: it is not a stdlib
//! module and is never installed into `lynxer/stdlib/`.
//!
//! A Rust panic must never unwind across the C ABI, so the entry point is
//! wrapped in `catch_unwind` and reports a failure status instead.

use std::cell::RefCell;
use std::ffi::{c_char, c_void, CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr::null;
use std::slice;

use libffi::middle::{arg, Arg, Cif, CodePtr, Ret, Type};

// Result tags; keep in sync with `LynxerFfiResultTag` in `lynxer/ffi_abi.h`.
const RESULT_VOID: u32 = 0;
const RESULT_INT64: u32 = 1;
const RESULT_FLOAT64: u32 = 2;
const RESULT_CSTRING: u32 = 3;
const RESULT_BYTES: u32 = 4;

// Argument tags; keep in sync with `LynxerFfiArgTag` in `lynxer/ffi_abi.h`.
const ARG_INT: u32 = 0;
const ARG_FLOAT: u32 = 1;
const ARG_BOOL: u32 = 2;
const ARG_STRING: u32 = 3;
const ARG_UINT64: u32 = 4;
// The interpreter tags every other `Value` variant with this; the engine only
// ever rejects it.
#[allow(dead_code)]
const ARG_OTHER: u32 = 5;
const ARG_BYTES: u32 = 6;
const ARG_VALUE: u32 = 7;

/// The maximum number of packed arguments, matching the interpreter.
const MAX_PACKED_ARGS: usize = 256;

/// The largest buffer a `bytes` result may declare, so a malformed length
/// prefix cannot make the interpreter read far out of bounds.
const MAX_BUFFER: i64 = 1 << 30;
const RESULT_VALUE: u32 = 5;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LynxerFfiValueField {
    pub name: *const c_char,
    pub name_length: i64,
    pub field_type: *const c_char,
    pub type_length: i64,
    pub constant: u32,
    pub value: *const LynxerFfiValue,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LynxerFfiEnum {
    pub enum_name: *const c_char,
    pub enum_name_length: i64,
    pub variant_name: *const c_char,
    pub variant_name_length: i64,
    pub fields: *const LynxerFfiValueField,
    pub count: i64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LynxerFfiValue {
    pub tag: u32,
    pub value: LynxerFfiValuePayload,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub union LynxerFfiValuePayload {
    pub i: i64,
    pub u: u64,
    pub f: f64,
    pub boolean: u32,
    pub string: LynxerFfiData,
    pub bytes: LynxerFfiData,
    pub array: LynxerFfiArray,
    pub record: LynxerFfiRecord,
    pub enumeration: LynxerFfiEnum,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LynxerFfiData {
    pub data: *const u8,
    pub length: i64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LynxerFfiArray {
    pub items: *const LynxerFfiValue,
    pub count: i64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LynxerFfiRecord {
    pub fields: *const LynxerFfiValueField,
    pub count: i64,
    pub type_name: *const c_char,
    pub type_name_length: i64,
    pub display_name: *const c_char,
    pub display_name_length: i64,
    pub kind: u32,
}

/// One argument, mirroring `LynxerFfiArg` in `lynxer/ffi_abi.h`.
#[repr(C)]
pub struct LynxerFfiArg {
    pub tag: u32,
    pub i: i64,
    pub f: f64,
    pub s: *const c_char,
    pub data: *const u8,
    pub data_length: i64,
    pub value: *const LynxerFfiValue,
}

/// The call result, mirroring `LynxerFfiResult` in `lynxer/ffi_abi.h`.
#[repr(C)]
pub struct LynxerFfiResult {
    pub tag: u32,
    pub i: i64,
    pub f: f64,
    pub s: *const c_char,
    pub data: *const u8,
    pub data_length: i64,
    pub value: *const LynxerFfiValue,
}

impl LynxerFfiResult {
    fn scalar(tag: u32, i: i64, f: f64) -> Self {
        Self {
            tag,
            i,
            f,
            s: null(),
            data: null(),
            data_length: 0,
            value: null(),
        }
    }

    fn void() -> Self {
        Self::scalar(RESULT_VOID, 0, 0.0)
    }

    fn int64(value: i64) -> Self {
        Self::scalar(RESULT_INT64, value, 0.0)
    }

    fn float64(value: f64) -> Self {
        Self::scalar(RESULT_FLOAT64, 0, value)
    }

    fn from_cstring(pointer: *const c_char) -> Self {
        let bytes = if pointer.is_null() {
            &[][..]
        } else {
            unsafe { CStr::from_ptr(pointer) }.to_bytes()
        };
        Self {
            tag: RESULT_CSTRING,
            i: 0,
            f: 0.0,
            s: store_result_string(bytes),
            data: null(),
            data_length: 0,
            value: null(),
        }
    }

    fn bytes(data: *const u8, data_length: i64) -> Self {
        Self {
            tag: RESULT_BYTES,
            i: 0,
            f: 0.0,
            s: null(),
            data,
            data_length,
            value: null(),
        }
    }

    fn typed_value(value: *const LynxerFfiValue) -> Self {
        Self {
            tag: RESULT_VALUE,
            i: 0,
            f: 0.0,
            s: null(),
            data: null(),
            data_length: 0,
            value,
        }
    }

    /// A `bytes` return is a pointer to `[i64 length][payload]` in the module's
    /// own buffer. The buffer is not guaranteed to be 8-aligned, so the length
    /// prefix is copied out rather than dereferenced as a pointer to `i64`.
    fn from_bytes(pointer: *const u8) -> Result<Self, String> {
        if pointer.is_null() {
            return Ok(Self::bytes(null(), 0));
        }
        let mut prefix = [0u8; 8];
        unsafe { std::ptr::copy_nonoverlapping(pointer, prefix.as_mut_ptr(), 8) };
        let length = i64::from_le_bytes(prefix);
        if length < 0 || length > MAX_BUFFER {
            return Err("native call returned an invalid bytes length".to_string());
        }
        let data = if length == 0 {
            null()
        } else {
            unsafe { pointer.add(8) }
        };
        Ok(Self::bytes(data, length))
    }
}

thread_local! {
    // The message for the most recent failed call, or an empty string.
    static LAST_ERROR: RefCell<CString> = RefCell::new(CString::default());
    // The one live string result; its pointer stays valid until the next call.
    static RESULT_STRING: RefCell<CString> = RefCell::new(CString::default());
}

fn store_result_string(bytes: &[u8]) -> *const c_char {
    // A `cstring` result may not contain NUL; the interpreter's convention is
    // to replace any embedded NUL with a space, exactly as the crate this
    // engine replaces did.
    let sanitized: Vec<u8> = bytes
        .iter()
        .map(|&b| if b == 0 { b' ' } else { b })
        .collect();
    RESULT_STRING.with(|cell| {
        let mut buffer = cell.borrow_mut();
        *buffer = CString::new(sanitized).unwrap_or_default();
        buffer.as_ptr()
    })
}

fn set_error(message: &str) {
    LAST_ERROR.with(|cell| {
        *cell.borrow_mut() = CString::new(message.replace('\0', " ")).unwrap_or_default();
    });
}

fn clear_error() {
    LAST_ERROR.with(|cell| {
        *cell.borrow_mut() = CString::default();
    });
}

/// The result type of a signature.
#[derive(Clone, Copy)]
enum Return {
    Void,
    Int64,
    Float64,
    CString,
    Bytes,
    Value,
}

/// A concrete parameter type, after normalization.
#[derive(Clone, Copy)]
enum Parameter {
    Int64,
    Float64,
    CString,
    Bytes,
    Value,
}

/// The parameter list: either fixed types or the packed `...` convention.
enum Parameters {
    Fixed(Vec<Parameter>),
    Packed,
    /// The buffered packed form `cdecl:<ret>(...,bytes)`.
    PackedBuffers,
}

struct Signature {
    result: Return,
    parameters: Parameters,
}

/// Resolves a normalized type token, applying the same aliases the interpreter
/// always has: every integer width and `uintptr` collapse to `int64`, and
/// `double` to `float64`.
fn normalize_token(token: &str) -> Option<Return> {
    match token {
        "int64" | "uintptr" | "uint64" | "int32" | "uint32" | "int16" | "uint16" | "int8"
        | "uint8" => Some(Return::Int64),
        "float64" | "double" => Some(Return::Float64),
        "cstring" => Some(Return::CString),
        "bytes" => Some(Return::Bytes),
        "value" => Some(Return::Value),
        _ => None,
    }
}

fn parameter_for(return_type: Return) -> Parameter {
    match return_type {
        Return::Int64 => Parameter::Int64,
        Return::Float64 => Parameter::Float64,
        Return::CString => Parameter::CString,
        Return::Bytes => Parameter::Bytes,
        Return::Value => Parameter::Value,
        Return::Void => unreachable!("void is only a return type"),
    }
}

fn unsupported(original: &str) -> String {
    format!("unsupported native signature '{}'", original)
}

/// Parses `<ret>(<args>)`; typed values are gated behind the v2 prefix.
fn parse_signature(original: &str) -> Result<Signature, String> {
    let (version2, normalized) = if let Some(signature) = original.strip_prefix("cdecl:v2:") {
        (true, signature)
    } else if let Some(signature) = original.strip_prefix("v2:") {
        (true, signature)
    } else {
        (false, original.strip_prefix("cdecl:").unwrap_or(original))
    };
    let open = normalized
        .find('(')
        .ok_or_else(|| "invalid native function signature".to_string())?;
    let close = normalized
        .rfind(')')
        .ok_or_else(|| "invalid native function signature".to_string())?;
    if open >= close {
        return Err("invalid native function signature".to_string());
    }

    let result_token = &normalized[..open];
    let result = if result_token == "void" {
        Return::Void
    } else {
        normalize_token(result_token).ok_or_else(|| unsupported(original))?
    };

    // Split the parameter list the way the interpreter did: a trailing comma
    // does not introduce an empty token.
    let mut tokens: Vec<&str> = Vec::new();
    let text = &normalized[open + 1..close];
    let mut start = 0;
    while start < text.len() {
        match text[start..].find(',') {
            Some(offset) => {
                tokens.push(text[start..start + offset].trim());
                start += offset + 1;
            }
            None => {
                tokens.push(text[start..].trim());
                break;
            }
        }
    }

    let parameters = if tokens.len() == 1 && tokens[0] == "..." {
        Parameters::Packed
    } else if tokens.len() == 2 && tokens[0] == "..." && tokens[1] == "bytes" {
        Parameters::PackedBuffers
    } else {
        let mut parsed = Vec::with_capacity(tokens.len());
        for token in tokens {
            parsed.push(parameter_for(
                normalize_token(token).ok_or_else(|| unsupported(original))?,
            ));
        }
        Parameters::Fixed(parsed)
    };

    if version2 && !matches!(&parameters, Parameters::Fixed(_)) {
        return Err(unsupported(original));
    }

    if !version2
        && (matches!(result, Return::Value)
            || matches!(&parameters, Parameters::Fixed(types) if types.iter().any(|ty| matches!(ty, Parameter::Value))))
    {
        return Err(unsupported(original));
    }

    Ok(Signature { result, parameters })
}

// `Parameter::Bytes` expands to two libffi arguments (a pointer and a length)
// and is handled by the caller, never through this single-type mapping.
fn ffi_type(parameter: Parameter) -> Type {
    match parameter {
        Parameter::Int64 => Type::i64(),
        Parameter::Float64 => Type::f64(),
        Parameter::CString => Type::pointer(),
        Parameter::Bytes => Type::pointer(),
        Parameter::Value => Type::pointer(),
    }
}

fn return_type(result: Return) -> Type {
    match result {
        Return::Void => Type::void(),
        Return::Int64 => Type::i64(),
        Return::Float64 => Type::f64(),
        Return::CString => Type::pointer(),
        Return::Bytes => Type::pointer(),
        Return::Value => Type::pointer(),
    }
}

/// Where each validated argument landed in the owned storage.
enum Slot {
    Int(usize),
    Float(usize),
    Text(usize),
    BytesPtr(usize),
    BytesLen(usize),
    Value(usize),
}

unsafe fn invoke(
    address: *mut c_void,
    signature: *const c_char,
    arguments: *const LynxerFfiArg,
    argument_count: i64,
) -> Result<LynxerFfiResult, String> {
    if signature.is_null() {
        return Err("invalid native function signature".to_string());
    }
    let original = CStr::from_ptr(signature).to_string_lossy().into_owned();
    let parsed = parse_signature(&original)?;

    if address.is_null() {
        return Err("native call expects a non-zero function address".to_string());
    }

    let supplied: &[LynxerFfiArg] = if arguments.is_null() || argument_count <= 0 {
        &[]
    } else {
        slice::from_raw_parts(arguments, argument_count as usize)
    };

    match parsed.parameters {
        Parameters::Fixed(parameters) => {
            if supplied.len() != parameters.len() {
                return Err(format!(
                    "native call argument count does not match signature '{}'",
                    original
                ));
            }
            call_fixed(address, parsed.result, &parameters, supplied, &original)
        }
        Parameters::Packed => call_packed(address, parsed.result, supplied),
        Parameters::PackedBuffers => {
            call_packed_buffers(address, parsed.result, supplied)
        }
    }
}

unsafe fn call_fixed(
    address: *mut c_void,
    result: Return,
    parameters: &[Parameter],
    supplied: &[LynxerFfiArg],
    original: &str,
) -> Result<LynxerFfiResult, String> {
    let mut ints: Vec<i64> = Vec::new();
    let mut floats: Vec<f64> = Vec::new();
    let mut strings: Vec<CString> = Vec::new();
    let mut buffers: Vec<*const u8> = Vec::new();
    let mut lengths: Vec<i64> = Vec::new();
    let mut values: Vec<*const LynxerFfiValue> = Vec::new();
    let mut plan: Vec<Slot> = Vec::with_capacity(parameters.len());

    for (index, parameter) in parameters.iter().enumerate() {
        let value = &supplied[index];
        match parameter {
            Parameter::Int64 => {
                if value.tag != ARG_INT {
                    return Err("native call expected an integer argument".to_string());
                }
                plan.push(Slot::Int(ints.len()));
                ints.push(value.i);
            }
            Parameter::Float64 => {
                let number = match value.tag {
                    ARG_INT => value.i as f64,
                    ARG_FLOAT => value.f,
                    ARG_UINT64 => value.i as u64 as f64,
                    _ => return Err("numeric value required".to_string()),
                };
                plan.push(Slot::Float(floats.len()));
                floats.push(number);
            }
            Parameter::CString => {
                if value.tag != ARG_STRING {
                    return Err("native call expected a string argument".to_string());
                }
                plan.push(Slot::Text(strings.len()));
                strings.push(cstring_from_raw(value.s));
            }
            Parameter::Bytes => {
                if value.tag != ARG_BYTES {
                    return Err("native call expected a bytes argument".to_string());
                }
                let length = value.data_length.max(0);
                plan.push(Slot::BytesPtr(buffers.len()));
                buffers.push(value.data);
                plan.push(Slot::BytesLen(lengths.len()));
                lengths.push(length);
            }
            Parameter::Value => {
                if value.tag != ARG_VALUE || value.value.is_null() {
                    return Err("native call expected a typed value argument".to_string());
                }
                plan.push(Slot::Value(values.len()));
                values.push(value.value);
            }
        }
    }

    let text_pointers: Vec<*const c_char> = strings.iter().map(|text| text.as_ptr()).collect();

    let mut call_args: Vec<Arg> = Vec::with_capacity(plan.len());
    for slot in &plan {
        call_args.push(match slot {
            Slot::Int(index) => arg(&ints[*index]),
            Slot::Float(index) => arg(&floats[*index]),
            Slot::Text(index) => arg(&text_pointers[*index]),
            Slot::BytesPtr(index) => arg(&buffers[*index]),
            Slot::BytesLen(index) => arg(&lengths[*index]),
            Slot::Value(index) => arg(&values[*index]),
        });
    }

    // A `bytes` parameter occupies two libffi slots (pointer, length).
    let types: Vec<Type> = parameters
        .iter()
        .flat_map(|parameter| match parameter {
            Parameter::Bytes => vec![Type::pointer(), Type::i64()],
            other => vec![ffi_type(*other)],
        })
        .collect();
    let cif = Cif::try_new(types, return_type(result)).map_err(|_| unsupported(original))?;
    let code = CodePtr(address);

    let outcome = match result {
        Return::Void => {
            cif.call_return_into(code, &call_args, Ret::void());
            LynxerFfiResult::void()
        }
        Return::Int64 => LynxerFfiResult::int64(cif.call::<i64>(code, &call_args)),
        Return::Float64 => LynxerFfiResult::float64(cif.call::<f64>(code, &call_args)),
        Return::CString => {
            LynxerFfiResult::from_cstring(cif.call::<*const c_char>(code, &call_args))
        }
        Return::Bytes => LynxerFfiResult::from_bytes(cif.call::<*const u8>(code, &call_args))?,
        Return::Value => {
            LynxerFfiResult::typed_value(cif.call::<*const LynxerFfiValue>(code, &call_args))
        }
    };
    Ok(outcome)
}

unsafe fn call_packed(
    address: *mut c_void,
    result: Return,
    supplied: &[LynxerFfiArg],
) -> Result<LynxerFfiResult, String> {
    if supplied.len() > MAX_PACKED_ARGS {
        return Err("native call has too many packed arguments".to_string());
    }

    let mut numbers: Vec<f64> = Vec::new();
    let mut strings: Vec<CString> = Vec::new();
    for value in supplied {
        match value.tag {
            ARG_INT => numbers.push(value.i as f64),
            ARG_FLOAT => numbers.push(value.f),
            ARG_BOOL => numbers.push(if value.i != 0 { 1.0 } else { 0.0 }),
            ARG_STRING => strings.push(cstring_from_raw(value.s)),
            _ => return Err("native call argument is not a number or string".to_string()),
        }
    }

    let text_pointers: Vec<*const c_char> = strings.iter().map(|text| text.as_ptr()).collect();
    let number_pointer: *const f64 = if numbers.is_empty() {
        null()
    } else {
        numbers.as_ptr()
    };
    let string_pointer: *const *const c_char = if text_pointers.is_empty() {
        null()
    } else {
        text_pointers.as_ptr()
    };
    let number_count = numbers.len() as i64;
    let string_count = text_pointers.len() as i64;

    let types = vec![Type::pointer(), Type::i64(), Type::pointer(), Type::i64()];
    let cif = Cif::try_new(types, return_type(result))
        .map_err(|_| "unsupported native signature '...'".to_string())?;
    let code = CodePtr(address);
    let call_args = [
        arg(&number_pointer),
        arg(&number_count),
        arg(&string_pointer),
        arg(&string_count),
    ];

    let outcome = match result {
        Return::Void => {
            cif.call_return_into(code, &call_args, Ret::void());
            LynxerFfiResult::void()
        }
        Return::Int64 => LynxerFfiResult::int64(cif.call::<i64>(code, &call_args)),
        Return::Float64 => LynxerFfiResult::float64(cif.call::<f64>(code, &call_args)),
        Return::CString => {
            LynxerFfiResult::from_cstring(cif.call::<*const c_char>(code, &call_args))
        }
        Return::Bytes => LynxerFfiResult::from_bytes(cif.call::<*const u8>(code, &call_args))?,
        Return::Value => {
            LynxerFfiResult::typed_value(cif.call::<*const LynxerFfiValue>(code, &call_args))
        }
    };
    Ok(outcome)
}

/// The buffered packed form: numbers and strings as [`call_packed`], plus the
/// byte buffers a module declares with the `bytes` tag. The callee sees seven
/// scalars.
unsafe fn call_packed_buffers(
    address: *mut c_void,
    result: Return,
    supplied: &[LynxerFfiArg],
) -> Result<LynxerFfiResult, String> {
    if supplied.len() > MAX_PACKED_ARGS {
        return Err("native call has too many packed arguments".to_string());
    }

    let mut numbers: Vec<f64> = Vec::new();
    let mut strings: Vec<CString> = Vec::new();
    let mut buffers: Vec<*const u8> = Vec::new();
    let mut lengths: Vec<i64> = Vec::new();
    for value in supplied {
        match value.tag {
            ARG_INT => numbers.push(value.i as f64),
            ARG_FLOAT => numbers.push(value.f),
            ARG_BOOL => numbers.push(if value.i != 0 { 1.0 } else { 0.0 }),
            ARG_STRING => strings.push(cstring_from_raw(value.s)),
            ARG_BYTES => {
                let length = value.data_length.max(0);
                buffers.push(value.data);
                lengths.push(length);
            }
            _ => {
                return Err(
                    "native call argument is not a number, string or bytes".to_string()
                )
            }
        }
    }

    let text_pointers: Vec<*const c_char> = strings.iter().map(|text| text.as_ptr()).collect();
    let number_pointer: *const f64 = if numbers.is_empty() {
        null()
    } else {
        numbers.as_ptr()
    };
    let string_pointer: *const *const c_char = if text_pointers.is_empty() {
        null()
    } else {
        text_pointers.as_ptr()
    };
    let buffer_pointer: *const *const u8 = if buffers.is_empty() {
        null()
    } else {
        buffers.as_ptr()
    };
    let length_pointer: *const i64 = if lengths.is_empty() {
        null()
    } else {
        lengths.as_ptr()
    };
    let number_count = numbers.len() as i64;
    let string_count = text_pointers.len() as i64;
    let buffer_count = buffers.len() as i64;

    let types = vec![
        Type::pointer(),
        Type::i64(),
        Type::pointer(),
        Type::i64(),
        Type::pointer(),
        Type::pointer(),
        Type::i64(),
    ];
    let cif = Cif::try_new(types, return_type(result))
        .map_err(|_| "unsupported native signature '...,bytes'".to_string())?;
    let code = CodePtr(address);
    let call_args = [
        arg(&number_pointer),
        arg(&number_count),
        arg(&string_pointer),
        arg(&string_count),
        arg(&buffer_pointer),
        arg(&length_pointer),
        arg(&buffer_count),
    ];

    let outcome = match result {
        Return::Void => {
            cif.call_return_into(code, &call_args, Ret::void());
            LynxerFfiResult::void()
        }
        Return::Int64 => LynxerFfiResult::int64(cif.call::<i64>(code, &call_args)),
        Return::Float64 => LynxerFfiResult::float64(cif.call::<f64>(code, &call_args)),
        Return::CString => {
            LynxerFfiResult::from_cstring(cif.call::<*const c_char>(code, &call_args))
        }
        Return::Bytes => LynxerFfiResult::from_bytes(cif.call::<*const u8>(code, &call_args))?,
        Return::Value => {
            LynxerFfiResult::typed_value(cif.call::<*const LynxerFfiValue>(code, &call_args))
        }
    };
    Ok(outcome)
}

/// The interpreter's `std::string::c_str()` equivalent: the callee sees bytes
/// up to the first NUL.
fn cstring_from_raw(pointer: *const c_char) -> CString {
    if pointer.is_null() {
        return CString::default();
    }
    let bytes = unsafe { CStr::from_ptr(pointer) }.to_bytes();
    CString::new(bytes).unwrap_or_default()
}

/// Invokes the native function at `address`. See `lynxer/ffi_abi.h`.
///
/// # Safety
/// `address` must point at a function with the calling convention and types
/// described by `signature`; `arguments` must point to `argument_count` valid
/// `LynxerFfiArg` values; `out` may be null.
#[no_mangle]
pub unsafe extern "C" fn lynxer_ffi_call(
    address: *mut c_void,
    signature: *const c_char,
    arguments: *const LynxerFfiArg,
    argument_count: i64,
    out: *mut LynxerFfiResult,
) -> i32 {
    let outcome = catch_unwind(AssertUnwindSafe(|| {
        invoke(address, signature, arguments, argument_count)
    }));

    match outcome {
        Ok(Ok(result)) => {
            clear_error();
            if !out.is_null() {
                *out = result;
            }
            0
        }
        Ok(Err(message)) => {
            set_error(&message);
            1
        }
        Err(_) => {
            set_error("native call failed");
            1
        }
    }
}

/// The message for the most recent failed call on this thread. See
/// `lynxer/ffi_abi.h`.
#[no_mangle]
pub extern "C" fn lynxer_ffi_last_error() -> *const c_char {
    LAST_ERROR.with(|cell| cell.borrow().as_ptr())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn typed_value_signatures_are_versioned_and_preserve_v1() {
        let typed = parse_signature("cdecl:v2:value(value)").unwrap();
        assert!(matches!(typed.result, Return::Value));
        assert!(matches!(
            typed.parameters,
            Parameters::Fixed(parameters)
                if matches!(parameters.as_slice(), [Parameter::Value])
        ));

        let legacy = parse_signature("cdecl:int64(int64)").unwrap();
        assert!(matches!(legacy.result, Return::Int64));
        assert!(matches!(
            legacy.parameters,
            Parameters::Fixed(parameters)
                if matches!(parameters.as_slice(), [Parameter::Int64])
        ));
        assert!(parse_signature("cdecl:value(value)").is_err());
        assert!(parse_signature("cdecl:v2:value(...)").is_err());
    }

    #[test]
    fn c_abi_struct_layout_matches_the_header_on_64_bit_targets() {
        if cfg!(target_pointer_width = "64") {
            use core::mem::{align_of, size_of};
            assert_eq!(size_of::<LynxerFfiValue>(), 64);
            assert_eq!(align_of::<LynxerFfiValue>(), 8);
            assert_eq!(size_of::<LynxerFfiValueField>(), 48);
            assert_eq!(size_of::<LynxerFfiEnum>(), 48);
            assert_eq!(size_of::<LynxerFfiArg>(), 56);
            assert_eq!(size_of::<LynxerFfiResult>(), 56);
        }
    }
}
