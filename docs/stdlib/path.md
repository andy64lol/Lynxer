# path

Path manipulation and filesystem helpers backed by Python's `pathlib.Path`.
Paths are represented as Lynxer strings, so they can be stored and passed
between functions without exposing Python objects.

```c
global setup(){
    import("path");
}
```

Functions return an empty string, empty list, `false`, or `-1` when the
underlying pathlib operation fails.

## Locations and construction

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `cwd` | `cwd()` | `str` | Current working directory |
| `home` | `home()` | `str` | Current user's home directory |
| `absolute` | `absolute(str path)` | `str` | Absolute path without requiring existence |
| `resolve` | `resolve(str path)` | `str` | Normalized absolute path; missing paths are allowed |
| `expandUser` | `expandUser(str path)` | `str` | Expand a leading `~` |
| `join` | `join(str base, str child)` | `str` | Join two path components |
| `join3` | `join3(str first, str second, str third)` | `str` | Join three path components |
| `normalize` | `normalize(str path)` | `str` | Normalize `.` and `..` without making the path absolute |

## Components and matching

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `name` | `name(str path)` | `str` | Final path component |
| `stem` | `stem(str path)` | `str` | Final component without its last suffix |
| `suffix` | `suffix(str path)` | `str` | Last suffix, including its dot |
| `suffixes` | `suffixes(str path)` | `list` | All suffixes |
| `parent` | `parent(str path)` | `str` | Parent path |
| `anchor` | `anchor(str path)` | `str` | Drive/root anchor |
| `root` | `root(str path)` | `str` | Root component |
| `drive` | `drive(str path)` | `str` | Drive component, mainly useful on Windows |
| `parts` | `parts(str path)` | `list` | Path components |
| `isAbsolute` | `isAbsolute(str path)` | `bool` | Whether the path is absolute |
| `match` | `match(str path, str pattern)` | `bool` | Match a pathlib glob pattern |
| `relativeTo` | `relativeTo(str path, str base)` | `str` | Path relative to `base`, or `""` if unrelated |
| `withName` | `withName(str path, str newName)` | `str` | Replace the final component |
| `withSuffix` | `withSuffix(str path, str newSuffix)` | `str` | Replace the final suffix |

## Filesystem queries

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `exists` | `exists(str path)` | `bool` | Whether the path exists |
| `isFile` | `isFile(str path)` | `bool` | Whether the path is a regular file |
| `isDir` | `isDir(str path)` | `bool` | Whether the path is a directory |
| `isSymlink` | `isSymlink(str path)` | `bool` | Whether the path is a symbolic link |
| `isMount` | `isMount(str path)` | `bool` | Whether the path is a mount point |
| `sameFile` | `sameFile(str first, str second)` | `bool` | Whether two existing paths refer to the same file |
| `size` | `size(str path)` | `int` | File size in bytes, or `-1` |
| `modifiedTime` | `modifiedTime(str path)` | `float` | Modification time as a Unix timestamp, or `-1.0` |

## Directory traversal

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `iterDir` | `iterDir(str path)` | `list` | Sorted direct children as strings |
| `glob` | `glob(str path, str pattern)` | `list` | Sorted direct matches |
| `rglob` | `rglob(str path, str pattern)` | `list` | Sorted recursive matches |

## Filesystem changes and text I/O

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `mkdir` | `mkdir(str path)` | `bool` | Create one directory |
| `mkdirs` | `mkdirs(str path)` | `bool` | Create a directory tree; existing directories are okay |
| `rmdir` | `rmdir(str path)` | `bool` | Remove an empty directory |
| `unlink` | `unlink(str path)` | `bool` | Remove a file or symlink |
| `unlinkMissingOk` | `unlinkMissingOk(str path)` | `bool` | Remove a file, accepting a missing path |
| `touch` | `touch(str path)` | `bool` | Create a file or update its timestamp |
| `rename` | `rename(str path, str target)` | `str` | Rename and return the target path |
| `replace` | `replace(str path, str target)` | `str` | Replace the target and return its path |
| `readText` | `readText(str path)` | `str` | Read UTF-8 text |
| `readTextEncoding` | `readTextEncoding(str path, str encoding)` | `str` | Read text with an explicit encoding |
| `writeText` | `writeText(str path, str content)` | `bool` | Overwrite with UTF-8 text |
| `writeTextEncoding` | `writeTextEncoding(str path, str content, str encoding)` | `bool` | Overwrite with an explicit encoding |
| `appendText` | `appendText(str path, str content)` | `bool` | Append UTF-8 text |
| `asUri` | `asUri(str path)` | `str` | Convert an absolute path to a `file://` URI |

## Example

```c
global setup(){
    import("path");
}

global main(){
    str root = global.path.cwd();
    str work = global.path.join(root, "path-demo");
    global.path.mkdirs(work);

    str file = global.path.join(work, "notes.txt");
    global.path.writeText(file, "hello\n");
    global.path.appendText(file, "world\n");

    println(global.path.name(file), " ", global.path.suffix(file));
    println(global.path.readText(file));
    println(global.path.glob(work, "*.txt"));

    global.path.unlink(file);
    global.path.rmdir(work);
}
```