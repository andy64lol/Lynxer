# Deprecated syntax and APIs

This page is the migration index for Lynxer's deprecated language syntax.
Deprecated forms remain accepted where stated, but new code should use the
replacement. Deprecation warnings are emitted for the declaration keywords,
the `global.` call prefix, deprecated operators, tuple bracket coercions, and
legacy methods where the parser/runtime supports them. `suppressDeprecationWarning()`
suppresses runtime deprecation warnings; it does not make old syntax canonical.

## Function declarations

Use `fn` for top-level functions, nested functions, and methods.

| Deprecated declaration | Replacement | Notes |
| --- | --- | --- |
| `global name(...) { ... }` | `fn name(...) { ... }` | Use a bare call in the same module. |
| `func name(...) { ... }` | `fn name(...) { ... }` | Use a bare call in the same module. |
| `local name(...) { ... }` | `fn name(...) { ... }` | Declare nested functions with `fn`; call them by name or qualified path. |
| `local method(...) { ... }` in a `template` or legacy `class` | `fn method(...) { ... }` | Method behavior is unchanged. |
| `global.name(...)` for a same-module function | `name(...)` | Do not remove `global.` when it refers to a builtin or an imported namespace. |
| `local.name(...)` for a nested function | `name(...)` or `parent.name(...)` | Use the nested hierarchy when calling from outside the declaring scope. |

Legacy function declaration keywords produce a deprecation warning recommending
`fn`. Call same-module functions by name, imported functions through
`<module>.<name>(...)`, and nested functions through their hierarchy. The
former `global.` and `local.` function prefixes are unnecessary. The `global.`
prefix still has builtin/namespace behavior in some contexts; do not
mechanically remove it from builtin calls or native-module forwarders without
checking what the name refers to.

### The `global.` prefix

Every `global.<name>` in a program emits a parser deprecation warning with the
code `deprecatedGlobalPrefix`, naming the bare replacement:

```
program.lynx:2:20: warning[deprecatedGlobalPrefix]: the 'global.' prefix is deprecated; use 'trim' instead
```

It is a warning, not an error: the form keeps working and program output is
unchanged. It appears when a program is **run** and in `--lint` output (where it
counts toward the warning total), so it also shows up in editor diagnostics.
Mechanical tooling parses -- `--format`, `--ast`, `--compile` and the embedding
runtime -- do not emit it, and **imported module sources are exempt**: the
standard library declares its functions with `fn` but still uses
`global.<builtin>(...)` and `global.<namespace>.<op>(...)` internally to reach
native-module forwarders, without producing warnings. A macro
body that uses `global.` is expanded into its caller, so that use is attributed
to the importing program and does warn.

`global.` is not a way to reach variables from another scope. Variables are
looked up by bare name; `global.x` resolves to a variable only when that name
already exists in the active environment chain (see
[language.md](language.md#scoping)). With a bare name the lookup is identical,
so drop the prefix in new code. The one form that genuinely needs the prefix
is a function passed as a value to a built-in such as `nativeThreadStart`;
that form also warns, since a bare named function works the same way.

## Legacy class form

The `class` object form is deprecated in favor of [`template`](templates.md).
Use template header parameters, `build Type(args) named value;`, and
`extending` inheritance. The legacy form remains supported for compatibility;
see [classes.md](classes.md) for its `init`, `new`, `extends`, and `super`
behavior.

## Operator spellings

Use keyword operators instead of these deprecated symbolic spellings:

| Deprecated | Replacement |
| --- | --- |
| `==` | `is` |
| `!=`, `not is` | `isnt` |
| `&&` | `and` |
| `\|\|` | `or` |
| `!!` | `not` |
| `!&&` | `nand` |
| `!\|\|` | `nor` |
| `&` | `bitand` |
| `\|` | `bitor` |
| `^` | `bitxor` |
| `~` | `bitnot` |
| `!&` | `bitnand` |
| `!\|` | `bitnor` |
| `!^` | `bitxnor` |
| `<<` | `bitleft` |
| `>>` | `bitright` |

The symbolic forms still parse and run while warning at each use. A bare `!` is
not deprecated syntax; it is invalid.

## Tuple assignment

Assign tuples using parenthesized tuple syntax rather than coercing a bracket
list:

```lynx
tuple current = (int 1, int 2);
current = (int 3, int 4);
```

Assigning a bracket list to a `tuple` variable is accepted for compatibility,
but emits a deprecation warning. `tupleCreate(...)` and `listToTuple(...)` are
the explicit conversion helpers. See [tuples.md](tuples.md#declaration).

## Legacy WinAPI built-ins

The `winAPI.*` builtin calls remain available for compatibility but emit a
deprecation warning. Import the native-backed `winAPI-amd64` or `winAPI-arm64`
standard library with `importAs` and call its functions through the module
alias instead. The selected module checks the host operating system and
architecture. See [windows-api.md](windows-api.md).

## Other legacy syntax

The old `exec({name})` codeblock invocation is replaced by `exec(){{name}}`;
use the latter form. See [codeblocks.md](codeblocks.md).

## See also

- [functions.md](functions.md#deprecated-declaration-keywords)
- [operators.md](operators.md#deprecated-spellings-at-a-glance)
- [tuples.md](tuples.md)
- [templates.md](templates.md) and [classes.md](classes.md)
