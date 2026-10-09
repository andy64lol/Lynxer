#![allow(non_snake_case, non_camel_case_types, unused, improper_ctypes, clippy::all)]
//! `raylib` stdlib backend: raylib 6.0 bindings for Lynxer.
//!
//! The generated shims (one per raylib function, struct accessor and constant)
//! live in `generated_ops.rs`, produced by `codegen/generate.py`. This file only
//! provides the module ABI glue: `lynxer_module_init_v1` registers every op.
//! The Lynxer-facing surface is `lynxer/stdlib/raylib.lynx`.

use ::core::ffi::{c_char, c_int, c_long, c_uint, c_ulong, c_void};
use ::raylib_sys as rl;
use ::std::ffi::CString;

include!("generated_ops.rs");

type RegisterFunction =
    unsafe extern "C" fn(*const c_char, *const c_char, *const c_char) -> c_int;
type RegisterConstant = unsafe extern "C" fn(*const c_char, i64) -> c_int;
type RegisterType = unsafe extern "C" fn(*const c_char, *const c_char) -> c_int;

fn cstr(text: &str) -> CString {
    CString::new(text).unwrap_or_else(|_| CString::new("").unwrap())
}

/// Lynxer native-module entry point: registers every op and constant.
#[no_mangle]
pub unsafe extern "C" fn lynxer_module_init_v1(
    register_function: RegisterFunction,
    register_constant: RegisterConstant,
    register_type: RegisterType,
) -> c_int {
    for (name, symbol, signature) in OPS.iter() {
        let n = cstr(name);
        let s = cstr(symbol);
        let g = cstr(signature);
        if register_function(n.as_ptr(), s.as_ptr(), g.as_ptr()) == 0 {
            eprintln!("lynxer_raylib: registration rejected: {name} -> {symbol} {signature}");
            return 1;
        }
    }
    for (name, value) in CONSTANTS.iter() {
        let n = cstr(name);
        if register_constant(n.as_ptr(), *value) == 0 {
            eprintln!("lynxer_raylib: constant rejected: {name} = {value}");
            return 1;
        }
    }
    let _ = register_type;
    0
}
