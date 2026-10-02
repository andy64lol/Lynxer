# compress

gzip, zlib, zstd, bzip2, brotli and lz4 streams, plus ZIP and TAR archives.

**Backend:** native — `stdlib/compress.so`, built from the Rust crate
`rust/compress` over `flate2`, `zstd`, `bzip2`, `brotli`, `lz4_flex`, `zip` and `tar`.

**Import:** `import("compress")` → `global.compress.*`

## How bytes travel

An in-memory **`<codec>Compress`** takes `bytes` and returns `bytes`, and its
**`<codec>Decompress`** does the same, so a payload that is not valid UTF-8 is
representable. The **`*File`** operations work on paths and read and write the
bytes themselves.

A decompress is capped at **64 MiB** of output, for the in-memory stream
operations and the `*File` operations alike; an LZ4 stream whose declared length
exceeds the cap is refused before it is decoded. Archive extraction is capped
too — a ZIP entry on `zipRead` and `zipExtract`, and a total entry budget on
`tarExtract` and `tarGzExtract` — so a crafted archive cannot exhaust memory or
disk.

## Sentinels

The in-memory stream operations use an empty `bytes` sentinel: a malformed
payload or an unknown stream yields an empty `bytes`. The file and archive operations answer with the
status string `"ok"` / `"ERROR: <message>"`, because they can fail with a
reason.

## Archives

`zipCreate`, `tarCreate` and `tarGzCreate` take a JSON **manifest**: an array of
entries, each `{"name": "<path in archive>", "path": "<source file>"}` or
`{"name": "...", "base64": "<inline data>"}`. Entries may also specify
`"password": "..."` to enable AES-128 encryption for that file. A name that is absolute or contains `..` is rejected when the archive is written.

`zipExtract` and `tarExtract` extract into a directory and refuse any entry
whose name would escape it.

## Functions

| Function | Signature | Returns |
| --- | --- | --- |
| `gzipCompress` | `(bytes data)` | the gzip stream as `bytes` |
| `gzipDecompress` | `(bytes data)` | the payload as `bytes`, or empty on failure |
| `gzipCompressFile` | `(str input, str output)` | `"ok"` / `"ERROR: ..."` |
| `gzipDecompressFile` | `(str input, str output)` | `"ok"` / `"ERROR: ..."` |
| `zlibCompress` / `zlibDecompress` / `zlibCompressFile` / `zlibDecompressFile` | as gzip | as gzip |
| `zstdCompress` / `zstdDecompress` / `zstdCompressFile` / `zstdDecompressFile` | as gzip | as gzip |
| `bzip2Compress` / `bzip2Decompress` / `bzip2CompressFile` / `bzip2DecompressFile` | as gzip | as gzip |
| `brotliCompress` / `brotliDecompress` / `brotliCompressFile` / `brotliDecompressFile` | as gzip | as gzip |
| `lz4Compress` / `lz4Decompress` / `lz4CompressFile` / `lz4DecompressFile` | as gzip | as gzip |
| `zipCreate` | `(str path, str manifest)` | `"ok"` / `"ERROR: ..."` |
| `zipList` | `(str path)` | JSON array of entry names, or `"ERROR: ..."` |
| `zipRead` | `(str path, str entry)` | the entry's bytes, or empty when absent |
| `zipReadWithPassword` | `(str path, str entry, str password)` | the decrypted entry's bytes, or empty when absent or wrong |
| `zipExtract` | `(str path, str directory)` | `"ok"` / `"ERROR: ..."` |
| `zipExtractWithPassword` | `(str path, str directory, str password)` | `"ok"` / `"ERROR: ..."` |
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
    bytes payload = bytesOf(text);

    // In-memory round trip.
    bytes packed = global.compress.gzipCompress(payload);
    println(bytesToStr(global.compress.gzipDecompress(packed)) is text);

    // File round trip (binary-safe).
    global.fileIO.writeFile("data.txt", text);
    println(global.compress.zstdCompressFile("data.txt", "data.zst"));
    println(global.compress.zstdDecompressFile("data.zst", "data.out"));
    println(global.fileIO.readFile("data.out") is text);

    // BZIP2 is supported too, when the dependency is compiled in.
    bytes bzipPack = global.compress.bzip2Compress(bytesOf(text));
    println(bytesToStr(global.compress.bzip2Decompress(bzipPack)) is text);

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
