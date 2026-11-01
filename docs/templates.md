# Templates

A `template` is a named record type with typed constructor parameters, typed
fields, and `fn` methods. The deprecated `local` method spelling remains
accepted with a warning. A template is the current way to define objects; the older
[`class`](classes.md) form is deprecated but still supported.

Declare templates at file scope using `fn` for entry points and ordinary
functions. The template parameter list in the header is mandatory, even when
it is empty.

## Declaration and construction

```lynx
fn setup(){}

template Greeter(str name, int times) {
    str greeting = "Hello";

    fn line() {
        return this.greeting + ", " + this.name + "!";
    }

    fn speak() {
        int index = 0;
        while (index < this.times) {
            println(this.line());
            index += 1;
        }
    }
}

fn main(){
    build Greeter("Andy", 2) named greeter;
    greeter.speak();
}
```

The header parameters (`str name`, `int times`) are **also fields**. Building the
template converts each argument to the declared parameter type and stores it in
the field of the same name, so methods read it as `this.name` and `this.times`.

`build Template(arguments) named variable;` is a declaration. It creates the
instance and declares `variable` with the template as its type. The variable
then supports field access and method calls like any other record:

```lynx
println(greeter.name);       // Andy
greeter.times = 1;
greeter.speak();
```

`new Template(arguments)` still works and produces the same value, but
`build ... named` is the canonical form.

## Fields and methods

Body fields use `type name = initialValue;`, exactly as in a legacy class. A
field may be `const`, and `fn` methods use `this.field` to read and write
fields. The deprecated `local` method spelling is still accepted with a
warning.

```lynx
template Counter(int value) {
    int calls = 0;
    const int step = 1;

    fn increment() {
        this.value += this.step;
        this.calls += 1;
    }

    fn get() {
        return this.value;
    }
}
```

The header parameter field `value` is initialized from the build argument;
`calls` and `step` use their body initializers. Body field initializers run in
the environment around the construction, not inside a method, so they cannot
read `this` or the header parameters. A parameter name may not be declared
again as a body field; that is a parse error.

Methods follow the legacy class rules:

- no return annotation (`fn get() -> int` is invalid);
- parameters are typed but cannot have default values;
- method lookup uses the instance's actual template, so overrides dispatch
  dynamically.

## Inheritance

Use `extending` to derive one template from another previously declared
template:

```lynx
template Animal(str name) {
    fn speak() {
        return "...";
    }
}

template Dog(str name) extending Animal {
    fn speak() {
        return "woof";
    }
}
```

The child inherits the parent's fields and methods. A child method with the
same name overrides the inherited method:

```lynx
build Dog("Rex") named pet;
println(pet.name);        // Rex
println(pet.speak());     // woof
```

Inheritance is single-parent, and the parent must be declared first. The child
declares its own constructor parameters; inherited parameter fields are
initialized to `none` unless the child redeclares them in its own header. In
the example above, `Dog(str name)` shadows `Animal`'s `name` field with its own
parameter field.

A template can only extend another template. A legacy `class` can only extend
another class; mixing the two forms is a parse error.

Templates do not use `init` or `super(...)`. The header is the constructor, and
inherited state is expressed through fields and overridden methods:

```lynx
template Tagged(str tag) {
    str kind = "tag";

    fn show() {
        return this.kind + ":" + this.tag;
    }
}

template Plain() extending Tagged {
    str kind = "plain";
}

build Plain() named plain;
println(plain.show());    // plain:none
```

## Differences from legacy classes

| | `template` | legacy `class` |
| --- | --- | --- |
| Constructor | header parameters | `fn init(...)` |
| Construction | `build T(args) named x;` | `T x = new T(args);` |
| Inheritance keyword | `extending` | `extends` |
| Parent constructor | none | `super(...)` inside `init` |
| Status | current | deprecated |

The `class` form keeps working for compatibility, including `new`, `extends`,
and `super(...)`. New code should use templates.

## Errors

| Situation | Message |
| --- | --- |
| Missing header parentheses | `expected '(' after template name` |
| Wrong argument count | `template 'T' expects 2 arguments, received 1` |
| Argument type mismatch | the usual conversion error, located at the build |
| `init` declared in a template | `a template declares its constructor parameters in its header; 'init' methods are not allowed` |
| Parameter redeclared as a field | `template parameter 'x' is already a field; remove the duplicate declaration` |
| Template extends a class | `template 'T' cannot extend class 'C'; use 'class' instead` |

## See also

- [classes.md](classes.md) — the deprecated class form.
- [structs.md](structs.md) — data-only records, positionally constructed.
- [vargroups.md](vargroups.md) — records with defaulted fields.
- [enums.md](enums.md) — tagged unions.
