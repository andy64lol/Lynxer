# `winAPI-amd64` standard library

The `winAPI-amd64` module exposes the supported Windows API through native
code on Windows amd64. Import it under a valid Lynxer alias:

```lynx
fn setup() {
    importAs("winAPI-amd64", "windowsApi");
}

fn main() {
    println(windowsApi.getProcessId());
    println(windowsApi.getCurrentDirectory());
}
```

The module checks the host OS and architecture before every API operation; it
does not emulate or cross-dispatch Win32 calls. Its functions are backed by
the native Win32 implementation rather than Lynxer built-ins. See the
[Windows API catalog](../windows-api.md) and
[amd64 guide](../winapi/windows-amd64.md).
