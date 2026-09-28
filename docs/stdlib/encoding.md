# encoding

Base64, hex, base32, base58, ascii85, percent-encoding and quoted-printable
codecs.

**Backend:** native — `stdlib/encoding.so`, built from the Rust crate
`rust/encoding` over [`base64`](https://docs.rs/base64),
[`hex`](https://docs.rs/hex),
[`data-encoding`](https://docs.rs/data-encoding),
[`bs58`](https://docs.rs/bs58), [`ascii85`](https://docs.rs/ascii85),
[`percent-encoding`](https://docs.rs/percent-encoding) and
[`quoted_printable`](https://docs.rs/quoted_printable).
**Import:** `import("encoding")` → `global.encoding.*`

## How bytes travel

A Lynxer string is the byte sequence here. An encode function takes the **UTF-8
bytes** of its input, and a decode function returns the decoded bytes **as
text**. Lynxer has no byte type yet, so a payload that is not valid UTF-8 has no
representation: a decode that produces one returns `""`, the same sentinel a
malformed input returns. `*Valid` reports whether the input is well formed in
its codec — it does not require the decoded bytes to be valid UTF-8, so it can be
`true` while `*Decode` answers `""`.

Test a decode result for `""` rather than expecting an exception: there is no
error channel across the native ABI. See the binary-payload decision in
[todo.md](../../todo.md) for the plans that need bulk binary (`compress`,
`crypto`).

## Conventions

- **Padding** is optional when decoding and always emitted when encoding, for
  base64 and base32 alike. `base32` decoding also accepts either case.
- **Whitespace is never accepted** inside an encoded payload.
- A **malformed `%` escape** is a failure rather than a passthrough: the
  underlying decoder would leave `%2` alone, so the module validates the escapes
  itself.
- `ascii85` output includes the Adobe `<~`/`~>` frame; decoding accepts the
  frame or a bare stream, ignores whitespace, and understands the `z` shorthand
  for four zero bytes.
- `quotedPrintable` is the text encoding of RFC 2045: `=` is escaped, `CR`/`LF`
  become `=0D`/`=0A`, and long lines gain soft breaks that decoding removes.

## Functions

| Function | Signature | Returns |
| --- | --- | --- |
| `base64Encode` | `(str s)` | Standard base64, padded |
| `base64Decode` | `(str s)` | Decoded text, or `""` |
| `base64Valid` | `(str s)` | `true` if `s` is well-formed standard base64 |
| `base64UrlEncode` | `(str s)` | URL-safe base64 (`-`/`_`), padded |
| `base64UrlDecode` | `(str s)` | Decoded text, or `""` |
| `base64UrlValid` | `(str s)` | `true` if `s` is well-formed URL-safe base64 |
| `hexEncode` | `(str s)` | Lower-case hexadecimal |
| `hexEncodeUpper` | `(str s)` | Upper-case hexadecimal |
| `hexDecode` | `(str s)` | Decoded text, or `""` |
| `hexValid` | `(str s)` | `true` if `s` is hexadecimal with an even length |
| `base32Encode` | `(str s)` | Upper-case RFC 4648 base32, padded |
| `base32Decode` | `(str s)` | Decoded text, or `""` |
| `base32Valid` | `(str s)` | `true` if `s` is well-formed base32 |
| `base58Encode` | `(str s)` | Base58 (the Bitcoin alphabet) |
| `base58Decode` | `(str s)` | Decoded text, or `""` |
| `base58Valid` | `(str s)` | `true` if `s` is well-formed base58 |
| `ascii85Encode` | `(str s)` | Adobe ascii85, framed with `<~` `~>` |
| `ascii85Decode` | `(str s)` | Decoded text, or `""` |
| `ascii85Valid` | `(str s)` | `true` if `s` is well-formed ascii85 |
| `percentEncode` | `(str s)` | Percent-encoded URI component |
| `percentDecode` | `(str s)` | Decoded text, or `""` |
| `percentValid` | `(str s)` | `true` if every `%` is followed by two hex digits |
| `quotedPrintableEncode` | `(str s)` | Quoted-printable text |
| `quotedPrintableDecode` | `(str s)` | Decoded text, or `""` |
| `quotedPrintableValid` | `(str s)` | `true` if `s` is well-formed quoted-printable |

`percentEncode` uses `encodeURIComponent` semantics: `A-Z a-z 0-9 - _ . ! ~ *
' ( )` stay unescaped and everything else, including `/ ? = &`, is escaped.

## Example

```lynx
global setup(){ import("encoding"); }

global main(){
    println(global.encoding.base64Encode("Lynxer"));
    println(global.encoding.base64Decode("THlueA"));   // padding is optional
    println(global.encoding.hexEncode("Lynxer"));
    println(global.encoding.percentEncode("a b/c?d=e&f"));
    println(global.encoding.percentValid("%2"));        // truncated escape
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
