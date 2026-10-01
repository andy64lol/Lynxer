# Functions

Lynxer has three function declaration forms. They differ in **where** they are
written and **how** they are called. Parameters, return annotations and
caller-supplied [codeblocks](codeblocks.md) work the same for all three.

| Form | Written | Where | Called from |
|------|---------|-------|-------------|
| Global function | `global name(...)` | between `setup` and `main` | `global.name(...)`, or a bare name in the same file |
| File-level function | `func name(...)` | between `setup` and `main` | a bare name in the same file; `global.module.name(...)` from an importer |
| Local function | `local name(...)` | inside another function body | a bare name, or `local.name(...)` |

`global func name(...)` is **not** valid syntax (`unexpected character ' '`);
use `func name(...)` for a file-level function.

## Global functions

`global` is the default, exported declaration. Declare it top-level, between
`global setup()` and `global main()`:

```lynx
global setup(){}

global double(int value) -> int {
    return value * 2;
}

global main(){
    println(double(21));        // 42, bare name in the same file
    println(global.double(21)); // 42, fully qualified
}
```

- A global function is callable by its bare name in its own file and by
  `global.name(...)` everywhere, including from an importer as
  `global.module.name(...)`.
- `global setup()` and `global main()` are global functions with extra rules:
  `setup` must be the first declaration, `main` the last, and either may take
  defaulted parameters. See [language.md](language.md#program-structure) and
  `overrideMain` in [language.md](language.md#overridemain).
- Entry-point functions may **not** declare codeblock parameters.

## File-level functions (`func`)

A `func` belongs to its source file. Declare it top-level, between
`global setup()` and `global main()`:

```lynx
global setup(){}

func double(int value) -> int {
    return value * 2;
}

global main(){
    println(double(21));   // 42
}
```

- `func` names are **file-scoped**: two imported files may each define a
  `double` helper without colliding.
- In the defining file, call it by bare name. From an importer, call it through
  the module: `global.moduleName.double(21)` (see [modules.md](modules.md)).
- `func` declarations must be top-level and may not follow `global main()`
  (`declarations may not follow global main()`).

## Local functions

A `local` function is declared inside another function body and is not visible
outside it:

```lynx
global setup(){}

global main(){
    local square(int n) -> int {
        return n * n;
    }
    println(square(4));        // 16, bare name
    println(local.square(5));  // 25, qualified
}
```

- A local function is not exported and cannot be called from another function.
- It may close over the top-level variables that are in scope where it runs.

## Parameters and defaults

- Parameters may be typed (`int n`) or untyped (treated as `any`).
- A parameter may have a default (`int b = 1`).
- A required parameter may **not** follow one with a default:
  `required parameters cannot follow a parameter with a default value`.
- Class methods are the exception: their parameters are typed but may **not**
  have defaults (see [classes.md](classes.md)).

## Return annotations

Only the arrow form is accepted:

```lynx
global f(int x) -> int { return x + 1; }
```

- `: int` is **not** valid syntax (`unexpected character ':'`).
- Omit the annotation, or use `-> any`, to leave the result unchecked.
- `-> none` is accepted: the function must return nothing, and a returned value
  fails with `value cannot be assigned to type 'none'`.
- A mismatched return is a source-located runtime error.
- Named types (a `struct`, `class`, or `enum`) may be used as the annotation.

## Caller-supplied codeblocks

Any of the three forms may declare caller-supplied blocks **after** its
parameter list and before its body. Write each block name in its own `{...}`:

```lynx
<kind> name(<params>){<blockName>}{<blockName>}... { <body> }
```

The body invokes a block with `exec(){{<blockName>}}`. A caller supplies a block
with `(){ ... }` or, for a named [codeblock](codeblocks.md) variable, `(){{var}}`:

```lynx
global setup(){}

global runTwice(){first}{second}{
    exec(){{first}}
    exec(){{second}}
}

func mapTwice(int value){apply}{
    exec(value){{apply}}
    exec(value){{apply}}
}

global main(){
    runTwice(){
        println("one");
    }{
        println("two");
    }

    codeblock triple = {
        println(value * 3);
    };
    mapTwice(4){{triple}}   // 12 printed twice
}
```

- The codeblock-parameter syntax is identical for `global`, `func` and `local`
  declarations; the parser treats all three the same way.
- Callers may mix inline blocks (`(){ ... }`) and named references
  (`(){{var}}`) in the same call.
- `func` and `local` blocks are supplied the same way; a `func` reached through
  an import takes blocks at the call site too:
  `global.module.runTwice(){...}{...}`.
- Entry-point functions (`setup`/`main`) may not declare codeblock parameters.
- A codeblock name must be unique across the source file and may not repeat a
  regular parameter name of the same function.

See [codeblocks.md](codeblocks.md) for the full reference, including inferred
parameters and the `codeblock` type.

## Scoping

There are no nested block scopes; see [language.md](language.md#scoping). A call
to any of these forms gets a fresh scope for its parameters and locals. Those do
not leak out, but the function can read and update the top-level variables it
closes over.

## See also

- [language.md](language.md#functions) — functions in the context of program
  structure.
- [codeblocks.md](codeblocks.md) — stored and caller-supplied codeblocks.
- [async.md](async.md) — local `async` sub-functions and `await`.
- [classes.md](classes.md) — `local` methods on a class.
- [modules.md](modules.md) — calling exported `global` and `func` names across
  files.
