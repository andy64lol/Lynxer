use lynxer_abi::{lynxer_module, LynxerFfiValue};

const OPS: &[(&str, &str, &str)] = &[("typed", "typed_echo", "cdecl:v2:value(value)")];

lynxer_module!(OPS);

#[no_mangle]
pub unsafe extern "C" fn typed_echo(value: *const LynxerFfiValue) -> *const LynxerFfiValue {
    value
}

fn main() {}
