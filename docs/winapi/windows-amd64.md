# Windows amd64 WinAPI

Import the native-backed `winAPI-amd64` standard library under a valid alias
when Lynxer runs on Windows amd64:

```lynx
fn setup() {
    importAs("winAPI-amd64", "windowsApi");
}

fn main() {
    println(windowsApi.getProcessId());
    println(windowsApi.getCurrentDirectory());
}
```

The module rejects API calls unless the process is running on Windows amd64.
It does not dispatch to another operating system or architecture. The function
set and return conventions are shared with the
[Windows arm64 module](windows-arm64.md).

See [Windows API access](../windows-api.md) for the complete function catalog,
argument and return conventions, and examples. The `winAPI-amd64` module does
not provide arbitrary DLL calls; use [native-module FFI](../native-modules.md)
for APIs not listed there.
