//! Shared C-ABI plumbing for Lynxer's Rust stdlib modules.
//!
//! Every module is a `cdylib` that exports `lynxer_module_init_v1` and one
//! `#[no_mangle] extern "C"` function per op. Ops use the packed signature
//! `cdecl:<ret>(...)`, so the interpreter calls them as
//!
//! ```c
//! <ret> op(const double* nums, int64_t num_count,
//!          const char* const* strs, int64_t str_count);
//! ```
//!
//! This crate provides the argument view, the panic guards (a Rust panic must
//! not unwind across the C ABI and abort the interpreter), the single
//! thread-local string result buffer, and the module registration helper.
//! Fixed native signatures can also use the versioned `LynxerFfiValue` tree
//! (`cdecl:v2:value(value)`) for common recursively typed values.
//!
//! See `docs/native-module-abi.md`.

use core::ffi::{c_char, c_int, c_void};
use std::cell::RefCell;
use std::ffi::{CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};

/// Host services supplied by the interpreter through `lynxer_module_attach_v1`.
///
/// Layout matches `LynxerHostApi` in `lynxer/stdlib/lynxer_native_abi.h`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct LynxerHostApi {
    pub version: c_int,
    pub context: *mut c_void,
    pub invoke: Option<unsafe extern "C" fn(*mut c_void, *const c_char, c_int, f64) -> c_int>,
    pub interrupted: Option<unsafe extern "C" fn(*mut c_void) -> c_int>,
    pub program_args: Option<unsafe extern "C" fn(*mut c_void) -> *const c_char>,
    /// Runs `body(user)` with the interpreter lock released, then re-acquires
    /// it. Used by blocking module ops so they do not wedge other threads.
    pub blocking: Option<
        unsafe extern "C" fn(*mut c_void, unsafe extern "C" fn(*mut c_void), *mut c_void) -> c_int,
    >,
    pub request_exit: Option<unsafe extern "C" fn(*mut c_void, i64) -> c_int>,
}

/// Extended host services introduced by `lynxer_module_attach_v2`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct LynxerHostApiV2 {
    pub version: c_int,
    pub context: *mut c_void,
    pub invoke: Option<unsafe extern "C" fn(*mut c_void, *const c_char, c_int, f64) -> c_int>,
    pub interrupted: Option<unsafe extern "C" fn(*mut c_void) -> c_int>,
    pub program_args: Option<unsafe extern "C" fn(*mut c_void) -> *const c_char>,
    pub blocking: Option<
        unsafe extern "C" fn(*mut c_void, unsafe extern "C" fn(*mut c_void), *mut c_void) -> c_int,
    >,
    pub request_exit: Option<unsafe extern "C" fn(*mut c_void, i64) -> c_int>,
    pub invoke_threadsafe:
        Option<unsafe extern "C" fn(*mut c_void, *const c_char, c_int, f64) -> c_int>,
}

/// Recursively typed value used by the additive `cdecl:v2:value(value)` call
/// signature. All pointers are borrowed for the duration of the call.
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
pub struct LynxerFfiRecord {
    pub fields: *const LynxerFfiValueField,
    pub count: i64,
    pub type_name: *const c_char,
    pub type_name_length: i64,
    pub display_name: *const c_char,
    pub display_name_length: i64,
    pub kind: u32,
}

pub const VALUE_NULL: u32 = 0;
pub const VALUE_INT64: u32 = 1;
pub const VALUE_FLOAT64: u32 = 2;
pub const VALUE_BOOL: u32 = 3;
pub const VALUE_STRING: u32 = 4;
pub const VALUE_BYTES: u32 = 5;
pub const VALUE_ARRAY: u32 = 6;
pub const VALUE_RECORD: u32 = 7;
pub const VALUE_TUPLE: u32 = 8;
pub const VALUE_UINT64: u32 = 9;
pub const VALUE_CHAR: u32 = 10;
pub const VALUE_ENUM: u32 = 11;

// The API copy is shared with native modules. They must use
// `invoke_threadsafe` from a worker thread; `invoke` is only safe while already
// on the interpreter thread.
unsafe impl Send for LynxerHostApi {}
unsafe impl Sync for LynxerHostApi {}
unsafe impl Send for LynxerHostApiV2 {}
unsafe impl Sync for LynxerHostApiV2 {}

/// The only host API version the interpreter currently offers.
pub const HOST_API_VERSION: c_int = 1;
pub const HOST_API_VERSION_V2: c_int = 2;

