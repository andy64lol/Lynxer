# crypto

Hashes, HMAC, constant-time comparison, OS randomness and Ed25519 signatures.

**Backend:** native — `stdlib/crypto.so`, built from the Rust crate `rust/crypto`
over `sha2`, `sha1`, `md-5`, `sha3`, `blake3`, `hmac`, `subtle`, `getrandom` and
`ed25519-dalek`.

**Import:** `import("crypto")` → `global.crypto.*`

## How bytes travel

Binary payloads cross the module ABI as `bytes`:

- a **digest** or **HMAC** is returned as lower-case hex (a text form is the API);
- a **message**, **key**, **signature** or **random payload** is `bytes` — an
  Ed25519 key is the 32 raw bytes, a signature the 64 raw bytes;
- arbitrary file contents go through `hashFile` / `hmacFile`, which read the
  bytes inside the module.

A failure is a scalar sentinel: an unknown algorithm, a malformed payload or an
I/O error yields `""` for a hex output and an empty `bytes` for a binary one,
and a predicate yields `false`.

## Algorithms

`hash` and `hmac` accept a canonical name or the common aliases used by tools and scripts, case-insensitive and with spaces, underscores or dashes ignored. Supported families are:

| Name | Digest | HMAC |
| --- | --- | --- |
| `sha1` / `sha-1` | yes | yes |
| `sha224` / `sha-224` | yes | yes |
| `sha256` / `sha-256` / `sha2-256` | yes | yes |
| `sha384` / `sha-384` | yes | yes |
| `sha512` / `sha-512` / `sha2-512` | yes | yes |
| `sha3-256` / `sha3_256` | yes | yes |
| `sha3-512` / `sha3_512` | yes | yes |
| `blake3` | yes | no |
| `md5` / `md-5` | yes | no |

Any other name yields `""` as an explicit failure sentinel; this is not a valid digest of empty input.

## Randomness

`randomBytes`, `randomHex` and `randomToken` draw from the operating system's
entropy source. The count is capped at 1 MiB; a negative or larger count yields
an empty `bytes` (or `""` for the hex and token forms). These are not reproducible, so a test asserts their shape, never their
value.

## Ed25519

`generateEd25519KeyPair` returns JSON, `{"private": "<base64>", "public":
"<base64>"}`. `signEd25519` takes the private key and `verifyEd25519` the public
key as raw `bytes` (decode the JSON fields with `bytesFromBase64`-style code, or
use the `encoding` module's `base64Decode`). Ed25519 is deterministic, so a fixed
key signs a fixed message to a fixed signature.

## Functions

| Function | Signature | Returns |
| --- | --- | --- |
| `hash` | `(str algorithm, bytes data)` | lower-case hex digest, or `""` |
| `hashFile` | `(str algorithm, str path)` | lower-case hex digest of the file, or `""` |
| `hmac` | `(str algorithm, bytes key, bytes data)` | lower-case hex MAC, or `""` |
| `hmacFile` | `(str algorithm, bytes key, str path)` | lower-case hex MAC of the file, or `""` |
| `verifyHmac` | `(str algorithm, bytes key, bytes data, str mac)` | `true` / `false` (constant-time) |
| `constantTimeEquals` | `(bytes a, bytes b)` | `true` / `false` |
| `randomBytes` | `(int count)` | `bytes`, or empty on failure |
| `randomHex` | `(int count)` | lower-case hex, or `""` |
| `randomToken` | `(int count)` | URL-safe base64 without padding, or `""` |
| `generateEd25519KeyPair` | `()` | JSON `{"private","public"}`, or `""` |
| `signEd25519` | `(bytes privateKey, bytes message)` | signature `bytes`, or empty on failure |
| `verifyEd25519` | `(bytes publicKey, bytes message, bytes signature)` | `true` / `false` |

## Example

```lynx
global setup(){ import("crypto"); import("fileIO"); }

global main(){
    // A SHA-256 digest of "abc", as lower-case hex.
    println(global.crypto.hash("sha256", bytesOf("abc")));

    // An HMAC-SHA256, and its constant-time verification.
    str mac = global.crypto.hmac("sha256", bytesOf("key"), bytesOf("message"));
    println(global.crypto.verifyHmac("sha256", bytesOf("key"), bytesOf("message"), mac));
    println(global.crypto.verifyHmac("sha256", bytesOf("key"), bytesOf("other"), mac));

    // Hash a file's bytes (binary-safe).
    global.fileIO.writeFile("data.bin", "payload");
    println(global.crypto.hashFile("sha256", "data.bin"));

    // A fresh Ed25519 key pair, then sign and verify.
    str pair = global.crypto.generateEd25519KeyPair();
    bytes private = global.encoding.base64Decode(global.json.jsonGet(pair, "private"));
    bytes public = global.encoding.base64Decode(global.json.jsonGet(pair, "public"));
    bytes signature = global.crypto.signEd25519(private, bytesOf("hello"));
    println(global.crypto.verifyEd25519(public, bytesOf("hello"), signature));
}
```

---

## See also

- [Standard library contracts](../stdlib-contracts.md)
- [Built-in functions](../builtins.md)
- [Limitations](../limitations.md)
