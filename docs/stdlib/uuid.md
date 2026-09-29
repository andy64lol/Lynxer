# uuid

UUID generation, parsing and formatting.

**Backend:** native — `stdlib/uuid.so`, built from the Rust crate
`rust/uuid` over [`uuid`](https://docs.rs/uuid) with the `v3`, `v4`, `v5` and
`v7` features.
**Import:** `import("uuid")` → `global.uuid.*`

## How UUIDs travel

A UUID crosses the ABI as a **string**; the canonical form is the lower-case
hyphenated one, `8-4-4-4-12`. `uuidToHex` returns the *simple* form instead — the
32 hex characters of the 16 bytes with no hyphens — and `uuidFromHex` accepts
exactly that. `uuidToBytes` / `uuidFromBytes` exchange the raw 16 bytes as a
`bytes` value.

Parsing accepts every form the crate does, and trims surrounding whitespace
first:

| Input | Example |
| --- | --- |
| Hyphenated | `a1a2a3a4-b1b2-c1c2-d1d2-d3d4d5d6d7d8` |
| Simple | `a1a2a3a4b1b2c1c2d1d2d3d4d5d6d7d8` |
| URN | `urn:uuid:a1a2a3a4-b1b2-c1c2-d1d2-d3d4d5d6d7d8` |
| Braced | `{a1a2a3a4-b1b2-c1c2-d1d2-d3d4d5d6d7d8}` |

The URN and braced forms wrap the **hyphenated** form; wrapping the simple form
is not accepted.

A failure is in-band: an unparseable UUID yields `""` from a string operation,
`false` from `uuidValid`, and `-1` from `uuidVersion` and `uuidTimestamp`.

## Generation

`uuidV4` is random and `uuidV7` is time-ordered, so neither is reproducible.
`uuidV3` and `uuidV5` hash a name into a namespace, so they are — and they match
the RFC 9562 examples, which is what the fixture pins.

`uuidVersion` reports the version nibble as-is, which is why a UUID that is not
a well-formed RFC variant (such as one whose 13th hex digit is `c`) reports a
version of `12`. `uuidVariant` names the variant (`ncs`, `rfc4122`,
`microsoft`, `future`). `uuidTimestamp` returns the Unix time in milliseconds
for a time-based UUID and `-1` for v3, v4, v5 and nil.

`v1` and `v6` are deliberately not exposed.

## Functions

| Function | Signature | Returns |
| --- | --- | --- |
| `uuidV4` | `()` | A random version 4 UUID |
| `uuidV7` | `()` | A time-ordered version 7 UUID |
| `uuidV3` | `(str namespace, str name)` | A version 3 (MD5) UUID, or `""` if the namespace is not a UUID |
| `uuidV5` | `(str namespace, str name)` | A version 5 (SHA-1) UUID, or `""` if the namespace is not a UUID |
| `uuidNil` | `()` | `00000000-0000-0000-0000-000000000000` |
| `uuidParse` | `(str s)` | The canonical lower-case form, or `""` |
| `uuidFormat` | `(str s, bool upper)` | The canonical form, upper-cased when `upper`, or `""` |
| `uuidValid` | `(str s)` | `true` if `s` parses in any accepted form |
| `uuidVersion` | `(str s)` | The version nibble (`3`, `4`, `5`, `7`, `0` for nil), or `-1` |
| `uuidVariant` | `(str s)` | `ncs`, `rfc4122`, `microsoft`, `future`, or `""` |
| `uuidTimestamp` | `(str s)` | Unix milliseconds for a time-based UUID, else `-1` |
| `uuidToHex` | `(str s)` | The 16 bytes as 32 lower-case hex characters, or `""` |
| `uuidFromHex` | `(str hex)` | The canonical form of 32 hex characters, or `""` |
| `uuidToBytes` | `(str s)` | The raw 16 bytes, or empty on failure |
| `uuidFromBytes` | `(bytes data)` | The canonical form of 16 bytes, or `""` |
| `uuidNamespace` | `(str name)` | A well-known namespace: `dns`, `url`, `oid` or `x500`, else `""` |

`uuidParse(s)` is the same as `uuidFormat(s, false)`.

## Example

```lynx
global setup(){ import("uuid"); }

global main(){
    str namespace = global.uuid.uuidNamespace("dns");
    println(global.uuid.uuidV3(namespace, "www.example.com"));
    println(global.uuid.uuidV5(namespace, "www.example.com"));
    println(global.uuid.uuidVersion(global.uuid.uuidV7()));
    println(global.uuid.uuidValid("nope"));
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