/// Runs the Lynxer function `name` with no argument or one numeric argument.
/// Returns 0 on success and non-zero when the callback failed.
pub fn invoke(host: &LynxerHostApi, name: &str, argument: Option<f64>) -> c_int {
    let callback = match host.invoke {
        Some(callback) => callback,
        None => return 1,
    };
    let name = match CString::new(name) {
        Ok(name) => name,
        Err(_) => return 1,
    };
    let (has_argument, value) = match argument {
        Some(value) => (1, value),
        None => (0, 0.0),
    };
    unsafe { callback(host.context, name.as_ptr(), has_argument, value) }
}

/// Invokes a Lynxer function while acquiring the interpreter lock. This is
/// for native worker threads; callbacks already on the interpreter thread
/// should use [`invoke`] to avoid changing its lock state.
pub fn invoke_threadsafe(host: &LynxerHostApiV2, name: &str, argument: Option<f64>) -> c_int {
    let callback = match host.invoke_threadsafe {
        Some(callback) => callback,
        None => return 1,
    };
    let name = match CString::new(name) {
        Ok(name) => name,
        Err(_) => return 1,
    };
    let (has_argument, value) = match argument {
        Some(value) => (1, value),
        None => (0, 0.0),
    };
    unsafe { callback(host.context, name.as_ptr(), has_argument, value) }
}

/// True once the process has received SIGINT.
pub fn interrupted(host: &LynxerHostApi) -> bool {
    match host.interrupted {
        Some(callback) => unsafe { callback(host.context) != 0 },
        None => false,
    }
}

/// Signature of the interpreter's `nativeRegisterFunction` callback.
pub type RegisterFunction =
    unsafe extern "C" fn(*const c_char, *const c_char, *const c_char) -> i32;
/// Signature of the interpreter's `nativeRegisterConstant` callback.
pub type RegisterConstant = unsafe extern "C" fn(*const c_char, i64) -> i32;
/// Signature of the interpreter's `nativeRegisterType` callback.
pub type RegisterType = unsafe extern "C" fn(*const c_char, *const c_char) -> i32;

/// Borrowed view of the arguments the interpreter packed into a native call.
pub struct Args<'a> {
    numbers: &'a [f64],
    strings: Vec<&'a str>,
    buffers: Vec<&'a [u8]>,
}

impl<'a> Args<'a> {
    pub fn num(&self, index: usize) -> f64 {
        self.numbers.get(index).copied().unwrap_or(0.0)
    }

    pub fn int(&self, index: usize) -> i64 {
        let value = self.num(index);
        if value.is_finite() {
            value as i64
        } else {
            0
        }
    }

    pub fn float(&self, index: usize) -> f32 {
        self.num(index) as f32
    }

    pub fn bool(&self, index: usize) -> bool {
        self.num(index) != 0.0
    }

    pub fn string(&self, index: usize) -> &'a str {
        self.strings.get(index).copied().unwrap_or("")
    }

    pub fn string_count(&self) -> usize {
        self.strings.len()
    }

    /// The *i*-th byte buffer, from the buffered packed form
    /// (`cdecl:<ret>(...,bytes)`). Empty when absent.
    pub fn bytes(&self, index: usize) -> &'a [u8] {
        self.buffers.get(index).copied().unwrap_or(&[])
    }

    pub fn buffer_count(&self) -> usize {
        self.buffers.len()
    }
}

/// Rebuilds the argument view from the raw pointers the interpreter passes.
///
/// # Safety
/// `nums`/`strs` must point to `nnums`/`nstrs` live elements for the duration
/// of the call, and each pointer in `strs` must be a valid C string.
pub unsafe fn view<'a>(
    nums: *const f64,
    nnums: i64,
    strs: *const *const c_char,
    nstrs: i64,
) -> Args<'a> {
    let numbers: &'a [f64] = if nums.is_null() || nnums <= 0 {
        &[]
    } else {
        std::slice::from_raw_parts(nums, nnums as usize)
    };

    let mut strings: Vec<&'a str> = Vec::new();
    if !strs.is_null() && nstrs > 0 {
        let raw = std::slice::from_raw_parts(strs, nstrs as usize);
        strings.reserve(raw.len());
        for pointer in raw {
            if pointer.is_null() {
                strings.push("");
            } else {
                strings.push(CStr::from_ptr(*pointer).to_str().unwrap_or(""));
            }
        }
    }

    Args {
        numbers,
        strings,
        buffers: Vec::new(),
    }
}

