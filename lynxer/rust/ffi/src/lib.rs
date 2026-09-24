// This module is intentionally left empty because the `ffi*` built-ins are implemented in C++.
// The Rust module is not needed for functionality, but is kept for consistency with other Rust modules.

#[no_mangle]
pub extern "C" fn lynxer_module_init_v1(
    _register_function: lynxer_abi::RegisterFunction,
    _register_constant: lynxer_abi::RegisterConstant,
    _register_type: lynxer_abi::RegisterType,
) -> i32 {
    // No-op: The `ffi*` built-ins are implemented in C++.
    0
}
