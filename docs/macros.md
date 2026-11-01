# Macros

Macros expand into Lynxer tokens before the file is parsed. A macro is visible
throughout its declaring source file, including before its declaration:

```lynx
macro printAll!(*args){println(*args)}

fn setup(){}

fn main(){
    printAll!("value", 42);
}
```

`*args` stands for all tokens supplied in the invocation's parentheses,
separated by commas. It can be used more than once in the macro body. Macro
definitions do not become runtime functions.

## Exporting macros

Macros are private to their file by default. Prefix a declaration with `pub`
to make it available to a file that imports that source module:

```lynx
pub macro logValue!(*args){println(*args)}
```

The importer must use a source-file `import` or `importAs`; native modules do
not export Lynxer macros. Relative source imports resolve from the importing
file's directory, including imports made by an imported module. See
[modules.md](modules.md#search-order).

## Caller-supplied codeblocks

Macros can declare named codeblock parameters after `(*args)`, using the same
brace signature as a function. In the macro template, `{{name}}` inserts the
caller's block, including its braces:

```lynx
fn apply(int value){body}{
    exec(value){{body}}
}

macro applyNow!(*args){body}{apply(*args){{body}}}

fn setup(){}

fn main(){
    applyNow!(42){
        println(value);
    };
}
```

Any `fn` declaration can receive caller-supplied codeblocks. Their parameters
are declared as `{name}` after the regular parameters and invoked with
`exec(...){{name}}`; callers provide inline blocks at the call site. The legacy
`global`, `func`, and `local` declaration forms also support codeblocks, but are
deprecated. See [functions.md](functions.md) and [codeblocks.md](codeblocks.md).
