# Planned implementation gaps

This page summarizes known gaps that are intended to be addressed. Each entry
has an implementation task in [todo.md](../todo.md). Intentional product
boundaries, removed features and compatibility decisions are documented in
[removed-features.md](removed-features.md).

There are no open planned implementation gaps at this time. The native-module
and test/coverage entries that were tracked here have been implemented; see the
**Native modules** section of [todo.md](../todo.md) for the completed work.

## raylib handles are unvalidated

This is a deliberate boundary of the `raylib` bindings, not a planned gap. Its
non-scalar struct values (`Image`, `Model`, `Sound`, `Music`, `Font`, `Color`
returns, …) are exposed as opaque `int` handles, and a handle is the raw address
of a heap-allocated value — there is no registry and no validation. Passing an
invalid, stale or already-released handle, or releasing one twice, is undefined
behaviour and typically crashes the process. Unlike every other module, a bad
handle is **not** reported as a sentinel: the caller owns each handle and must
`<struct>Free` it exactly once. Scalar-only structs (`Vector2`, `Rectangle`,
`Camera3D`, …) avoid handles entirely by being flattened into scalars. See the
[raylib module page](stdlib/raylib.md#handles-are-unvalidated-pointers).
