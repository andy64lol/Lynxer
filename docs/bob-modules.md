# Bob modules

A Lynxer module is a **directory with a manifest that names its entry point**,
mirroring Node's `node_modules/<name>/`. The interpreter finds it by walking up
from the importing file for `modules/<name>/` and loading the manifest's
`entry` — see [modules.md](modules.md) for the resolution rules.

```text
modules/foo/
  module.toml        # [module] name, version, edition, entry
  src/main.lynx      # the module source
```

The same directory is what `bob --init --module` scaffolds and what `bob
publish` uploads.

## The manifest

`module.toml` carries the metadata the registry and the interpreter both use:

```toml
[module]
name = "foo"
version = "0.1.0"
edition = "2026"
entry = "src/main.lynx"

[dependencies]
```

| Field | Meaning |
| --- | --- |
| `name` | The module name; must be letters, digits, `.`, `_` or `-`, and start with a letter or digit. `import("<name>")` uses it. |
| `version` | Canonical semantic versioning (`1.2.3`), used as the release tag. |
| `entry` | A safe `.lynx` path under `src/`; the file the interpreter loads. |
| `edition` | The Lynxer edition the module targets. |

The interpreter reads only `[module].entry`; the rest is used by Bob and the
registry.

## Source shape

`src/main.lynx` uses the pure-module shape from [extending.md](extending.md): a
`////` documentation header, an empty `global setup()`, and exported `global`
functions. A module is imported by name, not run, so it has no `main()`:

```lynx
////
foo: an example module.
////

global setup(){}

global foo() -> str { return "foo bar!"; }

global main(){}
```

The namespace is the imported name, so a program reaches this as
`global.foo.foo()`:

```lynx
global setup(){ import("foo"); }
global main(){ println(global.foo.foo()); }   // foo bar!
```

## Scaffolding

`bob --init --module` writes `module.toml` and `src/main.lynx` in the current
directory:

```console
$ bob --init --module
Created module.toml
Created src/main.lynx
```

## Published archive

`bob publish` packs only the manifest and the files under `src/`:

```text
my-module-1.0.0.zip
├── manifest.toml    # a copy of module.toml
└── src/
    └── main.lynx
```

The release is tagged with the module version and the asset is named
`{name}-{version}.zip`. Bob validates before upload: the name matches, the
version is canonical semantic versioning, source paths stay under `src/` (no
traversal, no symbolic links), and the archive stays below the size and
file-count limits.

## Worked example: `foo`

```console
# publish
$ bob --init --module          # then edit src/main.lynx to add global foo()
$ GITHUB_TOKEN=... bob publish
Published foo@0.1.0(sha256:fTCAq0Eq...=)     # release 0.1.0, asset foo-0.1.0.zip

# register the name → repository mapping (see bob-registry.md)
#   "foo": { "repository": "andy64lol/foo", "version": "0.1.0" }

# install
$ bob install foo 0.1.0
Installed foo@0.1.0(sha256:fTCAq0Eq...=)
```

> **Status.** `bob install` currently stores the download at
> `bob/packages/foo-0.1.0/archive.zip`; extracting it into `modules/foo/` so the
> interpreter's `import("foo")` finds it is planned (see
> [../Bob/todo.md](../Bob/todo.md)).

## See also

- [modules.md](modules.md) — directory-module resolution in the interpreter
- [extending.md](extending.md) — writing a module end to end
- [bob.md](bob.md) — the Bob CLI
- [bob-registry.md](bob-registry.md) — resolving a name to a repository
