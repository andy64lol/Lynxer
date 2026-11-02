# `winAPI-arm64` standard library

The `winAPI-arm64` module exposes the supported Windows API through native
code on Windows arm64. Import it under a valid Lynxer alias:

```lynx
fn setup() {
    importAs("winAPI-arm64", "windowsApi");
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
[arm64 guide](../winapi/windows-arm64.md). Typed `HANDLE`, `HWND` and `SOCKET`
helpers are available alongside the backward-compatible integer APIs.
