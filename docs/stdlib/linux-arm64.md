# `linux-arm64` standard library

The `linux-arm64` module exposes raw Linux syscall wrappers for Linux arm64.
The module name selects the target; no runtime `select()` call is required.

```lynx
fn setup() {
    importAs("linux-arm64", "linuxArm64");
}

fn main() {
    println(linuxArm64.syscallGetProcessId());
}
```

The module rejects calls unless both the operating system and process
architecture match. Arguments are raw integer syscall arguments, including
native addresses for pointers. Calls follow the Linux syscall ABI (up to six
arguments); failures raise catchable Lynxer errors. See the
[arm64 syscall reference](../syscalls/linux-arm64.md) and
[shared catalog](../syscalls.md).
