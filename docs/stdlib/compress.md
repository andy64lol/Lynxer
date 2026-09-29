# compress

gzip, zlib, zstd, brotli and lz4 streams, plus ZIP and TAR archives.

**Backend:** native — `stdlib/compress.so`, built from the Rust crate
`rust/compress` over `flate2`, `zstd`, `brotli`, `lz4_flex`, `zip` and `tar`.

**Import:** `import("compress")` → `global.compress.*`

## How bytes travel

Lynxer has no byte type, so an in-memory **`<codec>Compress`** returns base64 and
its **`<codec>Decompress`** takes base64. A decompressed payload is not
necessarily UTF-8, so `Decompress` returns text only when it is and `""`
otherwise. The **`*File`** operations read and write the bytes themselves and
are the binary-safe path for arbitrary data.

A decompress is capped at **64 MiB** of output, for the in-memory stream
operations and the `*File` operations alike; an LZ4 stream whose declared length
exceeds the cap is refused before it is decoded. Archive extraction is capped
too — a ZIP entry on `zipRead` and `zipExtract`, and a total entry budget on
`tarExtract` and `tarGzExtract` — so a crafted archive cannot exhaust memory or
disk.

## Sentinels

The in-memory stream operations use scalar sentinels: a malformed payload or an
unknown stream yields `""`. The file and archive operations answer with the
status string `"ok"` / `"ERROR: <message>"`, because they can fail with a
reason.

## Archives

`zipCreate`, `tarCreate` and `tarGzCreate` take a JSON **manifest**: an array of
entries, each `{"name": "<path in archive>", "path": "<source file>"}` or
`{"name": "...", "base64": "<inline data>"}`. A name that is absolute or
contains `..` is rejected when the archive is written.

`zipExtract` and `tarExtract` extract into a directory and refuse any entry
whose name would escape it.

## Functions

| Function | Signature | Returns |
| --- | --- | --- |
| `gzipCompress` | `(str text)` | base64 gzip stream |
| `gzipDecompress` | `(str base64Data)` | text, or `""` |
| `gzipCompressFile` | `(str input, str output)` | `"ok"` / `"ERROR: ..."` |
| `gzipDecompressFile` | `(str input, str output)` | `"ok"` / `"ERROR: ..."` |
| `zlibCompress` / `zlibDecompress` / `zlibCompressFile` / `zlibDecompressFile` | as gzip | as gzip |
| `zstdCompress` / `zstdDecompress` / `zstdCompressFile` / `zstdDecompressFile` | as gzip | as gzip |
| `brotliCompress` / `brotliDecompress` / `brotliCompressFile` / `brotliDecompressFile` | as gzip | as gzip |
| `lz4Compress` / `lz4Decompress` / `lz4CompressFile` / `lz4DecompressFile` | as gzip | as gzip |
| `zipCreate` | `(str path, str manifest)` | `"ok"` / `"ERROR: ..."` |
| `zipList` | `(str path)` | JSON array of entry names, or `"ERROR: ..."` |
| `zipRead` | `(str path, str entry)` | base64, or `""` |
| `zipExtract` | `(str path, str directory)` | `"ok"` / `"ERROR: ..."` |
| `tarCreate` | `(str path, str manifest)` | `"ok"` / `"ERROR: ..."` |
| `tarList` | `(str path)` | JSON array of entry names, or `"ERROR: ..."` |
| `tarExtract` | `(str path, str directory)` | `"ok"` / `"ERROR: ..."` |
| `tarGzCreate` | `(str path, str manifest)` | `"ok"` / `"ERROR: ..."` |
| `tarGzExtract` | `(str path, str directory)` | `"ok"` / `"ERROR: ..."` |

## Example

```lynx
global setup(){ import("compress"); import("fileIO"); }

global main(){
    str text = "the quick brown fox jumps over the lazy dog";

    // In-memory round trip, through base64.
    str packed = global.compress.gzipCompress(text);
    println(global.compress.gzipDecompress(packed) is text);

    // File round trip (binary-safe).
    global.fileIO.writeFile("data.txt", text);
    println(global.compress.zstdCompressFile("data.txt", "data.zst"));
    println(global.compress.zstdDecompressFile("data.zst", "data.out"));
    println(global.fileIO.readFile("data.out") is text);

    // Build a ZIP containing a file and an inline entry.
    str manifest = "[{\"name\":\"a.txt\",\"path\":\"data.txt\"},{\"name\":\"b.txt\",\"base64\":\"aGVsbG8=\"}]";
    println(global.compress.zipCreate("archive.zip", manifest));
    println(global.compress.zipList("archive.zip"));
    println(global.compress.zipExtract("archive.zip", "unpacked"));
}
```

---

## See also

- [Standard library contracts](../stdlib-contracts.md)
- [Built-in functions](../builtins.md)
- [Limitations](../limitations.md)
