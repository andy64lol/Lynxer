//! Shared C-ABI plumbing for Clynxer's Rust stdlib modules.
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
//!
//! See `clynxer/docs/native-module-abi.md`.

use core::ffi::c_char;
use std::cell::RefCell;
use std::ffi::{CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};

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

    Args { numbers, strings }
}

thread_local! {
    static RESULT_STRING: RefCell<CString> = RefCell::new(CString::default());
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
