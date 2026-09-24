# Operators

Every operator Lynxer has, with the canonical keyword spelling and the
deprecated symbolic spellings that still parse.

Symbolic spellings are **deprecated**: they compile and run, but each use prints

```
Warning: line N, column M: operator 'X' is deprecated; use 'Y' instead.
```

Use the keyword forms in new code. A bare `!` is not a spelling of anything —
it is a syntax error.

## Arithmetic and text

| Operator | Meaning |
| --- | --- |
| `+` | Addition, or `str` / `char` concatenation |
| `-` | Subtraction |
| `*` | Multiplication |
| `/` | Division — always returns a float |
| `%` | Modulo (integers only) |
| `/%` | Floor division (integers only) |
| `**` | Exponentiation, right-associative |
| unary `-` | Negation |

`//` is a line comment, never division. Integer division without a fraction is
`/%`.

## Comparison and equality

| Operator | Meaning | Deprecated spellings |
| --- | --- | --- |
| `is` | Equal | `==` |
| `isnt` | Not equal | `!=`, `not is` |
| `<` `<=` `>` `>=` | Ordering (numbers, or two strings) | — |

`is` / `isnt` compare any two values, including sentinels and enum variants,
which compare by identity. Ordering applies to numbers and to two strings.

## Boolean logic

| Operator | Meaning | Deprecated spellings |
| --- | --- | --- |
| `and` | Short-circuit AND | `&&` |
| `or` | Short-circuit OR | `\|\|` |
| `not` | Unary NOT | `!!` |
| `nand` | Logical NAND | `!&&` |
| `nor` | Logical NOR | `!\|\|` |
| `xor` | Logical XOR | — |
| `xnor` | Logical XNOR | — |

`and` / `or` short-circuit. `nand`, `nor`, `xor` and `xnor` evaluate both sides.

## Bitwise and shifts

These require `int64` operands.

| Operator | Meaning | Deprecated spellings |
| --- | --- | --- |
| `bitand` | Bitwise AND | `&` |
| `bitor` | Bitwise OR | `\|` |
| `bitxor` | Bitwise XOR | `^` |
| `bitnot` | Bitwise NOT | `~` |
| `bitnand` | Bitwise NAND | `!&` |
| `bitnor` | Bitwise NOR | `!\|` |
| `bitxnor` | Bitwise XNOR | `!^` |
| `bitleft` | Shift left | `<<` |
| `bitright` | Shift right | `>>` |

## Compound assignment

`+=`, `-=`, `*=`, `/=`, `%=`, `**=` and `/%=` are accepted. The result is
validated against the variable's declared type, so `int x = 5; x /= 2;` is an
error rather than a silent truncation.

## Precedence

Lowest to highest:

1. `or` / `nor`
2. `xor` / `xnor`
3. `and` / `nand`
4. `not`
5. equality (`is` / `isnt`) and ordering (`<` `<=` `>` `>=`)
6. `bitor`
7. `bitxor`
8. `bitand`
9. shifts (`bitleft` / `bitright`)
10. `+` `-`
11. `*` `/` `%` `/%`
12. `**` (right-associative)
13. unary (`not`, unary `-`, `bitnot`)
14. postfix and primary

Parenthesise when in doubt: the parser follows this chain exactly.

## Deprecated spellings at a glance

| Deprecated | Use instead |
| --- | --- |
| `==` | `is` |
| `!=` | `isnt` |
| `not is` | `isnt` |
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

See [language.md](language.md) for the surrounding language and
[limitations.md](limitations.md) for behaviour that is not implemented at all.