/// Rebuilds the argument view for the buffered packed form
/// `cdecl:<ret>(...,bytes)`, which also carries byte buffers.
///
/// # Safety
/// As [`view`]; additionally `bufs`/`lens` must point to `nbufs` live entries,
/// and `lens[i]` must be the length in bytes of the buffer at `bufs[i]`.
pub unsafe fn view_buffers<'a>(
    nums: *const f64,
    nnums: i64,
    strs: *const *const c_char,
    nstrs: i64,
    bufs: *const *const u8,
    lens: *const i64,
    nbufs: i64,
) -> Args<'a> {
    let mut args = view(nums, nnums, strs, nstrs);
    if !bufs.is_null() && !lens.is_null() && nbufs > 0 {
        let pointers = std::slice::from_raw_parts(bufs, nbufs as usize);
        let lengths = std::slice::from_raw_parts(lens, nbufs as usize);
        args.buffers.reserve(pointers.len());
        for index in 0..pointers.len() {
            let length = lengths[index].max(0) as usize;
            if pointers[index].is_null() || length == 0 {
                args.buffers.push(&[]);
            } else {
                args.buffers
                    .push(std::slice::from_raw_parts(pointers[index], length));
            }
        }
    }
    args
}

thread_local! {
    static RESULT_STRING: RefCell<CString> = RefCell::new(CString::default());
    static RESULT_BYTES: RefCell<Vec<u8>> = RefCell::new(Vec::new());
}

/// Stores the one live string result. The pointer stays valid until the next
/// string-returning call, which is exactly the interpreter's copy window.
pub fn store_result_string(value: String) -> *const c_char {
    RESULT_STRING.with(|cell| {
        let mut buffer = cell.borrow_mut();
        *buffer = CString::new(value.replace('\0', " ")).unwrap_or_default();
        buffer.as_ptr()
    })
}

pub fn guard_int<F: FnOnce() -> i64>(body: F) -> i64 {
    catch_unwind(AssertUnwindSafe(body)).unwrap_or(-1)
}

pub fn guard_float<F: FnOnce() -> f64>(body: F) -> f64 {
    catch_unwind(AssertUnwindSafe(body)).unwrap_or(-1.0)
}

/// Stores the one live `bytes` result as `[i64 little-endian length][payload]`
/// and returns a pointer to it. Valid until the next bytes-returning call,
/// which is exactly the interpreter's copy window.
pub fn store_result_bytes(value: Vec<u8>) -> *const u8 {
    RESULT_BYTES.with(|cell| {
        let mut buffer = cell.borrow_mut();
        let length = value.len() as i64;
        buffer.clear();
        buffer.extend_from_slice(&length.to_le_bytes());
        buffer.extend_from_slice(&value);
        buffer.as_ptr()
    })
}

pub fn guard_bytes<F: FnOnce() -> Vec<u8>>(body: F) -> *const u8 {
    match catch_unwind(AssertUnwindSafe(body)) {
        Ok(value) => store_result_bytes(value),
        Err(_) => std::ptr::null(),
    }
}

pub fn guard_string<F: FnOnce() -> String>(body: F) -> *const c_char {
    match catch_unwind(AssertUnwindSafe(body)) {
        Ok(value) => store_result_string(value),
        Err(_) => std::ptr::null(),
    }
}

/// Registers every `(name, symbol, signature)` entry through the interpreter's
/// callback. Returns 0 on success and 1 on the first failure, matching
/// `lynxer_module_init_v1`'s contract.
///
/// # Safety
/// `register_function` must be the callback the interpreter passed to
/// `lynxer_module_init_v1`.
pub unsafe fn register_all(
    table: &[(&str, &str, &str)],
    register_function: RegisterFunction,
) -> i32 {
    for (name, symbol, signature) in table {
        let name = CString::new(*name).unwrap_or_default();
        let symbol = CString::new(*symbol).unwrap_or_default();
        let signature = CString::new(*signature).unwrap_or_default();
        if register_function(name.as_ptr(), symbol.as_ptr(), signature.as_ptr()) == 0 {
            return 1;
        }
    }
    0
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::mem::{align_of, size_of};

    #[test]
    fn typed_value_abi_layout_is_stable_on_64_bit_targets() {
        if cfg!(target_pointer_width = "64") {
            assert_eq!(size_of::<LynxerHostApi>(), 56);
            assert_eq!(size_of::<LynxerHostApiV2>(), 64);
            assert_eq!(size_of::<LynxerFfiValue>(), 64);
            assert_eq!(align_of::<LynxerFfiValue>(), 8);
            assert_eq!(size_of::<LynxerFfiValueField>(), 48);
            assert_eq!(size_of::<LynxerFfiArray>(), 16);
            assert_eq!(size_of::<LynxerFfiData>(), 16);
            assert_eq!(size_of::<LynxerFfiRecord>(), 56);
            assert_eq!(size_of::<LynxerFfiEnum>(), 48);
        }
    }
}

