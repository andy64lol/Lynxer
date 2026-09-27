# Conditionals

## `if` / `elif` / `else`

```lynx
if (n > 0) {
    println("positive");
} elif (n is 0) {
    println("zero");
} else {
    println("negative");
}
```

`else if` is also accepted and behaves like `elif`.

## `switch` / `case` / `default`

Cases are written `case(pattern){ ... }`; there is **no colon form and no
`break` fallthrough** — the first matching case runs, then the `switch` ends.

```lynx
switch (x) {
    case(1){ println("one"); }
    case(2){ println("two"); }
    default(){ println("other"); }
}
```

A pattern may destructure an enum payload and binds names for the body (see
[enums.md](enums.md)):

```lynx
switch (result) {
    case(status.Ok(value)){ println("ok ", value); }
    case(status.Failed(reason)){ println("failed ", reason); }
}
```

## See also

- [loops.md](loops.md) — `while`, `for`, `doWhile`, `iterate`, `forever`, and
  `break`/`continue`/`restart`.
- [language.md](language.md) — the rest of the language reference.
- [enums.md](enums.md) — the enum types that `case` patterns destructure.
