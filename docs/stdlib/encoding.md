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

The encoded form of a payload is always text and the raw form is always a
`bytes` value: an **encode** takes `bytes` and returns a `str`, a **decode**
takes a `str` and returns `bytes`. A payload that is not valid UTF-8 is therefore
representable — a decode that used to yield `""` now returns those bytes — so a
decode failure is an empty `bytes`. `*Valid` reports whether the input is well
formed in its codec.

Test a decode with `bytesLength(result) is 0` rather than expecting an exception:
there is no error channel across the native ABI.

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
| `base64Encode` | `(bytes b)` | Standard base64, padded |
| `base64Decode` | `(str s)` | the decoded `bytes`, or empty on failure |
| `base64Valid` | `(str s)` | `true` if `s` is well-formed standard base64 |
| `base64UrlEncode` | `(bytes b)` | URL-safe base64 (`-`/`_`), padded |
| `base64UrlDecode` | `(str s)` | the decoded `bytes`, or empty on failure |
| `base64UrlValid` | `(str s)` | `true` if `s` is well-formed URL-safe base64 |
| `hexEncode` | `(bytes b)` | Lower-case hexadecimal |
| `hexEncodeUpper` | `(bytes b)` | Upper-case hexadecimal |
| `hexDecode` | `(str s)` | the decoded `bytes`, or empty on failure |
| `hexValid` | `(str s)` | `true` if `s` is hexadecimal with an even length |
| `base32Encode` | `(bytes b)` | Upper-case RFC 4648 base32, padded |
| `base32Decode` | `(str s)` | the decoded `bytes`, or empty on failure |
| `base32Valid` | `(str s)` | `true` if `s` is well-formed base32 |
| `base58Encode` | `(bytes b)` | Base58 (the Bitcoin alphabet) |
| `base58Decode` | `(str s)` | the decoded `bytes`, or empty on failure |
| `base58Valid` | `(str s)` | `true` if `s` is well-formed base58 |
| `ascii85Encode` | `(bytes b)` | Adobe ascii85, framed with `<~` `~>` |
| `ascii85Decode` | `(str s)` | the decoded `bytes`, or empty on failure |
| `ascii85Valid` | `(str s)` | `true` if `s` is well-formed ascii85 |
| `percentEncode` | `(bytes b)` | Percent-encoded URI component |
| `percentDecode` | `(str s)` | the decoded `bytes`, or empty on failure |
| `percentValid` | `(str s)` | `true` if every `%` is followed by two hex digits |
| `quotedPrintableEncode` | `(bytes b)` | Quoted-printable text |
| `quotedPrintableDecode` | `(str s)` | the decoded `bytes`, or empty on failure |
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
