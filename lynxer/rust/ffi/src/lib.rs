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

use libffi::middle::{arg, ffi_abi_FFI_DEFAULT_ABI, Arg, Cif, CodePtr, FfiAbi, Ret, Type};

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
    Int32,
    UInt32,
    Pointer,
    Utf16,
    Float64,
    CString,
    Bytes,
    Value,
}

/// A concrete parameter type, after normalization.
#[derive(Clone)]
enum Parameter {
    Int64,
    Int32,
    UInt32,
    Pointer,
    Utf16,
    Struct(Vec<Parameter>),
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
    stdcall: bool,
}

/// Resolves a signature token while preserving pointer and 32-bit integer
/// widths needed by Win32 APIs. `double` aliases `float64`.
fn normalize_token(token: &str) -> Option<Return> {
    match token {
        "int32" => Some(Return::Int32),
        "uint32" => Some(Return::UInt32),
        "uintptr" | "handle" | "hwnd" | "socket" => Some(Return::Pointer),
        "utf16" | "wchar" => Some(Return::Utf16),
        "int64" | "uint64" | "int16" | "uint16" | "int8" | "uint8" => Some(Return::Int64),
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
        Return::Int32 => Parameter::Int32,
        Return::UInt32 => Parameter::UInt32,
        Return::Pointer => Parameter::Pointer,
        Return::Utf16 => Parameter::Utf16,
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

fn split_signature_types(text: &str) -> Result<Vec<&str>, String> {
    let mut parts = Vec::new();
    let mut depth = 0usize;
    let mut start = 0usize;
    for (index, character) in text.char_indices() {
        match character {
            '(' => depth += 1,
            ')' => {
                if depth == 0 {
                    return Err("invalid native function signature".to_string());
                }
                depth -= 1;
            }
            ',' if depth == 0 => {
                parts.push(text[start..index].trim());
                start = index + 1;
            }
            _ => {}
        }
    }
    if depth != 0 {
        return Err("invalid native function signature".to_string());
    }
    if start < text.len() {
        parts.push(text[start..].trim());
    }
    Ok(parts)
}

fn parse_parameter(token: &str, depth: usize) -> Option<Parameter> {
    if depth < 8 && token.starts_with("struct(") && token.ends_with(')') {
        let fields = split_signature_types(&token[7..token.len() - 1]).ok()?;
        if fields.is_empty() || fields.len() > 64 || fields.iter().any(|field| field.is_empty()) {
            return None;
        }
        let parsed: Vec<Parameter> = fields
            .into_iter()
            .map(|field| parse_parameter(field, depth + 1))
            .collect::<Option<_>>()?;
        if parsed.iter().any(|field| {
            matches!(
                field,
                Parameter::Bytes | Parameter::Value | Parameter::Utf16 | Parameter::CString
            )
        }) {
            return None;
        }
        Some(Parameter::Struct(parsed))
    } else {
        normalize_token(token).map(parameter_for)
    }
}

/// Parses `<ret>(<args>)`; typed values are gated behind the v2 prefix.
fn parse_signature(original: &str) -> Result<Signature, String> {
    let (stdcall, convention_stripped) = if let Some(signature) = original.strip_prefix("stdcall:")
    {
        (true, signature)
    } else if let Some(signature) = original.strip_prefix("cdecl:") {
        (false, signature)
    } else {
        (false, original)
    };
    let (version2, normalized) = if let Some(signature) = convention_stripped.strip_prefix("v2:") {
        (true, signature)
    } else {
        (false, convention_stripped)
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
    let text = &normalized[open + 1..close];
    let tokens = split_signature_types(text)?;

    let parameters = if tokens.len() == 1 && tokens[0] == "..." {
        Parameters::Packed
    } else if tokens.len() == 2 && tokens[0] == "..." && tokens[1] == "bytes" {
        Parameters::PackedBuffers
    } else {
        let mut parsed = Vec::with_capacity(tokens.len());
        for token in tokens {
            parsed.push(parse_parameter(token, 0).ok_or_else(|| unsupported(original))?);
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

    Ok(Signature {
        result,
        parameters,
        stdcall,
    })
}

fn calling_abi(stdcall: bool) -> Result<FfiAbi, String> {
    if !stdcall {
        return Ok(ffi_abi_FFI_DEFAULT_ABI);
    }
    #[cfg(all(windows, target_arch = "x86"))]
    {
        return Ok(libffi::raw::ffi_abi_FFI_STDCALL);
    }
    #[cfg(all(windows, not(target_arch = "x86")))]
    {
        return Ok(ffi_abi_FFI_DEFAULT_ABI);
    }
    #[cfg(not(windows))]
    {
        Err("stdcall native signatures are only supported on Windows".to_string())
    }
}

struct StructFieldLayout {
    parameter: Parameter,
    offset: usize,
    size: usize,
    nested: Option<StructLayout>,
}

struct StructLayout {
    fields: Vec<StructFieldLayout>,
    size: usize,
}

fn build_ffi_type(
    parameter: &Parameter,
    abi: FfiAbi,
) -> Result<(Type, usize, Option<StructLayout>), String> {
    if let Parameter::Struct(fields) = parameter {
        let mut types = Vec::with_capacity(fields.len());
        let mut children = Vec::with_capacity(fields.len());
        for field in fields {
            let (field_type, size, nested) = build_ffi_type(field, abi)?;
            types.push(field_type);
            children.push((size, nested));
        }
        let mut structure = Type::structure(types);
        let offsets = structure
            .struct_offsets(abi)
            .map_err(|_| "invalid native struct layout".to_string())?;
        let size = unsafe { (*structure.as_raw_ptr()).size };
        if size == 0 || size > 1 << 20 {
            return Err("native struct layout exceeds 1 MiB".to_string());
        }
        let layout = StructLayout {
            fields: fields
                .iter()
                .cloned()
                .zip(offsets)
                .zip(children)
                .map(|((parameter, offset), (size, nested))| StructFieldLayout {
                    parameter,
                    offset,
                    size,
                    nested,
                })
                .collect(),
            size,
        };
        return Ok((structure, size, Some(layout)));
    }

    let ffi_type = ffi_type(parameter.clone());
    let size = unsafe { (*ffi_type.as_raw_ptr()).size };
    Ok((ffi_type, size, None))
}

unsafe fn aggregate_field(
    value: *const LynxerFfiValue,
    index: usize,
) -> Option<*const LynxerFfiValue> {
    if value.is_null() {
        return None;
    }
    match (*value).tag {
        6 | 8 => {
            let array = (*value).value.array;
            if array.count < 0 || index >= array.count as usize || array.items.is_null() {
                None
            } else {
                Some(array.items.add(index))
            }
        }
        7 => {
            let record = (*value).value.record;
            if record.count < 0 || index >= record.count as usize || record.fields.is_null() {
                None
            } else {
                Some((*record.fields.add(index)).value)
            }
        }
        _ => None,
    }
}

unsafe fn write_struct_value(
    value: *const LynxerFfiValue,
    layout: &StructLayout,
    bytes: &mut [u8],
) -> Result<(), String> {
    if bytes.len() < layout.size {
        return Err("native struct storage is too small".to_string());
    }
    for (index, field) in layout.fields.iter().enumerate() {
        let child = aggregate_field(value, index)
            .ok_or_else(|| "native struct argument has the wrong number of fields".to_string())?;
        let end = field
            .offset
            .checked_add(field.size)
            .filter(|end| *end <= bytes.len())
            .ok_or_else(|| "invalid native struct field offset".to_string())?;
        let target = &mut bytes[field.offset..end];
        match &field.parameter {
            Parameter::Struct(_) => {
                let nested = field
                    .nested
                    .as_ref()
                    .ok_or_else(|| "invalid nested native struct layout".to_string())?;
                write_struct_value(child, nested, target)?;
            }
            Parameter::Int64 => {
                if (*child).tag != 1 || target.len() != 8 {
                    return Err("native struct field expects an int64".to_string());
                }
                target.copy_from_slice(&(*child).value.i.to_ne_bytes());
            }
            Parameter::Int32 => {
                if (*child).tag != 1 || target.len() != 4 {
                    return Err("native struct field expects an int32".to_string());
                }
                let number = (*child).value.i;
                if number < i32::MIN as i64 || number > i32::MAX as i64 {
                    return Err("native struct int32 field is out of range".to_string());
                }
                target.copy_from_slice(&(number as i32).to_ne_bytes());
            }
            Parameter::UInt32 => {
                if target.len() != 4 {
                    return Err("invalid native struct uint32 field".to_string());
                }
                let number = match (*child).tag {
                    1 if (*child).value.i >= 0 => (*child).value.i as u64,
                    9 => (*child).value.u,
                    _ => return Err("native struct field expects a uint32".to_string()),
                };
                if number > u32::MAX as u64 {
                    return Err("native struct uint32 field is out of range".to_string());
                }
                target.copy_from_slice(&(number as u32).to_ne_bytes());
            }
            Parameter::Pointer => {
                if target.len() != core::mem::size_of::<usize>() {
                    return Err("invalid native struct pointer field".to_string());
                }
                let pointer = match (*child).tag {
                    0 => 0usize,
                    1 => (*child).value.i as usize,
                    9 => (*child).value.u as usize,
                    _ => {
                        return Err(
                            "native struct field expects a pointer-sized integer".to_string()
                        )
                    }
                };
                target.copy_from_slice(&pointer.to_ne_bytes());
            }
            Parameter::Float64 => {
                if target.len() != 8 {
                    return Err("invalid native struct float64 field".to_string());
                }
                let number = match (*child).tag {
                    1 => (*child).value.i as f64,
                    2 => (*child).value.f,
                    9 => (*child).value.u as f64,
                    _ => return Err("native struct field expects a number".to_string()),
                };
                target.copy_from_slice(&number.to_ne_bytes());
            }
            Parameter::Utf16 | Parameter::CString | Parameter::Bytes | Parameter::Value => {
                return Err("native struct fields do not support string or byte values".to_string())
            }
        }
    }
    if aggregate_field(value, layout.fields.len()).is_some() {
        return Err("native struct argument has too many fields".to_string());
    }
    Ok(())
}

// `Parameter::Bytes` expands to two libffi arguments (a pointer and a length)
// and is handled by the caller, never through this single-type mapping.
fn ffi_type(parameter: Parameter) -> Type {
    match parameter {
        Parameter::Int64 => Type::i64(),
        Parameter::Int32 => Type::i32(),
        Parameter::UInt32 => Type::u32(),
        Parameter::Pointer => Type::pointer(),
        Parameter::Utf16 => Type::pointer(),
        Parameter::Struct(_) => unreachable!("struct fields use ABI-specific layouts"),
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
        Return::Int32 => Type::i32(),
        Return::UInt32 => Type::u32(),
        Return::Pointer => Type::pointer(),
        Return::Utf16 => Type::pointer(),
        Return::Float64 => Type::f64(),
        Return::CString => Type::pointer(),
        Return::Bytes => Type::pointer(),
        Return::Value => Type::pointer(),
    }
}

/// Where each validated argument landed in the owned storage.
enum Slot {
    Int(usize),
    Int32(usize),
    UInt32(usize),
    Pointer(usize),
    Utf16(usize),
    Struct(usize),
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

    let abi = calling_abi(parsed.stdcall)?;
    match parsed.parameters {
        Parameters::Fixed(parameters) => {
            if supplied.len() != parameters.len() {
                return Err(format!(
                    "native call argument count does not match signature '{}'",
                    original
                ));
            }
            call_fixed(
                address,
                parsed.result,
                &parameters,
                supplied,
                &original,
                abi,
            )
        }
        Parameters::Packed => call_packed(address, parsed.result, supplied, abi),
        Parameters::PackedBuffers => call_packed_buffers(address, parsed.result, supplied, abi),
    }
}

unsafe fn call_fixed(
    address: *mut c_void,
    result: Return,
    parameters: &[Parameter],
    supplied: &[LynxerFfiArg],
    original: &str,
    abi: FfiAbi,
) -> Result<LynxerFfiResult, String> {
    let mut ints: Vec<i64> = Vec::new();
    let mut ints32: Vec<i32> = Vec::new();
    let mut uints32: Vec<u32> = Vec::new();
    let mut pointers: Vec<*mut c_void> = Vec::new();
    let mut floats: Vec<f64> = Vec::new();
    let mut strings: Vec<CString> = Vec::new();
    let mut wide_strings: Vec<Vec<u16>> = Vec::new();
    let mut buffers: Vec<*const u8> = Vec::new();
    let mut lengths: Vec<i64> = Vec::new();
    let mut values: Vec<*const LynxerFfiValue> = Vec::new();
    let mut struct_values: Vec<Vec<u64>> = Vec::new();
    let mut plan: Vec<Slot> = Vec::with_capacity(parameters.len());
    let mut types: Vec<Type> = Vec::with_capacity(parameters.len());
    let mut layouts: Vec<Option<StructLayout>> = Vec::with_capacity(parameters.len());

    for (index, parameter) in parameters.iter().enumerate() {
        let value = &supplied[index];
        if matches!(parameter, Parameter::Bytes) {
            types.push(Type::pointer());
            types.push(Type::i64());
            layouts.push(None);
        } else {
            let (native_type, _, layout) = build_ffi_type(parameter, abi)?;
            types.push(native_type);
            layouts.push(layout);
        }
        match parameter {
            Parameter::Int64 => {
                if value.tag != ARG_INT {
                    return Err("native call expected an integer argument".to_string());
                }
                plan.push(Slot::Int(ints.len()));
                ints.push(value.i);
            }
            Parameter::Int32 => {
                if value.tag != ARG_INT || value.i < i32::MIN as i64 || value.i > i32::MAX as i64 {
                    return Err("native call expected a signed 32-bit integer argument".to_string());
                }
                plan.push(Slot::Int32(ints32.len()));
                ints32.push(value.i as i32);
            }
            Parameter::UInt32 => {
                let number = match value.tag {
                    ARG_INT if value.i >= 0 => value.i as u64,
                    ARG_UINT64 => value.i as u64,
                    _ => {
                        return Err(
                            "native call expected an unsigned 32-bit integer argument".to_string()
                        )
                    }
                };
                if number > u32::MAX as u64 {
                    return Err(
                        "native call expected an unsigned 32-bit integer argument".to_string()
                    );
                }
                plan.push(Slot::UInt32(uints32.len()));
                uints32.push(number as u32);
            }
            Parameter::Pointer => {
                plan.push(Slot::Pointer(pointers.len()));
                let address = match value.tag {
                    ARG_INT => {
                        #[cfg(target_pointer_width = "32")]
                        if value.i < i32::MIN as i64 || value.i > u32::MAX as i64 {
                            return Err("native call pointer argument is out of range".to_string());
                        }
                        value.i as usize
                    }
                    ARG_UINT64 if value.i as u64 <= usize::MAX as u64 => value.i as u64 as usize,
                    ARG_UINT64 => {
                        return Err("native call pointer argument is out of range".to_string())
                    }
                    _ => return Err("native call expected an integer pointer argument".to_string()),
                };
                pointers.push(address as *mut c_void);
            }
            Parameter::Utf16 => {
                if value.tag != ARG_STRING {
                    return Err("native call expected a string argument".to_string());
                }
                let text = cstring_from_raw(value.s);
                let wide = text
                    .to_string_lossy()
                    .encode_utf16()
                    .chain(std::iter::once(0))
                    .collect();
                plan.push(Slot::Utf16(wide_strings.len()));
                wide_strings.push(wide);
            }
            Parameter::Struct(_) => {
                if value.tag != ARG_VALUE || value.value.is_null() {
                    return Err("native call expected an aggregate struct argument".to_string());
                }
                let layout = layouts[index]
                    .as_ref()
                    .ok_or_else(|| "invalid native struct layout".to_string())?;
                let word_size = core::mem::size_of::<u64>();
                let word_count = (layout.size + word_size - 1) / word_size;
                let mut storage = vec![0u64; word_count];
                let bytes =
                    core::slice::from_raw_parts_mut(storage.as_mut_ptr().cast::<u8>(), layout.size);
                write_struct_value(value.value, layout, bytes)?;
                plan.push(Slot::Struct(struct_values.len()));
                struct_values.push(storage);
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
    let wide_text_pointers: Vec<*const u16> =
        wide_strings.iter().map(|text| text.as_ptr()).collect();

    let mut call_args: Vec<Arg> = Vec::with_capacity(plan.len());
    for slot in &plan {
        call_args.push(match slot {
            Slot::Int(index) => arg(&ints[*index]),
            Slot::Int32(index) => arg(&ints32[*index]),
            Slot::UInt32(index) => arg(&uints32[*index]),
            Slot::Pointer(index) => arg(&pointers[*index]),
            Slot::Utf16(index) => arg(&wide_text_pointers[*index]),
            Slot::Struct(index) => arg(&struct_values[*index][0]),
            Slot::Float(index) => arg(&floats[*index]),
            Slot::Text(index) => arg(&text_pointers[*index]),
            Slot::BytesPtr(index) => arg(&buffers[*index]),
            Slot::BytesLen(index) => arg(&lengths[*index]),
            Slot::Value(index) => arg(&values[*index]),
        });
    }

    let cif = Cif::try_new_with_abi(types, return_type(result), abi)
        .map_err(|_| unsupported(original))?;
    let code = CodePtr(address);

    let outcome = match result {
        Return::Void => {
            cif.call_return_into(code, &call_args, Ret::void());
            LynxerFfiResult::void()
        }
        Return::Int64 => LynxerFfiResult::int64(cif.call::<i64>(code, &call_args)),
        Return::Int32 => LynxerFfiResult::int64(cif.call::<i32>(code, &call_args) as i64),
        Return::UInt32 => LynxerFfiResult::int64(cif.call::<u32>(code, &call_args) as i64),
        Return::Pointer => {
            LynxerFfiResult::int64(cif.call::<*mut c_void>(code, &call_args) as usize as i64)
        }
        Return::Utf16 => {
            let text = utf16_to_cstring(cif.call::<*const u16>(code, &call_args))?;
            LynxerFfiResult::from_cstring(text.as_ptr())
        }
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
    abi: FfiAbi,
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
    let cif = Cif::try_new_with_abi(types, return_type(result), abi)
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
        Return::Int32 => LynxerFfiResult::int64(cif.call::<i32>(code, &call_args) as i64),
        Return::UInt32 => LynxerFfiResult::int64(cif.call::<u32>(code, &call_args) as i64),
        Return::Pointer => {
            LynxerFfiResult::int64(cif.call::<*mut c_void>(code, &call_args) as usize as i64)
        }
        Return::Utf16 => {
            let text = utf16_to_cstring(cif.call::<*const u16>(code, &call_args))?;
            LynxerFfiResult::from_cstring(text.as_ptr())
        }
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
    abi: FfiAbi,
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
            _ => return Err("native call argument is not a number, string or bytes".to_string()),
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
    let cif = Cif::try_new_with_abi(types, return_type(result), abi)
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
        Return::Int32 => LynxerFfiResult::int64(cif.call::<i32>(code, &call_args) as i64),
        Return::UInt32 => LynxerFfiResult::int64(cif.call::<u32>(code, &call_args) as i64),
        Return::Pointer => {
            LynxerFfiResult::int64(cif.call::<*mut c_void>(code, &call_args) as usize as i64)
        }
        Return::Utf16 => {
            let text = utf16_to_cstring(cif.call::<*const u16>(code, &call_args))?;
            LynxerFfiResult::from_cstring(text.as_ptr())
        }
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

unsafe fn utf16_to_cstring(pointer: *const u16) -> Result<CString, String> {
    if pointer.is_null() {
        return Ok(CString::default());
    }
    const MAX_UTF16_UNITS: usize = 1 << 24;
    let mut length = 0usize;
    while length < MAX_UTF16_UNITS && *pointer.add(length) != 0 {
        length += 1;
    }
    if length == MAX_UTF16_UNITS {
        return Err("native UTF-16 string is missing a terminator".to_string());
    }
    let text = String::from_utf16_lossy(slice::from_raw_parts(pointer, length));
    CString::new(text).map_err(|_| "native UTF-16 string contains a NUL".to_string())
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
    fn stdcall_signatures_select_the_windows_calling_convention() {
        let signature = parse_signature("stdcall:int32(int32)").unwrap();
        assert!(signature.stdcall);
        assert!(matches!(signature.result, Return::Int32));
        assert!(matches!(
            signature.parameters,
            Parameters::Fixed(parameters)
                if matches!(parameters.as_slice(), [Parameter::Int32])
        ));

        assert!(!parse_signature("cdecl:int32(int32)").unwrap().stdcall);
        #[cfg(not(windows))]
        assert!(calling_abi(true).is_err());
        #[cfg(windows)]
        assert!(calling_abi(true).is_ok());
    }

    #[test]
    fn fixed_width_integer_signatures_keep_the_native_width() {
        assert!(matches!(
            parse_signature("cdecl:uint32(uint32)").unwrap(),
            Signature {
                result: Return::UInt32,
                parameters: Parameters::Fixed(parameters),
                stdcall: false,
            } if matches!(parameters.as_slice(), [Parameter::UInt32])
        ));
        assert!(matches!(
            parse_signature("cdecl:uintptr(uintptr)").unwrap(),
            Signature {
                result: Return::Pointer,
                parameters: Parameters::Fixed(parameters),
                stdcall: false,
            } if matches!(parameters.as_slice(), [Parameter::Pointer])
        ));
    }

    #[test]
    fn int32_calls_use_a_32_bit_native_signature() {
        extern "C" fn add(left: i32, right: i32) -> i32 {
            left + right
        }

        let signature = CString::new("cdecl:int32(int32,int32)").unwrap();
        let arguments = [
            LynxerFfiArg {
                tag: ARG_INT,
                i: 17,
                f: 0.0,
                s: null(),
                data: null(),
                data_length: 0,
                value: null(),
            },
            LynxerFfiArg {
                tag: ARG_INT,
                i: 25,
                f: 0.0,
                s: null(),
                data: null(),
                data_length: 0,
                value: null(),
            },
        ];
        let result = unsafe {
            invoke(
                add as *const () as *mut c_void,
                signature.as_ptr(),
                arguments.as_ptr(),
                arguments.len() as i64,
            )
        }
        .unwrap();
        assert_eq!(result.tag, RESULT_INT64);
        assert_eq!(result.i, 42);
    }

    #[test]
    fn uintptr_preserves_invalid_handle_sentinels() {
        extern "C" fn is_invalid_handle(handle: *mut c_void) -> i32 {
            (handle as isize == -1) as i32
        }

        let signature = CString::new("cdecl:int32(uintptr)").unwrap();
        let argument = LynxerFfiArg {
            tag: ARG_INT,
            i: -1,
            f: 0.0,
            s: null(),
            data: null(),
            data_length: 0,
            value: null(),
        };
        let result = unsafe {
            invoke(
                is_invalid_handle as *const () as *mut c_void,
                signature.as_ptr(),
                &argument,
                1,
            )
        }
        .unwrap();
        assert_eq!(result.i, 1);
    }

    #[test]
    fn utf16_parameters_and_returns_round_trip_unicode() {
        extern "C" fn utf16_length(text: *const u16) -> u32 {
            let mut length = 0usize;
            unsafe {
                while *text.add(length) != 0 {
                    length += 1;
                }
            }
            length as u32
        }

        static TEXT: [u16; 4] = [b'h' as u16, b'i' as u16, 0x20ac, 0];
        extern "C" fn get_utf16() -> *const u16 {
            TEXT.as_ptr()
        }

        let input = CString::new("A€").unwrap();
        let parameter_signature = CString::new("cdecl:uint32(utf16)").unwrap();
        let parameter = LynxerFfiArg {
            tag: ARG_STRING,
            i: 0,
            f: 0.0,
            s: input.as_ptr(),
            data: null(),
            data_length: 0,
            value: null(),
        };
        let parameter_result = unsafe {
            invoke(
                utf16_length as *const () as *mut c_void,
                parameter_signature.as_ptr(),
                &parameter,
                1,
            )
        }
        .unwrap();
        assert_eq!(parameter_result.i, 2);

        let return_signature = CString::new("cdecl:utf16()").unwrap();
        let return_result = unsafe {
            invoke(
                get_utf16 as *const () as *mut c_void,
                return_signature.as_ptr(),
                null(),
                0,
            )
        }
        .unwrap();
        assert_eq!(return_result.tag, RESULT_CSTRING);
        assert_eq!(
            unsafe { CStr::from_ptr(return_result.s) }.to_bytes(),
            "hi€".as_bytes()
        );
    }

    #[test]
    fn c_layout_struct_arguments_are_marshaled_by_value() {
        #[repr(C)]
        struct Pair {
            left: i32,
            right: u32,
        }
        extern "C" fn sum(pair: Pair) -> i64 {
            pair.left as i64 + pair.right as i64
        }

        let fields = [
            LynxerFfiValue {
                tag: 1,
                value: LynxerFfiValuePayload { i: 17 },
            },
            LynxerFfiValue {
                tag: 1,
                value: LynxerFfiValuePayload { i: 25 },
            },
        ];
        let aggregate = LynxerFfiValue {
            tag: 6,
            value: LynxerFfiValuePayload {
                array: LynxerFfiArray {
                    items: fields.as_ptr(),
                    count: fields.len() as i64,
                },
            },
        };
        let argument = LynxerFfiArg {
            tag: ARG_VALUE,
            i: 0,
            f: 0.0,
            s: null(),
            data: null(),
            data_length: 0,
            value: &aggregate,
        };
        let signature = CString::new("cdecl:int64(struct(int32,uint32))").unwrap();
        let result = unsafe {
            invoke(
                sum as *const () as *mut c_void,
                signature.as_ptr(),
                &argument,
                1,
            )
        }
        .unwrap();
        assert_eq!(result.i, 42);
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
