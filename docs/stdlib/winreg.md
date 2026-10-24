# winreg

Windows registry reader/writer.

**Backend:** native — `stdlib/winreg.so`, built from `stdlib/winreg.cpp`.
Windows-only; the module is not built or available on Linux or macOS.

**Import:** `import("winreg")` → `global.winreg.*`

Roots are named `HKCU`, `HKLM`, `HKCR`, `HKU` or `HKCC`; the long `HKEY_*`
spellings are accepted too. Writes create the key when it does not exist, so a
program does not need a separate `createKey` call first. `readString` returns
`""` and `readInt` returns `-1` when the value is missing or has an unexpected
type; use `valueExists` when an empty value is meaningful.

## Functions

| Function | Signature | Returns |
| --- | --- | --- |
| `keyExists` | `(str root, str path)` | `true` / `false` |
| `valueExists` | `(str root, str path, str name)` | `true` / `false` |
| `readString` | `(str root, str path, str name)` | the value, or `""` |
| `readInt` | `(str root, str path, str name)` | the value, or `-1` |
| `readDefault` | `(str root, str path)` | the default value, or `""` |
| `writeString` | `(str root, str path, str name, str value)` | `true` / `false` |
| `writeExpandString` | `(str root, str path, str name, str value)` | `true` / `false` |
| `writeInt` | `(str root, str path, str name, int value)` | `true` / `false` |
| `writeDefault` | `(str root, str path, str value)` | `true` / `false` |
| `createKey` | `(str root, str path)` | `true` / `false` |
| `deleteValue` | `(str root, str path, str name)` | `true` / `false` |
| `deleteKey` | `(str root, str path)` | `true` / `false` (empty key only) |
| `deleteTree` | `(str root, str path)` | `true` / `false` (recursive) |
| `listKeys` | `(str root, str path)` | subkey names, one per line |
| `listValues` | `(str root, str path)` | value names, one per line |

`readString` understands `REG_SZ`, `REG_EXPAND_SZ`, `REG_MULTI_SZ` (joined with
newlines), `REG_DWORD` and `REG_QWORD` (decimal text). `readInt` understands
`REG_DWORD` and `REG_QWORD`.

## Example

```lynx
global setup(){ import("winreg"); }

global main(){
    str key = "Software\\Lynxer\\Example";
    global.winreg.writeString("HKCU", key, "name", "Lynxer");
    global.winreg.writeInt("HKCU", key, "version", 1);
    println(global.winreg.readString("HKCU", key, "name"));
    println(global.winreg.readInt("HKCU", key, "version"));
    global.winreg.deleteTree("HKCU", key);
}
```
