# Codeblocks in Lynxer

This document describes the **codeblock** feature in Lynxer, which allows storing and executing blocks of code dynamically. Codeblocks are a powerful tool for implementing callbacks, higher-order functions, and dynamic behavior.

---

## Table of Contents
1. [Introduction](#introduction)
2. [Syntax](#syntax)
3. [Storing Codeblocks](#storing-codeblocks)
4. [Executing Codeblocks](#executing-codeblocks)
5. [Parameterized Codeblocks](#parameterized-codeblocks)
6. [Inline Execution](#inline-execution)
7. [Named Codeblocks](#named-codeblocks)
8. [Limitations](#limitations)
9. [Examples](#examples)
10. [See Also](#see-also)

---

## Introduction

Codeblocks in Lynxer allow you to store executable blocks of code in variables and execute them dynamically. They are useful for implementing callbacks, higher-order functions, and dynamic behavior where the logic is determined at runtime.

---

## Syntax

Codeblocks are defined using the `codeblock` type. They can be stored in variables, passed as arguments, and executed using the `exec()` function.

---

## Storing Codeblocks

You can store a block of code in a variable using the `codeblock` type:

```lynx
codeblock helloWorld = {
    println("Hello world!");
};
```

---

## Executing Codeblocks

To execute a stored codeblock, use the `exec()` function:

```lynx
exec(){{helloWorld}}
```

---

## Parameterized Codeblocks

Codeblocks can declare parameters and their types. These parameters are checked against the provided values when the codeblock is executed.

### Declaring Parameters
```lynx
codeblock example = {
    println(name, age);
}[str name, int age]
```

### Executing with Parameters
```lynx
exec("Alice", 30){{example}}
```

- The parameter list is **positional** and **typed**. Each name used in the codeblock must appear in the list, and each supplied value is checked against its declared type.
- The semicolon after the closing `]` is optional.

---

## Inline Execution

You can execute a codeblock inline using the `exec()` function with a parameter list:

```lynx
str text = "left";
exec(str text) {
    println(text);
}
```

- Each declared value is looked up in the surrounding scope, checked against its declared type, and is available under its declared name while the block runs.
- The declaration is temporary and does not create magic `exec` variables.

---

## Named Codeblocks

Codeblocks can be passed as arguments to functions and executed dynamically:

### Example: Passing Codeblocks to Functions
```lynx
global setup() {
    codeblock code1 = {
        println("A");
    };
}

global something(){handler} {
    exec(){{handler}}
}

global main() {
    global.something(){{code1}}
}
```

- Codeblock identifiers are unique across the entire source file, regardless of where they are declared.
- A `codeblock` variable and a caller-supplied code-block parameter cannot reuse the same identifier.
- Named blocks retain their identity when passed to functions.

---

## Inferred Parameters

If the parameter list is omitted, the names and types are inferred from the variables referenced in the codeblock:

```lynx
codeblock example = {
    println(text);
};

exec("Hello!"){{example}}
```

- The first value is bound to `text`, the next value to the next referenced variable, and so on.
- The number of provided values must match the number of variables required by the codeblock.
- These bindings are temporary and restored after execution.

---

## Limitations

1. **No Nested Scopes**: Codeblocks do not introduce nested scopes. Variables declared inside a codeblock are not visible outside of it, and vice versa.
2. **No Block Expressions**: Codeblocks cannot be used as expressions (e.g., returning their result directly). Use explicit `return` statements.
3. **Unique Identifiers**: Codeblock identifiers must be unique across the entire source file.
4. **No Async/Await**: Codeblocks cannot be used with `async`/`await` syntax.
5. **Old Syntax**: The old `exec({name})` form is replaced by `exec(){{name}}`.

---

## Examples

### Example 1: Basic Codeblock
```lynx
codeblock greet = {
    println("Hello, " + name + "!");
}[str name] // requires a user provided parameter named "name"

exec("Alice"){{greet}}
```

### Example 2: Inline Execution
```lynx
str message = "World";
exec(str message) {
    println("Hello, " + message + "!");
}
```

### Example 3: Passing Codeblocks to Functions
```lynx
global runCode(){code} {
    exec(){{code}}
}

global main() {
    codeblock task = {
        println("Executing task...");
    };
    global.runCode(){{task}}
}
```

### Example 4: Inferred Parameters
```lynx
codeblock printValues = {
    println(x, y);
};

exec(10, 20){{printValues}}
```

---

## See Also

- [Language Reference](language.md) — For more details on Lynxer syntax and types.
- [Built-in Functions](builtins.md) — For built-in higher-order functions.
- [Limitations](limitations.md) — For constraints on codeblocks and other features.
