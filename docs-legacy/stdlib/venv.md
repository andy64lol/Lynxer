# `venv` — Virtual Environment Management

Import name: **`venv`**

```lynx
global setup(){
    import("venv");
}
```

The `venv` module provides a cross-platform interface for creating, inspecting,
and removing Python virtual environments, and for installing/uninstalling packages
inside them.  It delegates to Python's built-in `venv` module and `pip`.

---

## Sub-namespaces

| Namespace | Description |
|-----------|-------------|
| `global.venv.venv` | Virtual environment lifecycle |
| `global.venv.package` | Package management inside a venv |

---

## `global.venv.venv`

### `venv.create(str name, str path) → str`

Create a new virtual environment.

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `str` | Directory name for the new venv |
| `path` | `str` | Parent directory. Pass `""` to use the current working directory |

**Returns** `"ok:<absolute-path>"` on success, `"error:<message>"` on failure.

```lynx
str result = global.venv.venv.create("my-env", "");
// → "ok:/home/user/project/my-env"

str result = global.venv.venv.create("my-env", "/opt/envs");
// → "ok:/opt/envs/my-env"
```

---

### `venv.init(str venv_path) → str`

Verify that a virtual environment exists at the given path.  This does **not**
activate the environment; it confirms that the directory is a valid venv by
checking for `pyvenv.cfg`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `venv_path` | `str` | Absolute or relative path to the venv |

**Returns** `"ok:<absolute-path>"` if the venv is valid, `"error:<message>"` otherwise.

```lynx
str result = global.venv.venv.init("my-env");
// → "ok:/home/user/project/my-env"
```

---

### `venv.listVenvs() → str`

List all virtual environments found in the **current working directory** by
scanning for directories that contain a `pyvenv.cfg` file.

> **Note:** the function is named `listVenvs` rather than `list` because `list`
> is a reserved type keyword in Lynxer.

**Returns** a JSON array of venv names, e.g. `["dev-env", "my-env", "test-env"]`.

```lynx
str result = global.venv.venv.listVenvs();
// → "[\"dev-env\", \"my-env\"]"
```

---

### `venv.remove(str name) → str`

Delete a virtual environment by removing its directory tree.

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `str` | Venv name (resolved against cwd) or an absolute path |

**Returns** `"ok"` on success, `"error:<message>"` on failure.

```lynx
str result = global.venv.venv.remove("my-env");
// → "ok"
```

---

## `global.venv.package`

### `package.add(str venv_name, str pkg_name) → str`

Install a package into a virtual environment using `pip install`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `venv_name` | `str` | Venv name (resolved against cwd) or absolute path |
| `pkg_name`  | `str` | Package name as accepted by pip, e.g. `"requests"`, `"flask==2.3.0"` |

**Returns** `"ok"` on success, `"error:<stderr>"` on failure.

```lynx
str result = global.venv.package.add("my-env", "requests");
// → "ok"
```

---

### `package.remove(str venv_name, str pkg_name) → str`

Uninstall a package from a virtual environment using `pip uninstall -y`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `venv_name` | `str` | Venv name (resolved against cwd) or absolute path |
| `pkg_name`  | `str` | Package name to uninstall |

**Returns** `"ok"` on success, `"error:<stderr>"` on failure.

```lynx
str result = global.venv.package.remove("my-env", "requests");
// → "ok"
```

---

## Complete example

```lynx
global setup(){
    import("venv");
}

global main(){
    // Create
    str r = global.venv.venv.create("sandbox", "");
    print(r); print("\n");          // ok:/path/to/sandbox

    // Install a package
    str i = global.venv.package.add("sandbox", "httpx");
    print(i); print("\n");          // ok

    // List
    str l = global.venv.venv.listVenvs();
    print(l); print("\n");          // ["sandbox"]

    // Uninstall the package
    str u = global.venv.package.remove("sandbox", "httpx");
    print(u); print("\n");          // ok

    // Remove the venv
    str d = global.venv.venv.remove("sandbox");
    print(d); print("\n");          // ok
}
```

---

## Return-value convention

Every function in this module returns a plain `str`:

| Prefix | Meaning |
|--------|---------|
| `"ok"` | Success; no payload |
| `"ok:<value>"` | Success; payload follows the colon |
| `"error:<message>"` | Failure; human-readable message follows the colon |

You can pattern-match on the prefix with a `rawPy{}` block when you need
programmatic error handling.