/// Declares a panic-guarded packed op returning an integer.
#[macro_export]
macro_rules! export_int {
    ($name:ident, $args:ident, $body:block) => {
        #[no_mangle]
        pub unsafe extern "C" fn $name(
            nums: *const f64,
            nnums: i64,
            strs: *const *const core::ffi::c_char,
            nstrs: i64,
        ) -> i64 {
            $crate::guard_int(|| {
                let $args = $crate::view(nums, nnums, strs, nstrs);
                $body
            })
        }
    };
}

/// Declares a panic-guarded packed op returning a float.
#[macro_export]
macro_rules! export_float {
    ($name:ident, $args:ident, $body:block) => {
        #[no_mangle]
        pub unsafe extern "C" fn $name(
            nums: *const f64,
            nnums: i64,
            strs: *const *const core::ffi::c_char,
            nstrs: i64,
        ) -> f64 {
            $crate::guard_float(|| {
                let $args = $crate::view(nums, nnums, strs, nstrs);
                $body
            })
        }
    };
}

/// Declares a panic-guarded packed op returning a string.
#[macro_export]
macro_rules! export_string {
    ($name:ident, $args:ident, $body:block) => {
        #[no_mangle]
        pub unsafe extern "C" fn $name(
            nums: *const f64,
            nnums: i64,
            strs: *const *const core::ffi::c_char,
            nstrs: i64,
        ) -> *const core::ffi::c_char {
            $crate::guard_string(|| {
                let $args = $crate::view(nums, nnums, strs, nstrs);
                $body
            })
        }
    };
}

/// Declares a panic-guarded buffered packed op (`cdecl:<ret>(...,bytes)`)
/// returning an integer.
#[macro_export]
macro_rules! export_int_buffers {
    ($name:ident, $args:ident, $body:block) => {
        #[no_mangle]
        pub unsafe extern "C" fn $name(
            nums: *const f64,
            nnums: i64,
            strs: *const *const core::ffi::c_char,
            nstrs: i64,
            bufs: *const *const u8,
            lens: *const i64,
            nbufs: i64,
        ) -> i64 {
            $crate::guard_int(|| {
                let $args = $crate::view_buffers(nums, nnums, strs, nstrs, bufs, lens, nbufs);
                $body
            })
        }
    };
}

/// Declares a panic-guarded buffered packed op returning a float.
#[macro_export]
macro_rules! export_float_buffers {
    ($name:ident, $args:ident, $body:block) => {
        #[no_mangle]
        pub unsafe extern "C" fn $name(
            nums: *const f64,
            nnums: i64,
            strs: *const *const core::ffi::c_char,
            nstrs: i64,
            bufs: *const *const u8,
            lens: *const i64,
            nbufs: i64,
        ) -> f64 {
            $crate::guard_float(|| {
                let $args = $crate::view_buffers(nums, nnums, strs, nstrs, bufs, lens, nbufs);
                $body
            })
        }
    };
}

/// Declares a panic-guarded buffered packed op returning a string.
#[macro_export]
macro_rules! export_string_buffers {
    ($name:ident, $args:ident, $body:block) => {
        #[no_mangle]
        pub unsafe extern "C" fn $name(
            nums: *const f64,
            nnums: i64,
            strs: *const *const core::ffi::c_char,
            nstrs: i64,
            bufs: *const *const u8,
            lens: *const i64,
            nbufs: i64,
        ) -> *const core::ffi::c_char {
            $crate::guard_string(|| {
                let $args = $crate::view_buffers(nums, nnums, strs, nstrs, bufs, lens, nbufs);
                $body
            })
        }
    };
}

/// Declares a panic-guarded buffered packed op returning a `bytes` buffer.
#[macro_export]
macro_rules! export_bytes_buffers {
    ($name:ident, $args:ident, $body:block) => {
        #[no_mangle]
        pub unsafe extern "C" fn $name(
            nums: *const f64,
            nnums: i64,
            strs: *const *const core::ffi::c_char,
            nstrs: i64,
            bufs: *const *const u8,
            lens: *const i64,
            nbufs: i64,
        ) -> *const u8 {
            $crate::guard_bytes(|| {
                let $args = $crate::view_buffers(nums, nnums, strs, nstrs, bufs, lens, nbufs);
                $body
            })
        }
    };
}

/// Emits the module's `lynxer_module_init_v1` entry point from an op table.
#[macro_export]
macro_rules! lynxer_module {
    ($table:expr) => {
        #[no_mangle]
        pub unsafe extern "C" fn lynxer_module_init_v1(
            register_function: $crate::RegisterFunction,
            _register_constant: $crate::RegisterConstant,
            _register_type: $crate::RegisterType,
        ) -> core::ffi::c_int {
            $crate::register_all($table, register_function)
        }
    };
}
