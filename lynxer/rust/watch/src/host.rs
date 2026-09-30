//! Host API plumbing for the `watch` module.
//!
//! The interpreter calls `lynxer_module_attach_v1` after the module registers.
//! The only service `watch` needs is `blocking`, which runs a closure with the
//! interpreter lock released so `watchWait` can block on the watch descriptor
//! without wedging every other Lynxer thread.

use core::ffi::{c_int, c_void};
use std::sync::OnceLock;

use lynxer_abi::{LynxerHostApi, HOST_API_VERSION};

static HOST: OnceLock<LynxerHostApi> = OnceLock::new();

/// Stores the host API. Returns 0 on success, matching the attach contract.
///
/// # Safety
/// `host` must be a valid pointer to the interpreter's `LynxerHostApi`, valid
/// for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn lynxer_module_attach_v1(host: *const LynxerHostApi) -> c_int {
    if host.is_null() {
        return 1;
    }
    let host = *host;
    if host.version != HOST_API_VERSION {
        return 1;
    }
    let _ = HOST.set(host);
    0
}

/// Runs `body` with the interpreter lock released when a host is attached (so
/// other Lynxer threads can run while this thread blocks), or directly when it
/// is not (unit tests).
pub fn with_lock_released<F: FnOnce()>(body: F) {
    match HOST.get().and_then(|host| host.blocking) {
        Some(blocking) => {
            extern "C" fn trampoline<F: FnOnce()>(user: *mut c_void) {
                let mut boxed: Box<Option<F>> = unsafe { Box::from_raw(user as *mut Option<F>) };
                if let Some(body) = boxed.take() {
                    body();
                }
            }
            let boxed = Box::new(Some(body));
            let user = Box::into_raw(boxed) as *mut c_void;
            unsafe {
                blocking(core::ptr::null_mut(), trampoline::<F>, user);
            }
        }
        None => body(),
    }
}
