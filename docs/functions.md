# Functions

`fn` is the function declaration keyword. At file scope it declares a
same-module function; inside another function it declares a nested function.
Parameters, return annotations and caller-supplied
[codeblocks](codeblocks.md) work the same at either location.

```lynx
fn setup() {
    // Optional initialization.
}

fn calculate(int value) -> int {
    return value * 2;
}

fn parent() {
    fn nested() {
        println("nested function");
    }
    nested();
}

fn main() {
    println(calculate(21)); // 42
    parent.nested();
}
```

`main()` is required; `setup()` is optional. When `setup()` is absent the
compiler warns, and when `main()` is absent it reports an error. Both are
ordinary `fn` declarations with entry-point roles, and declarations may appear
in any order. Entry-point functions may take defaulted parameters but may not
declare codeblock parameters. See
[language.md](language.md#program-structure) and `overrideMain` in
[language.md](language.md#overridemain).

Imported functions use their module namespace, including aliases:

```lynx
importAs("math", "m");
println(m.calculate(21));
```

Nested names use hierarchical dot notation, with no fixed depth:
`parent.child.grandchild()`.

## Deprecated declaration keywords

The old `global`, `local` and `func` declarations remain accepted for
backwards compatibility, but each produces a deprecation warning recommending
`fn`. Replace the declaration keyword with `fn`; call functions directly in
the current module, through an imported module namespace, or through their
nested function path. The old `global.` and `local.` function prefixes are
also unnecessary for ordinary function calls.
See [deprecated.md](deprecated.md) for the complete migration index.

## Named lambdas

A named `lambda` is declared inside a function and called by its name in that
scope. It accepts typed parameters and an optional `-> type` return annotation:

```lynx
fn setup(){}

fn main() {
    str greeting = "Hello";

    lambda greet(str name) -> str {
        return inter"{greeting}, {name}!\n";
    }

    println(greet("Andy")); // Hello, Andy!

    fn nested() {
        println(greet("Andy")); // out-of-scope error if nested() is called
    }
}
```

Lambdas are scoped declarations, not global or exported functions. A lambda is
available in the function scope where it is declared; nested function scopes
cannot see its name. It can read variables from its enclosing function while
that scope is active. Control-flow blocks do not create nested scopes in
Lynxer; see [language.md](language.md#scoping). Lambdas support the same typed
and inferred generic parameters as other ordinary functions.

## Function values

Named functions, local functions, and lambdas can be used as values. Use the
`function` type for a checked declaration, or `any` when the value may also be
something else:

```lynx
fn setup(){}

fn addOne(int value) -> int {
    return value + 1;
}

fn apply(function operation, int value) -> int {
    return operation(value);
}

fn makeAdder(int base) -> function {
    fn add(int value) -> int {
        return base + value;
    }
    return add;
}

fn main() {
    function increment = addOne;
    println(apply(increment, 4)); // 5

    function addTen = makeAdder(10);
    println(addTen(3)); // 13
}
```

`function` values can be passed as arguments, returned, assigned, and called
through variables with the same argument and return checks as a direct call.
Local functions and lambdas retain their enclosing variable scopes, so a
returned function can still read and update captured variables. Their names
remain subject to the normal scope rules; in particular, a nested function
cannot look up a named lambda declared by its caller. A function value passed
explicitly as an argument remains available to the receiving function.

## Parameters and defaults

- Parameters may be typed (`int n`) or untyped (treated as `any`).
- A parameter may have a default (`int b = 1`).
- A required parameter may **not** follow one with a default:
  `required parameters cannot follow a parameter with a default value`.
- `type T of value` introduces a type parameter named `T`, inferred from the
  argument `value`. Use `T` to require matching argument types and to type the
  return value:

  ```lynx
  fn setup(){}

  fn keep(type T of first, T second) -> T {
      return second;
  }

  fn main() {
      int number = keep(1, 2);
      str word = keep("one", "two");
      println(number); // 2
      println(word);   // two
  }
  ```

  `T` can also be used for local declarations. Each call infers its own type;
  within one call, arguments using the same `T` must have the same Lynxer type.
  The generic parameter itself cannot have a default value; exported C
  functions and template/class methods do not support inferred generic
  parameters.
- Template and class methods are the exception: their parameters are typed but
  may **not** have defaults (see [templates.md](templates.md) and
  [classes.md](classes.md)).

## Return annotations

Only the arrow form is accepted:

```lynx
fn f(int x) -> int { return x + 1; }
```

- `: int` is **not** valid syntax (`unexpected character ':'`).
- Omit the annotation, or use `-> any`, to leave the result unchecked.
- `-> none` is accepted: the function must return nothing, and a returned value
  fails with `value cannot be assigned to type 'none'`.
- A mismatched return is a source-located runtime error.
- Named types (a `struct`, `template`, `class`, or `enum`) may be used as the
  annotation.

## Caller-supplied codeblocks

Any function may declare caller-supplied blocks **after** its parameter list
and before its body. Write each block name in its own `{...}`:

```lynx
<kind> name(<params>){<blockName>}{<blockName>}... { <body> }
```

The body invokes a block with `exec(){{<blockName>}}`. A caller supplies a block
with `(){ ... }` or, for a named [codeblock](codeblocks.md) variable, `(){{var}}`:

```lynx
fn setup(){}

fn runTwice(){first}{second}{
    exec(){{first}}
    exec(){{second}}
}

fn mapTwice(int value){apply}{
    exec(value){{apply}}
    exec(value){{apply}}
}

fn main(){
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

- The codeblock-parameter syntax is identical for top-level and nested `fn`
  declarations.
- Caller-supplied blocks also work on imported module functions.
- Callers may mix inline blocks (`(){ ... }`) and named references
  (`(){{var}}`) in the same call.
- An imported function takes blocks at the call site too:
  `module.runTwice(){...}{...}`.
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
- [templates.md](templates.md) — header constructors, `build ... named`, and
  fields.
- [classes.md](classes.md) — the deprecated class form.
- [modules.md](modules.md) — calling functions through imported module names.
