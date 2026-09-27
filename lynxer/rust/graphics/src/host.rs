//! The interpreter's host API, handed over once at load time.

use core::ffi::c_int;
use std::sync::OnceLock;

use lynxer_abi::{LynxerHostApi, HOST_API_VERSION};

static HOST: OnceLock<LynxerHostApi> = OnceLock::new();

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
