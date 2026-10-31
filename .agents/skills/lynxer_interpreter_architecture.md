---
name: lynxer-interpreter-architecture
description: Map of the Lynxer C++ interpreter pipeline and a checklist for adding syntax, AST nodes, builtins, formatter and lint support without breaking direct or compiled behavior.
---

# Lynxer interpreter architecture

Use this skill when a change touches the language itself: lexer, parser, AST,
optimizer, evaluator, builtins, formatter, lint, debugger, or the compile/embed
path. Read the relevant source before editing; the pipeline is small but each
stage has a matching responsibility elsewhere.

## Pipeline

```text
source (.lynx)
  -> Lexer          lynxer/lexer.cpp, lexer.hpp        tokens
  -> Parser         lynxer/parser.cpp, parser.hpp      AST
  -> Optimizer      lynxer/optimizer.cpp, .hpp         semantics-preserving AST rewrite
  -> Evaluator      lynxer/ast.cpp                     Expression/Statement::evaluate
  -> Runtime        lynxer/runtime.cpp, types.cpp, ops.cpp, native_value.cpp
```

The CLI sits in `lynxer/main.cpp` and `lynxer/shell.cpp`. `lynxer/config.cpp`
holds the compiled-in fallback for `lynxer/lynxer.config`.

## File map

| File | Responsibility |
| --- | --- |
| `lexer.cpp/.hpp` | Token kinds, string/char/number scanning, comments, interpolation detection. |
| `parser.cpp/.hpp` | Recursive-descent parser, precedence, declarations, statements, expressions, interpolation bodies. |
| `ast.hpp/.cpp` | `Expression` and `Statement` hierarchies, `evaluate`, `optimize`, `dump`. |
| `optimizer.cpp/.hpp` | Constant folding, short-circuit simplification, dead-branch removal. |
| `runtime.cpp/.hpp` | Program execution, environments, function calls, module loading, errors. |
| `types.cpp/.hpp` | Value types, conversions, type registry. |
| `ops.cpp/.hpp` | Binary/unary operator semantics. |
| `builtins.cpp/.hpp` | Builtin handler table, unsupported table, native-family dispatch. |
| `native_value.cpp/.hpp` | Native-module value bridge. |
| `formatter.cpp/.hpp` | Token-driven canonical formatting and `--format-oneline`. |
| `lint.cpp/.hpp` | Conservative static analysis, inferred return types, warnings. |
| `debugger.cpp/.hpp` | Source debugger and breakpoints. |
| `bundle.cpp/.hpp` | `--compile` payload and embedded module assembly. |
| `exports.cpp/.hpp`, `embed.cpp/.hpp` | `--emit-library`, embedding ABI, public headers. |
| `lynxer/lsp.py` | Language server; completions read `docs/builtins.md`. |

`LYNXER_SOURCES` in the root `Makefile` is an explicit list. If you add a new
interpreter `.cpp`, add it there; stdlib C++ files are wildcarded separately.

## Adding a builtin

1. Implement the handler in `lynxer/builtins.cpp` next to related builtins.
2. Register it in `handlerTable()`.
3. Remove the name from `unsupportedTable()` if it was listed there.
4. Check `isBuiltinName` callers and any name-specific static analysis.
5. Update `lint.cpp` return-type inference if the builtin returns a known type.
6. Add a row to `docs/builtins.md`; the LSP discovers completions from that table.
7. Add a fixture or extend `lynxer/examples/builtins.lynx`, and update its
   expected output in the root `Makefile`.
8. Run `make buildLynxer` and the narrowest fixture, then `make check`.

Builtins are not first-class values. `any f = println;` must keep failing with
`builtin ... cannot be used as a value`; do not make a builtin callable through
a variable unless the language is deliberately changed.

## Adding syntax or a language feature

Work through every stage:

1. **Lexer** — add or reuse a `TokenKind`; decide whether the token can appear
   inside strings, interpolation, or comments.
2. **Parser** — parse it into a dedicated AST node; keep source line/column on
   the token and use `fail(...)` for located errors.
3. **AST** — implement `evaluate`, `optimize`, and `dump` for every new node.
   `--ast` and the optimizer rely on all three.
4. **Runtime** — add any environment, scoping, or call-frame behavior.
5. **Optimizer** — only fold or remove code when semantics are identical.
6. **Formatter** — teach `formatter.cpp` how to space and indent the new tokens.
7. **Lint** — add type/argument checks when they are conservative and useful.
8. **Debugger** — check whether stepping and breakpoints still land correctly.
9. **Bundle/embed** — confirm `--compile` and `--emit-library` still work.
10. **Docs and showcase** — update `docs/`, `syntax.lynx`, and fixtures.

## Worked example: interpolated output calls

The `printf`/`printlnf` feature shows the full path:

- `lexer.cpp` detects a call target and marks direct string arguments as
  `TokenKind::InterpString`, reading raw text with `readRawString`.
- `parser.cpp::parseInterpString` splits literal text from `{expr}` bodies and
  parses each body as an expression.
- `ast.hpp/.cpp` defines `InterpStringExpression` with `addLiteral`,
  `addExpression`, `evaluate`, `optimize`, and `dump`.
- `builtins.cpp` aliases `printf` to `builtinPrint` and `printlnf` to
  `builtinPrintln`.
- `lint.cpp` knows both names return `none`.
- Docs, `syntax.lynx`, `builtins.lynx`, and the `Makefile` expectation cover it.

## Direct and compiled parity

`--compile` embeds the source, imported `.lynx` modules, `.so` libraries, and
`--include` files into one executable. It is not a separate compiler backend,
but it has its own bundle and runtime path. Any language change must be tested in
both modes. The `LYNXER_PARITY_FIXTURES` loop in the root `Makefile` is the
authoritative check.

## Diagnostics

- Use `SourceError`/`fail` with the token's line and column.
- Preserve the originating module path when an error crosses an import; tests
  assert the module file name, not the importer.
- Do not replace a located error with a generic message.
- Keep runtime errors catchable by `try`/`catch` where the existing contract says
  so.

## Gotchas

- `global setup()` must be the first global declaration and `global main()` the
  last; the parser and runtime enforce this.
- `global.name(...)` resolves to a core builtin before a module member; preserve
  that rule.
- The optimizer must not change output, exit codes, or side-effect order.
- The formatter never changes tokens; it only changes spacing and line breaks.
- Builtin names, keywords, and unsupported names are separate sets; keep them
  consistent.
- Add new interpreter source files to `LYNXER_SOURCES`; forgetting this produces
  link errors or silently stale builds.
- Header changes can require rebuilding dependents; use `make buildLynxer`, not a
  hand-run compiler command.

## Verification

```bash
make -j2 buildLynxer
./lynxer/lynxer --lint <fixture>.lynx
./lynxer/lynxer --ast <fixture>.lynx
./lynxer/lynxer <fixture>.lynx
./lynxer/lynxer --compile <fixture>.lynx -o /tmp/opencode/<fixture>
make check
```

Report any check that could not run and why.
