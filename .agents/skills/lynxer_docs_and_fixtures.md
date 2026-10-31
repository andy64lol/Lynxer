---
name: lynxer-docs-and-fixtures
description: Update Lynxer documentation, the runnable syntax showcase, examples and expected-output fixtures, and regenerate the static site consistently.
---

# Lynxer docs and fixtures

Use this skill when a change affects user-facing behavior, a builtin, a stdlib
module, the syntax showcase, or an expected test output.

## Canonical sources and generated output

- Canonical prose lives in `docs/*.md` and `docs/stdlib/*.md`.
- `site/build.py` renders `docs/` into `site/docs/*.html`. Do not hand-edit the
  generated HTML unless the task is specifically about the site output; edit the
  Markdown and run:
  ```bash
  python3 site/build.py
  ```
- `site/build.py` has two important lists:
  - `GROUPS` for the sidebar and page order;
  - `STDLIB_MODULES` for the per-module pages.
  Add new pages and modules there or they will not appear.
- `docs/README.md` is the documentation index; keep its tables and module count
  accurate.
- The language server reads `docs/builtins.md` for completions, so adding a
  builtin row there also exposes the name in the LSP.

## Documentation conventions

- Match the surrounding page: short sections, `lynx` code fences, tables for
  operation lists, and “See also” links.
- Show the smallest complete example, including `global setup()`/`global main()`
  when the page's other examples do.
- Document error sentinels, handle lifetimes, and platform limits where they
  matter.
- Use relative links between docs pages. Check that a moved page's links still
  resolve.
- Keep the `////` docstring in a stdlib wrapper in sync with its
  `docs/stdlib/<name>.md`; `--list-stdlibs` prints the wrapper text.

## Runnable showcase: `syntax.lynx`

`syntax.lynx` is the single-file tour of the language and is linted by
`make check`.

- Add a short demonstration when a new user-facing syntax form or builtin lands.
- Keep it runnable and deterministic; avoid network, GUI, audio, and host-specific
  output.
- Run it directly after editing:
  ```bash
  ./lynxer/lynxer syntax.lynx
  ./lynxer/lynxer --lint syntax.lynx
  ```
- If the change is large, also compile it:
  ```bash
  ./lynxer/lynxer --compile syntax.lynx -o /tmp/opencode/syntax
  /tmp/opencode/syntax
  ```

## Fixtures and expected output

Lynxer has two fixture styles:

1. **Sibling `.expected` files** under `lynxer/examples/`. The suite runs the
   `.lynx` file and diffs stdout plus stderr against the `.expected` file.
2. **Inline expected strings** in the root `Makefile`, used by core fixtures such
   as `builtins.lynx`, `conditions.lynx`, and the milestone fixtures.

When behavior changes:

- Update the fixture source and the matching expected output together.
- Preserve exact spacing, quoting, and line endings.
- Command substitution strips trailing newlines; a fixture that intentionally
  ends without a newline can still compare equal. Prefer an explicit newline
  unless the missing newline is part of the test.
- Add the fixture to `LYNXER_PARITY_FIXTURES` when it must also pass after
  `--compile`.
- Keep fixtures hermetic: no network, no wall-clock dependence, no host-specific
  paths or names. Use `lynxer/examples/assets/` for committed inputs.
- Conditional fixtures (GUI, audio, TLS) may be skipped by the suite. State the
  skip and why; do not claim they passed.

## LSP and debugger docs

- `lynxer/scripts/test_lsp.py` checks completion and diagnostics against the
  documentation and parser. If a builtin or keyword is added, consider extending
  that test.
- `lynxer/scripts/test_debugger.py` exercises stepping and breakpoints. Update it
  when statement boundaries or source locations change.
- `docs/language-server.md` and `docs/CLI.md` describe these surfaces; keep them
  accurate when flags or behavior change.

## Verification

```bash
python3 site/build.py
make check
./lynxer/lynxer syntax.lynx
git diff --check -- docs syntax.lynx Makefile lynxer/examples
```

If the site build is not part of the requested change, still verify that the
Markdown links you touched are valid. Do not commit or push unless the user asks.
