# Loops

Lynxer has five loop keywords: `while`, `for`, `doWhile`, `iterate` and
`forever`. Every one takes parentheses; `forever` and the condition-less
`doWhile` are written `forever(){ ... }` and `doWhile(){ ... }`.

## `while`

```lynx
while (n > 0) {
    n -= 1;
}
```

## `for`

`for` has two forms: the C style and the range style.

### C style

```lynx
for (int i = 0; i < 3; i = i + 1) {
    println(i);
}

for (int i = 0; i < 3) {   // update omitted: the increment is implicit
    println(i);
}
```

`i++` and `i--` do not exist — use `i = i + 1`, `i += 1`, or the implicit
update.

### Range style

The range form iterates an integer variable over a range of values:

```lynx
for (int i = 0..=10..2) { ... }   // 0, 2, 4, 6, 8, 10
for (int i = 0..10..2) { ... }    // 0, 2, 4, 6, 8
for (int i = 1..=3..1) { ... }    // 1, 2, 3
for (int i = 1..=3) { ... }       // 1, 2, 3 — the step defaults to 1
for (int i = 10..=0..-2) { ... }  // 10, 8, 6, 4, 2, 0
```

The header is `for (<type> <name> = <start> (.. | ..=) <end> [.. <step>])`:

- `..=` **includes** the end value; `..` **excludes** it.
- The optional third value is the step. It must be non-zero; a negative step
  counts down, and the bound comparison flips accordingly. Step of 1 must be
  written explicitly when a third value is present (`0..=10..1`).
- Bounds and step are evaluated once and must be integers; a non-integer bound
  or a zero step is a source-located error.
- The loop variable is (re)declared and assigned each iteration, so the body
  sees it. Reassigning it inside the body does not change the iteration — the
  range is fixed.
- The C style and the range style cannot be mixed; the range style has no
  separate condition or update clause.

`break` and `continue` work as in any loop.

| Loop | Values |
| --- | --- |
| `for (int i = 0..=10..2)` | `0, 2, 4, 6, 8, 10` |
| `for (int i = 0..10..2)` | `0, 2, 4, 6, 8` |
| `for (int i = 0..=3..1)` | `0, 1, 2, 3` |
| `for (int i = 0..0..1)` | *(no iterations)* |
| `for (int i = 10..=0..-2)` | `10, 8, 6, 4, 2, 0` |
| `for (int i = 3..0..-1)` | `3, 2, 1` |

`lynxer/examples/range_for.lynx` pins all of these.

## `doWhile`

```lynx
doWhile (n < 3) {          // tests before each pass after the first
    n += 1;
}

doWhile(){                 // no-condition form: loops until `break`
    n += 1;
    if (n > 9) { break; }
}
```

## `iterate`

```lynx
iterate (4) {              // exactly four iterations
    println("tick");
}
```

`iterate(count)` requires an integer count.

## `forever`

```lynx
forever(){                 // loops until `break`
    if (done) { break; }
}
```

`forever()` without a reachable `break` prints a warning unless
`suppressForeverWarning()` was called in `setup()`.

## `break`, `continue`, `restart`

- `break` leaves the innermost loop (and ends a `switch`).
- `continue` jumps to the next iteration.
- `restart` re-runs the current iteration from the top.

## See also

- [conditionals.md](conditionals.md) — `if`/`elif`/`else` and `switch`/`case`/`default`.
- [language.md](language.md) — the rest of the language reference.
