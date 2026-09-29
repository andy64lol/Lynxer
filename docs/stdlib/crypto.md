# crypto

Hashes, HMAC, constant-time comparison, OS randomness and Ed25519 signatures.

**Backend:** native — `stdlib/crypto.so`, built from the Rust crate `rust/crypto`
over `sha2`, `sha1`, `md-5`, `sha3`, `blake3`, `hmac`, `subtle`, `getrandom` and
`ed25519-dalek`.

**Import:** `import("crypto")` → `global.crypto.*`

## How bytes travel

Lynxer has no byte type, so binary payloads cross the module ABI as text:

- a **digest** or **HMAC** is returned as lower-case hex;
- an **Ed25519 key** and **signature** are base64 (the 32 raw key bytes, and the
  64 raw signature bytes);
- a caller that already holds bytes as base64 passes them to `hashBase64`;
- arbitrary file contents go through `hashFile` / `hmacFile`, which read the
  bytes inside the module and so are not limited to valid UTF-8.

A failure is a scalar sentinel: an unknown algorithm, a malformed payload or an
I/O error yields `""`, and a predicate yields `false`.

## Algorithms

`hash` and `hmac` take an algorithm name from a fixed set:

| Name | Digest | HMAC |
| --- | --- | --- |
| `sha1` | yes | yes |
| `sha224`, `sha256`, `sha384`, `sha512` | yes | yes |
| `sha3-256`, `sha3-512` | yes | yes |
| `blake3` | yes | no |
| `md5` | yes | no |

Any other name yields `""`.

## Randomness

`randomBytes`, `randomHex` and `randomToken` draw from the operating system's
entropy source. The count is capped at 1 MiB; a negative or larger count yields
`""`. These are not reproducible, so a test asserts their shape, never their
value.

## Ed25519

`generateEd25519KeyPair` returns JSON, `{"private": "<base64>", "public":
"<base64>"}`. `signEd25519` takes the private key and `verifyEd25519` the public
key. Ed25519 is deterministic, so a fixed key signs a fixed message to a fixed
signature.

## Functions

| Function | Signature | Returns |
| --- | --- | --- |
| `hash` | `(str algorithm, str data)` | lower-case hex digest, or `""` |
| `hashFile` | `(str algorithm, str path)` | lower-case hex digest of the file, or `""` |
| `hashBase64` | `(str algorithm, str base64Data)` | lower-case hex digest of the decoded bytes, or `""` |
| `hmac` | `(str algorithm, str key, str data)` | lower-case hex MAC, or `""` |
| `hmacFile` | `(str algorithm, str key, str path)` | lower-case hex MAC of the file, or `""` |
| `verifyHmac` | `(str algorithm, str key, str data, str mac)` | `true` / `false` (constant-time) |
| `constantTimeEquals` | `(str a, str b)` | `true` / `false` |
| `randomBytes` | `(int count)` | base64, or `""` |
| `randomHex` | `(int count)` | lower-case hex, or `""` |
| `randomToken` | `(int count)` | URL-safe base64 without padding, or `""` |
| `generateEd25519KeyPair` | `()` | JSON `{"private","public"}`, or `""` |
| `signEd25519` | `(str privateKey, str message)` | base64 signature, or `""` |
| `verifyEd25519` | `(str publicKey, str message, str signature)` | `true` / `false` |

## Example

```lynx
global setup(){ import("crypto"); import("fileIO"); }

global main(){
    // A SHA-256 digest of "abc", as lower-case hex.
    println(global.crypto.hash("sha256", "abc"));

    // An HMAC-SHA256, and its constant-time verification.
    str mac = global.crypto.hmac("sha256", "key", "message");
    println(global.crypto.verifyHmac("sha256", "key", "message", mac));
    println(global.crypto.verifyHmac("sha256", "key", "other", mac));

    // Hash a file's bytes (binary-safe).
    global.fileIO.writeFile("data.bin", "payload");
    println(global.crypto.hashFile("sha256", "data.bin"));

    // A fresh Ed25519 key pair, then sign and verify.
    str pair = global.crypto.generateEd25519KeyPair();
    str private = global.json.jsonGet(pair, "private");
    str public = global.json.jsonGet(pair, "public");
    str signature = global.crypto.signEd25519(private, "hello");
    println(global.crypto.verifyEd25519(public, "hello", signature));
}
```

---

## See also

- [Standard library contracts](../stdlib-contracts.md)
- [Built-in functions](../builtins.md)
- [Limitations](../limitations.md)
