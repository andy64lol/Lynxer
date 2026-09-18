//! Packed-argument FFI plumbing shared by every exported op.
//!
//! The interpreter calls a `cdecl:<ret>(...)` native function with every
//! argument packed into `(nums, nnums, strs, nstrs)`. These helpers rebuild a
//! typed view, catch panics so a bug cannot abort the interpreter, and own the
//! one live string result.

use core::ffi::c_char;
use std::cell::RefCell;
use std::ffi::{CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};

/// Borrowed view of the arguments packed by the interpreter.
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

    pub fn string(&self, index: usize) -> &'a str {
        self.strings.get(index).copied().unwrap_or("")
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
fn store_result_string(value: String) -> *const c_char {
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

/// Declares a panic-guarded packed op returning an integer.
#[macro_export]
macro_rules! game_export_int {
    ($name:ident, $args:ident, $body:block) => {
        #[no_mangle]
        pub unsafe extern "C" fn $name(
            nums: *const f64,
            nnums: i64,
            strs: *const *const core::ffi::c_char,
            nstrs: i64,
        ) -> i64 {
            $crate::ffi::guard_int(|| {
                let $args = $crate::ffi::view(nums, nnums, strs, nstrs);
                $body
            })
        }
    };
}

/// Declares a panic-guarded packed op returning a float.
#[macro_export]
macro_rules! game_export_float {
    ($name:ident, $args:ident, $body:block) => {
        #[no_mangle]
        pub unsafe extern "C" fn $name(
            nums: *const f64,
            nnums: i64,
            strs: *const *const core::ffi::c_char,
            nstrs: i64,
        ) -> f64 {
            $crate::ffi::guard_float(|| {
                let $args = $crate::ffi::view(nums, nnums, strs, nstrs);
                $body
            })
        }
    };
}

/// Declares a panic-guarded packed op returning a string.
#[macro_export]
macro_rules! game_export_string {
    ($name:ident, $args:ident, $body:block) => {
        #[no_mangle]
        pub unsafe extern "C" fn $name(
            nums: *const f64,
            nnums: i64,
            strs: *const *const core::ffi::c_char,
            nstrs: i64,
        ) -> *const core::ffi::c_char {
            $crate::ffi::guard_string(|| {
                let $args = $crate::ffi::view(nums, nnums, strs, nstrs);
                $body
            })
        }
    };
}
