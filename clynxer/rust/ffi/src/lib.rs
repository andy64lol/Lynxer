use lynxer_abi::{Args, Result};

#[no_mangle]
pub extern "C" fn lynxer_module_init_v1(
    register_function: extern "C" fn(&str, &str, &str) -> i32,
    _register_constant: extern "C" fn(&str, i64) -> i32,
    _register_type: extern "C" fn(&str, &str) -> i32,
) {
    register_function("ffiLoadLibrary", "cdecl:int64(cstring)", "ffiLoadLibrary");
    register_function("ffiLookup", "cdecl:int64(int64, cstring)", "ffiLookup");
    register_function("ffiCall", "cdecl:int64(int64, ...)", "ffiCall");
    register_function("ffiCloseLibrary", "cdecl:int64(int64)", "ffiCloseLibrary");
    register_function("ffiCallback", "cdecl:int64(...)", "ffiCallback");
    register_function("ffiFreeCallback", "cdecl:int64(int64)", "ffiFreeCallback");
}

#[no_mangle]
pub extern "C" fn ffiLoadLibrary(args: Args) -> Result<i64> {
    let path = args.string(0)?;
    unsafe {
        let handle = libc::dlopen(path.as_ptr() as *const _, libc::RTLD_NOW | libc::RTLD_LOCAL);
        if handle.is_null() {
            return Err("ffiLoadLibrary: failed to load library".into());
        }
        Ok(handle as i64)
    }
}

#[no_mangle]
pub extern "C" fn ffiLookup(args: Args) -> Result<i64> {
    let lib_handle = args.int(0)?;
    let name = args.string(1)?;
    unsafe {
        let handle = libc::dlopen(lib_handle as *const _, libc::RTLD_LAZY);
        if handle.is_null() {
            return Err("ffiLookup: invalid library handle".into());
        }
        let symbol = libc::dlsym(handle, name.as_ptr() as *const _);
        if symbol.is_null() {
            return Err("ffiLookup: failed to find symbol".into());
        }
        Ok(symbol as i64)
    }
}

#[no_mangle]
pub extern "C" fn ffiCall(args: Args) -> Result<i64> {
    let func_ptr = args.int(0)?;
    // Simplified: Assume the function is `int(int, int)` for demonstration.
    // In a real implementation, parse the signature dynamically.
    unsafe {
        let func: extern "C" fn(i32, i32) -> i32 = std::mem::transmute(func_ptr);
        let arg1 = args.int(1)?;
        let arg2 = args.int(2)?;
        Ok(func(arg1 as i32, arg2 as i32) as i64)
    }
}

#[no_mangle]
pub extern "C" fn ffiCloseLibrary(args: Args) -> Result<i64> {
    let lib_handle = args.int(0)?;
    unsafe {
        if libc::dlclose(lib_handle as *mut _) != 0 {
            return Err("ffiCloseLibrary: failed to close library".into());
        }
    }
    Ok(0)
}

#[no_mangle]
pub extern "C" fn ffiCallback(_args: Args) -> Result<i64> {
    Err("ffiCallback: not implemented".into())
}

#[no_mangle]
pub extern "C" fn ffiFreeCallback(_args: Args) -> Result<i64> {
    Err("ffiFreeCallback: not implemented".into())
}
