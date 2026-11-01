# `linux-amd64` standard library

The `linux-amd64` module exposes raw Linux syscall wrappers for Linux amd64.
The module name selects the target; no runtime `select()` call is required.

```lynx
fn setup() {
    importAs("linux-amd64", "linuxAmd64");
}

fn main() {
    println(linuxAmd64.syscallGetProcessId());
}
```

The module rejects calls unless both the operating system and process
architecture match. Arguments are raw integer syscall arguments, including
native addresses for pointers. Calls follow the Linux syscall ABI (up to six
arguments); failures raise catchable Lynxer errors. See the
[amd64 syscall reference](../syscalls/linux-amd64.md) and
[shared catalog](../syscalls.md).
