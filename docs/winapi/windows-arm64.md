# Windows arm64 WinAPI

Import the native-backed `winAPI-arm64` standard library under a valid alias
when Lynxer runs on Windows arm64:

```lynx
fn setup() {
    importAs("winAPI-arm64", "windowsApi");
}

fn main() {
    println(windowsApi.getProcessId());
    println(windowsApi.getCurrentDirectory());
}
```

The module rejects API calls unless the process is running on Windows arm64.
It does not dispatch to another operating system or architecture. The function
set and return conventions are shared with the
[Windows amd64 module](windows-amd64.md).

See [Windows API access](../windows-api.md) for the complete function catalog,
argument and return conventions, and examples. The `winAPI-arm64` module does
not provide arbitrary DLL calls; use [native-module FFI](../native-modules.md)
for APIs not listed there.
