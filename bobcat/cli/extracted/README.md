# Extracted reference: Lynxer CLI IDE

`cli_ide_extract.cpp` is a verbatim copy of the terminal IDE code from
`lynxer/shell.cpp` (lines 2043–4230): `ideRenderLine`, `struct IdeState`,
`ideRender`, `ideHandleKey` and `runCliIde`.

This is a **reference copy only** — Bobcat does not compile it. Lynxer's
original CLI IDE has since been removed; this copy preserves it. The runnable
Bobcat CLI IDE
is the standalone Rust binary in `../src/main.rs`, which reimplements the same
editor UX and drives `lynxer`/`wynxer` as external engines.

Keeping the extract here documents exactly what was taken from Lynxer and
makes it easy to port further keyboard/rendering behaviour into the Rust IDE.
