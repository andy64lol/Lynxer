//! Host API plumbing.
//!
//! The interpreter calls `lynxer_module_attach_v1` after the module registers,
//! handing over callbacks it can use to invoke Lynxer functions by name (the
//! frame callbacks) and to read the SIGINT flag. The struct is stored for the
//! process lifetime so a `&'static` reference can be captured by the macroquad
//! frame loop.

use core::ffi::c_int;
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
    if host.version != HOST_API_VERSION || host.invoke.is_none() {
        return 1;
    }
    let _ = HOST.set(host);
    0
}

pub fn host() -> Option<&'static LynxerHostApi> {
    HOST.get()
}
