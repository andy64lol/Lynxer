# Wynxer todo

Wynxer is a Lynxer-like language built for websites: it compiles to JS, WASM,
HTML and CSS, uses `.wpf` (Wynxer Page Format) instead of hand-written HTML,
and is a separate binary (v0.1.0) with a Rust backend. This file tracks the
work to take it from scaffolding to a usable web toolchain.

## 0. Project setup
- [x] Create the `wynxer/` crate (Cargo.toml, version 0.1.0, MIT licence).
- [x] Wire up the CLI shell (`wynxer version`, `elements`, `parse`, `build`, `serve`, `check`, `init`, `run`).
- [x] Add a README and example files.
- [x] Add a `wynxer.config` with name, version and CLI messages (mirror lynxer).
- [x] Add CI workflow for Wynxer (build + test on Linux and Windows).
- [ ] Add an AUR package (`wynxer`, source; `wynxer-bin` later).
- [ ] Add Windows installer / packaging skeleton.

## 1. Language (Wynxer)
- [x] Define the Wynxer surface syntax (borrow from Lynxer: `fn`, typed variables `int x = 5;`, no `let`, types, control flow).
- [x] Write the lexer (`src/lexer.rs`).
- [x] Write the parser (`src/parser.rs`) — recursive descent, expressions, control flow, typed variables (no `let`).
- [x] Build the AST (`src/ast.rs`) — expanded with `Expr`, `Stmt`, `Function`, `StructDef`, `EnumDef`, `Module`.
- [x] WPF element registry with common-sense APIs (header1..header6, text, button, newline, paragraph, image, link, input, div, span, list, table, etc. — 56 kinds).
- [x] Type checker with basic validation (`wynxer check` validates element kinds and parse the project).
- [x] Borrow the macro system from Lynxer (`pub`, `*args`, codeblock parameters) — parser handles `pub fn`, `*args`, `...` variadics, codeblock params; 3 dedicated tests.
- [ ] Error reporting with source locations (file:line:column + caret).
- [ ] `--ast`, `--lint`, `--format` style tooling, like lynxer.
- [x] Unit tests for the lexer and parser (22 tests pass: 8 lexer + 14 parser).

## 2. WPF (Wynxer Page Format)
- [x] Define the `.wpf` grammar (page, contents, element, style binding, scripts).
- [x] Implement the `.wpf` parser (`src/wpf/`) with `import`, `page`, `page.title`, `contents`, `element.*`, `ignore`, `scripts`.
- [x] Element registry with common-sense APIs (header1..header6, text, button, newline, paragraph, image, link, input, div, span, list, table, etc. — 56 kinds).
- [ ] Validate `.wpf` (unknown tags, missing required attributes, duplicate ids).
- [ ] Support nesting and layout primitives (row, column, grid, stack).
- [ ] Support bindings from Wynxer state to WPF attributes (`text = {count}`).
- [ ] Support events (`-> onClick`, `-> onSubmit`).
- [ ] Support components / reusable page fragments.
- [ ] Add `--wpf` linting and a runnable showcase (`syntax.wpf`).

## 3. Intermediate representation
- [ ] Define the IR (`src/ir.rs`) for pages, components, styles and logic.
- [ ] Lower WPF + Wynxer into the IR.
- [ ] Lowering tests.

## 4. Backends (Rust generates everything)
### 4.1 HTML
- [ ] HTML generator from the IR (`src/backend/html.rs`).
- [ ] Semantic tags, accessibility attributes, meta tags.
- [ ] No hand-written HTML in the output; everything comes from `.wpf`.
### 4.2 CSS
- [ ] CSS generator (`src/backend/css.rs`).
- [ ] Layout primitives (flex/grid) emitted from WPF layout tags.
- [ ] Theming (light/dark) and design tokens.
- [ ] Responsive breakpoints.
### 4.3 JavaScript
- [ ] JS code generator (`src/backend/js.rs`).
- [ ] Event handlers, state updates, DOM binding.
- [ ] Module output (ES modules).
- [ ] Source maps.
### 4.4 WebAssembly
- [ ] WASM code generator (`src/backend/wasm.rs`).
- [ ] Lynxer-style stdlib subset ported to WASM (strings, lists, math, json).
- [ ] JS glue for calling WASM exports.
- [ ] WASM size budget / tree-shaking.

## 5. Build and project model
- [x] `wynxer.config` (name, version, entry, output dir).
- [x] `wynxer init [name]` — scaffold a project (README, index.wpf, app.wynx, styles.css, .gitignore, `.wynxcfg`, build/); refuses to overwrite.
- [x] `wynxer run [path] [--port N]` — build, then serve; reads output/port from `.wynxcfg`.
- [x] `wynxer build` — emit `build/` with index.html, *.js, *.css (HTML from .wpf, JS transpiled from .wynx and inline scripts, CSS concatenated).
- [x] `wynxer serve` — minimal HTTP dev server serving the build directory.
- [x] `wynxer check` — parse + validate element kinds.
- [ ] WASM code generation (rustc --target wasm32-unknown-unknown).
- [ ] `--compile` / `--no-opt` style flags, like lynxer.
- [ ] Asset handling (images, fonts) copied into `build/`.

## 6. Tooling
- [ ] Formatter (`wynxer --format`).
- [ ] Linter with WPF + Wynxer rules.
- [ ] Language server (LSP) for `.wynx` and `.wpf`.
- [ ] Debugger (source maps + browser devtools).
- [ ] Documentation site (`wynxer/site/`), generated like lynxer's.

## 7. Standard library for the web
- [ ] DOM helpers (query, create, update elements).
- [ ] Fetch / HTTP client.
- [ ] Storage (localStorage / sessionStorage).
- [ ] Routing (client-side router).
- [ ] Forms and validation.
- [ ] Animation / transitions.

## 8. Testing
- [ ] Unit tests for lexer/parser/typechecker.
- [ ] Snapshot tests for the JS/WASM/HTML/CSS generators.
- [ ] End-to-end tests driving a headless browser.
- [ ] Parity: interpreted vs compiled output.

## 9. Documentation and examples
- [ ] `docs/` with the same structure as lynxer's (CLI, language, WPF, stdlib).
- [ ] `syntax.wynx` and `syntax.wpf` showcases.
- [ ] Example projects (todo app, blog, dashboard).
- [ ] README with build/run commands.

## 10. Release and packaging
- [ ] Version bump workflow (mirror lynxer's release checklist).
- [ ] AUR package (`wynxer`, `wynxer-bin`).
- [ ] Windows installer + release zip.
- [ ] GitHub Actions CI for Linux (amd64/arm64) and Windows (amd64/arm64).
- [ ] npm-style distribution (optional).

## Notes
- Wynxer reuses Lynxer's language ideas but is a separate binary and a
  separate project. Do not mix the two codebases; share only concepts.
- The Rust backend is the single source of truth for code generation.
- No hand-written HTML or CSS should appear in a Wynxer project; everything
  comes from `.wpf` and the Wynxer source.
- Do not push without explicit permission.
