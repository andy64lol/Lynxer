# path

`pathlib`-style path manipulation and filesystem helpers.

**Backend:** native — `stdlib/path.so`, built from `stdlib/path.cpp` over
`<filesystem>` and POSIX `stat`. **Import:** `import("path")` → `global.path.*`

Paths are plain strings. Functions return `""`, an empty list, `false` or `-1`
when the underlying operation fails.

## Construction

| Function | Signature | Notes |
| --- | --- | --- |
| `cwd` / `home` | `() -> str` | Working directory / home directory |
| `absolute` | `(str path) -> str` | Absolute path without normalization or symlink resolution |
| `resolve` | `(str path) -> str` | Canonical path; missing components are allowed |
| `expandUser` | `(str path) -> str` | Expands a leading `~` |
| `join` | `(str base, str child) -> str` | Joins two components |
| `join3` | `(str a, str b, str c) -> str` | Joins three components |
| `normalize` | `(str path) -> str` | Collapses `.` and `..`; `""` becomes `"."` |

## Components

| Function | Signature | Notes |
| --- | --- | --- |
| `name` | `(str path) -> str` | Final component |
| `stem` | `(str path) -> str` | Final component without its suffix |
| `suffix` | `(str path) -> str` | Final suffix, including the dot |
| `suffixes` | `(str path) -> list` | All suffixes, e.g. `[.tar, .gz]` |
| `parent` | `(str path) -> str` | Parent directory |
| `anchor` / `root` | `(str path) -> str` | `/` for absolute paths, else `""` |
| `drive` | `(str path) -> str` | Always `""` on POSIX |
| `parts` | `(str path) -> list` | Components from the root |
| `isAbsolute` | `(str path) -> bool` | Absolute-path test |
| `match` | `(str path, str pattern) -> bool` | `pathlib`-style glob match |
| `relativeTo` | `(str path, str base) -> str` | Path relative to `base`, or `""` |
| `withName` | `(str path, str newName) -> str` | Replaces the final component |
| `withSuffix` | `(str path, str newSuffix) -> str` | Replaces the final suffix; `""` removes it |

## Predicates

`exists`, `isFile`, `isDir`, `isSymlink`, `isMount` — `(str path) -> bool`.
`sameFile(str first, str second) -> bool` compares device and inode.

## Traversal

| Function | Signature | Notes |
| --- | --- | --- |
| `iterDir` | `(str path) -> list` | Direct children as full paths, sorted |
| `glob` | `(str path, str pattern) -> list` | Children matching a glob pattern, sorted |
| `rglob` | `(str path, str pattern) -> list` | Recursive glob, sorted |

Glob patterns support `*`, `?`, `[...]` and `**` (which spans directories).
`*` and `?` do not cross `/`.

## Mutation

| Function | Signature | Notes |
| --- | --- | --- |
| `mkdir` | `(str path) -> bool` | One directory; `false` if it exists |
| `mkdirs` | `(str path) -> bool` | Directory tree (existing is fine) |
| `rmdir` / `unlink` | `(str path) -> bool` | Removes an empty directory / a file |
| `unlinkMissingOk` | `(str path) -> bool` | As `unlink`, treating a missing path as success |
| `touch` | `(str path) -> bool` | Creates the file and refreshes its timestamp |
| `rename` / `replace` | `(str path, str target) -> str` | Returns the resulting target path, or `""` |

## Text and metadata

| Function | Signature | Notes |
| --- | --- | --- |
| `readText` | `(str path) -> str` | UTF-8 contents, or `""` |
| `readTextEncoding` | `(str path, str encoding) -> str` | Encoding is accepted but ignored; always UTF-8 |
| `writeText` | `(str path, str content) -> bool` | UTF-8 write |
| `writeTextEncoding` | `(str path, str content, str encoding) -> bool` | Encoding ignored |
| `appendText` | `(str path, str content) -> bool` | UTF-8 append |
| `size` | `(str path) -> int` | Bytes, or `-1` |
| `modifiedTime` | `(str path) -> float` | Unix timestamp with sub-second precision, or `-1.0` |
| `asUri` | `(str path) -> str` | `file://` URI of the absolute path |

## Example

```lynx
global setup(){ import("path"); }

global main(){
    println(global.path.name("/tmp/notes.txt"));
    println(global.path.suffixes("archive.tar.gz"));
    println(global.path.rglob(".", "*.lynx"));
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [parity.md](../parity.md) — the parity scope with Python Lynxer.
- [limitations.md](../limitations.md) — the full divergence register.
