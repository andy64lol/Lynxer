//! Lynxer `compress` stdlib backend: gzip/zlib/zstd/brotli/lz4 streams plus ZIP
//! and TAR archives.
//!
//! Binary payloads cross the module ABI as text: an in-memory `*Compress`
//! returns base64 and its `*Decompress` takes base64. Because a decompressed
//! payload may not be valid UTF-8, the in-memory `*Decompress` returns text only
//! for UTF-8 output (`""` otherwise); the `*File` ops read and write the bytes
//! themselves and are the binary-safe path.
//!
//! Stream ops use scalar sentinels (`""`); file and archive ops answer with the
//! status string `"ok"` / `"ERROR: <message>"` because they can fail for many
//! reasons. An archive extract refuses an entry that escapes its destination.

use std::fs::File;
use std::io::{Read, Write};
use std::path::Component;

use base64::engine::general_purpose::STANDARD;
use base64::Engine as _;
use lynxer_abi::{export_string, lynxer_module};
use serde::Deserialize;

/// The largest payload an in-memory decompress will produce, so a crafted input
/// cannot exhaust memory.
const MAX_DECOMPRESSED: usize = 64 << 20;

#[derive(Clone, Copy)]
enum Codec {
    Gzip,
    Zlib,
    Zstd,
    Brotli,
    Lz4,
}

fn error_text(message: &str) -> String {
    format!("ERROR: {message}")
}

fn read_limited<R: Read>(mut reader: R) -> Result<Vec<u8>, String> {
    let mut buffer = Vec::new();
    let mut chunk = [0u8; 16384];
    loop {
        let count = reader.read(&mut chunk).map_err(|error| error.to_string())?;
        if count == 0 {
            break;
        }
        if buffer.len() + count > MAX_DECOMPRESSED {
            return Err("decompressed data exceeds the size limit".to_string());
        }
        buffer.extend_from_slice(&chunk[..count]);
    }
    Ok(buffer)
}

/// A `Read` adapter that fails once more than `MAX_DECOMPRESSED` bytes have been
/// read, so a decompressed archive cannot expand without bound. Archive
/// extraction streams entry data to disk, so it needs a limit on the reader
/// rather than on a buffer.
struct Limited<R> {
    inner: R,
    remaining: usize,
}

impl<R: Read> Limited<R> {
    fn new(inner: R) -> Self {
        Self {
            inner,
            remaining: MAX_DECOMPRESSED,
        }
    }
}

impl<R: Read> Read for Limited<R> {
    fn read(&mut self, buffer: &mut [u8]) -> std::io::Result<usize> {
        if self.remaining == 0 {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidData,
                "decompressed data exceeds the size limit",
            ));
        }
        let cap = buffer.len().min(self.remaining);
        let count = self.inner.read(&mut buffer[..cap])?;
        self.remaining -= count;
        Ok(count)
    }
}

/// LZ4 streams carry their uncompressed length in a 4-byte little-endian
/// prefix. Read that prefix before handing the data to the decoder, so a
/// crafted length cannot make it allocate an unbounded buffer.
fn decompress_lz4(data: &[u8]) -> Result<Vec<u8>, String> {
    let prefix = data
        .get(..4)
        .ok_or_else(|| "lz4 stream is truncated".to_string())?;
    let declared = u32::from_le_bytes(prefix.try_into().unwrap()) as usize;
    if declared > MAX_DECOMPRESSED {
        return Err("decompressed data exceeds the size limit".to_string());
    }
    let bytes = lz4_flex::decompress_size_prepended(data).map_err(|error| error.to_string())?;
    if bytes.len() > MAX_DECOMPRESSED {
        return Err("decompressed data exceeds the size limit".to_string());
    }
    Ok(bytes)
}

fn compress(codec: Codec, data: &[u8]) -> Result<Vec<u8>, String> {
    match codec {
        Codec::Gzip => {
            let mut encoder =
                flate2::write::GzEncoder::new(Vec::new(), flate2::Compression::default());
            encoder.write_all(data).map_err(|e| e.to_string())?;
            encoder.finish().map_err(|e| e.to_string())
        }
        Codec::Zlib => {
            let mut encoder =
                flate2::write::ZlibEncoder::new(Vec::new(), flate2::Compression::default());
            encoder.write_all(data).map_err(|e| e.to_string())?;
            encoder.finish().map_err(|e| e.to_string())
        }
        Codec::Zstd => zstd::stream::encode_all(data, 3).map_err(|e| e.to_string()),
        Codec::Brotli => {
            let mut writer = brotli::CompressorWriter::new(Vec::new(), 4096, 5, 22);
            writer.write_all(data).map_err(|e| e.to_string())?;
            Ok(writer.into_inner())
        }
        Codec::Lz4 => Ok(lz4_flex::compress_prepend_size(data)),
    }
}

fn decompress(codec: Codec, data: &[u8]) -> Result<Vec<u8>, String> {
    match codec {
        Codec::Gzip => read_limited(flate2::read::GzDecoder::new(data)),
        Codec::Zlib => read_limited(flate2::read::ZlibDecoder::new(data)),
        Codec::Zstd => {
            let decoder = zstd::stream::read::Decoder::new(data).map_err(|e| e.to_string())?;
            read_limited(decoder)
        }
        Codec::Brotli => read_limited(brotli::Decompressor::new(data, 4096)),
        Codec::Lz4 => decompress_lz4(data),
    }
}

fn compress_in_memory(codec: Codec, text: &str) -> String {
    match compress(codec, text.as_bytes()) {
        Ok(bytes) => STANDARD.encode(bytes),
        Err(_) => String::new(),
    }
}

fn decompress_in_memory(codec: Codec, encoded: &str) -> String {
    let bytes = match STANDARD.decode(encoded) {
        Ok(bytes) => bytes,
        Err(_) => return String::new(),
    };
    match decompress(codec, &bytes) {
        Ok(bytes) => String::from_utf8(bytes).unwrap_or_default(),
        Err(_) => String::new(),
    }
}

fn compress_file(codec: Codec, input: &str, output: &str) -> String {
    let data = match std::fs::read(input) {
        Ok(data) => data,
        Err(error) => return error_text(&format!("cannot read {input}: {error}")),
    };
    match compress(codec, &data) {
        Ok(bytes) => match std::fs::write(output, bytes) {
            Ok(()) => "ok".to_string(),
            Err(error) => error_text(&format!("cannot write {output}: {error}")),
        },
        Err(error) => error_text(&error),
    }
}

fn decompress_file(codec: Codec, input: &str, output: &str) -> String {
    let data = match std::fs::read(input) {
        Ok(data) => data,
        Err(error) => return error_text(&format!("cannot read {input}: {error}")),
    };
    match decompress(codec, &data) {
        Ok(bytes) => match std::fs::write(output, bytes) {
            Ok(()) => "ok".to_string(),
            Err(error) => error_text(&format!("cannot write {output}: {error}")),
        },
        Err(error) => error_text(&error),
    }
}

// --- archives ---------------------------------------------------------------

/// One entry in a `zipCreate` / `tarCreate` / `tarGzCreate` manifest: a name,
/// plus either a source `path` or inline `base64` data.
#[derive(Deserialize)]
struct Entry {
    name: String,
    #[serde(default)]
    path: Option<String>,
    #[serde(default)]
    base64: Option<String>,
}

impl Entry {
    fn data(&self) -> Result<Vec<u8>, String> {
        if let Some(path) = &self.path {
            std::fs::read(path).map_err(|error| format!("cannot read {path}: {error}"))
        } else if let Some(encoded) = &self.base64 {
            STANDARD
                .decode(encoded)
                .map_err(|_| "entry base64 is malformed".to_string())
        } else {
            Ok(Vec::new())
        }
    }
}

fn parse_manifest(manifest: &str) -> Result<Vec<Entry>, String> {
    serde_json::from_str(manifest).map_err(|error| format!("invalid manifest: {error}"))
}

/// True when a name is a relative path with no `..` component, so an archive
/// entry cannot escape the destination directory.
fn is_safe_name(name: &str) -> bool {
    if name.is_empty() {
        return false;
    }
    let path = std::path::Path::new(name);
    !path.is_absolute()
        && path
            .components()
            .all(|part| matches!(part, Component::Normal(_) | Component::CurDir))
}

fn zip_create(path: &str, manifest: &str) -> String {
    let entries = match parse_manifest(manifest) {
        Ok(entries) => entries,
        Err(error) => return error_text(&error),
    };
    let file = match File::create(path) {
        Ok(file) => file,
        Err(error) => return error_text(&format!("cannot write {path}: {error}")),
    };
    let mut writer = zip::ZipWriter::new(file);
    let options = zip::write::SimpleFileOptions::default()
        .compression_method(zip::CompressionMethod::Deflated);
    for entry in entries {
        if !is_safe_name(&entry.name) {
            return error_text(&format!("unsafe entry name: {}", entry.name));
        }
        let data = match entry.data() {
            Ok(data) => data,
            Err(error) => return error_text(&error),
        };
        if let Err(error) = writer.start_file(entry.name.clone(), options) {
            return error_text(&error.to_string());
        }
        if let Err(error) = writer.write_all(&data) {
            return error_text(&error.to_string());
        }
    }
    match writer.finish() {
        Ok(_) => "ok".to_string(),
        Err(error) => error_text(&error.to_string()),
    }
}

fn zip_open(path: &str) -> Result<zip::ZipArchive<File>, String> {
    let file = File::open(path).map_err(|error| format!("cannot read {path}: {error}"))?;
    zip::ZipArchive::new(file).map_err(|error| error.to_string())
}

fn zip_list(path: &str) -> String {
    match zip_open(path) {
        Ok(archive) => {
            let names: Vec<String> = archive.file_names().map(String::from).collect();
            serde_json::to_string(&names).unwrap_or_default()
        }
        Err(error) => error_text(&error),
    }
}

fn zip_read(path: &str, entry: &str) -> String {
    match zip_open(path).and_then(|mut archive| {
        let file = archive.by_name(entry).map_err(|error| error.to_string())?;
        read_limited(file)
    }) {
        Ok(bytes) => STANDARD.encode(bytes),
        Err(_) => String::new(),
    }
}

fn zip_extract(path: &str, directory: &str) -> String {
    let mut archive = match zip_open(path) {
        Ok(archive) => archive,
        Err(error) => return error_text(&error),
    };
    if let Err(error) = std::fs::create_dir_all(directory) {
        return error_text(&format!("cannot create {directory}: {error}"));
    }
    for index in 0..archive.len() {
        let mut file = match archive.by_index(index) {
            Ok(file) => file,
            Err(error) => return error_text(&error.to_string()),
        };
        let relative = match file.enclosed_name() {
            Some(relative) => relative.to_path_buf(),
            None => return error_text(&format!("unsafe entry name: {}", file.name())),
        };
        let destination = std::path::Path::new(directory).join(relative);
        if file.is_dir() {
            if let Err(error) = std::fs::create_dir_all(&destination) {
                return error_text(&error.to_string());
            }
            continue;
        }
        if let Some(parent) = destination.parent() {
            if let Err(error) = std::fs::create_dir_all(parent) {
                return error_text(&error.to_string());
            }
        }
        // Refuse an entry that declares or yields more than the limit, so a
        // crafted archive cannot exhaust memory or disk.
        if file.size() > MAX_DECOMPRESSED as u64 {
            return error_text(&format!(
                "decompressed data exceeds the size limit: {}",
                file.name()
            ));
        }
        let data = match read_limited(&mut file) {
            Ok(data) => data,
            Err(error) => return error_text(&error),
        };
        if let Err(error) = std::fs::write(&destination, data) {
            return error_text(&error.to_string());
        }
    }
    "ok".to_string()
}

fn tar_append<W: Write>(builder: &mut tar::Builder<W>, manifest: &str) -> Result<(), String> {
    let entries = parse_manifest(manifest)?;
    for entry in entries {
        if !is_safe_name(&entry.name) {
            return Err(format!("unsafe entry name: {}", entry.name));
        }
        let data = entry.data()?;
        let mut header = tar::Header::new_gnu();
        header.set_size(data.len() as u64);
        header.set_mode(0o644);
        header.set_cksum();
        builder
            .append_data(&mut header, &entry.name, data.as_slice())
            .map_err(|error| error.to_string())?;
    }
    Ok(())
}

fn tar_create(path: &str, manifest: &str) -> String {
    let file = match File::create(path) {
        Ok(file) => file,
        Err(error) => return error_text(&format!("cannot write {path}: {error}")),
    };
    let mut builder = tar::Builder::new(file);
    if let Err(error) = tar_append(&mut builder, manifest) {
        return error_text(&error);
    }
    match builder.finish() {
        Ok(()) => "ok".to_string(),
        Err(error) => error_text(&error.to_string()),
    }
}

fn tar_gz_create(path: &str, manifest: &str) -> String {
    let file = match File::create(path) {
        Ok(file) => file,
        Err(error) => return error_text(&format!("cannot write {path}: {error}")),
    };
    let encoder = flate2::write::GzEncoder::new(file, flate2::Compression::default());
    let mut builder = tar::Builder::new(encoder);
    if let Err(error) = tar_append(&mut builder, manifest) {
        return error_text(&error);
    }
    match builder.into_inner() {
        Ok(encoder) => match encoder.finish() {
            Ok(_) => "ok".to_string(),
            Err(error) => error_text(&error.to_string()),
        },
        Err(error) => error_text(&error.to_string()),
    }
}

fn tar_list(path: &str) -> String {
    let file = match File::open(path) {
        Ok(file) => file,
        Err(error) => return error_text(&format!("cannot read {path}: {error}")),
    };
    let mut archive = tar::Archive::new(file);
    let entries = match archive.entries() {
        Ok(entries) => entries,
        Err(error) => return error_text(&error.to_string()),
    };
    let mut names = Vec::new();
    for entry in entries {
        match entry {
            Ok(entry) => match entry.path() {
                Ok(path) => names.push(path.to_string_lossy().into_owned()),
                Err(error) => return error_text(&error.to_string()),
            },
            Err(error) => return error_text(&error.to_string()),
        }
    }
    serde_json::to_string(&names).unwrap_or_default()
}

fn tar_extract<R: Read>(archive: tar::Archive<R>, directory: &str) -> String {
    if let Err(error) = std::fs::create_dir_all(directory) {
        return error_text(&format!("cannot create {directory}: {error}"));
    }
    let mut archive = archive;
    let entries = match archive.entries() {
        Ok(entries) => entries,
        Err(error) => return error_text(&error.to_string()),
    };
    let mut remaining = MAX_DECOMPRESSED as u64;
    for entry in entries {
        let mut entry = match entry {
            Ok(entry) => entry,
            Err(error) => return error_text(&error.to_string()),
        };
        // Refuse an archive whose entries declare more than the limit in total,
        // so extraction cannot exhaust the disk. (The reader is capped too, as a
        // backstop for a compressed archive.)
        let size = entry.size();
        if size > remaining {
            return error_text("decompressed data exceeds the size limit");
        }
        remaining -= size;
        // `unpack_in` refuses a path that escapes the destination.
        match entry.unpack_in(directory) {
            Ok(true) => {}
            Ok(false) => {
                return error_text(&format!(
                    "unsafe entry name: {}",
                    entry.path().unwrap_or_default().display()
                ))
            }
            Err(error) => return error_text(&error.to_string()),
        }
    }
    "ok".to_string()
}

fn tar_extract_plain(path: &str, directory: &str) -> String {
    match File::open(path) {
        Ok(file) => tar_extract(tar::Archive::new(file), directory),
        Err(error) => error_text(&format!("cannot read {path}: {error}")),
    }
}

fn tar_gz_extract(path: &str, directory: &str) -> String {
    match File::open(path) {
        // A gzip-compressed tar can expand far beyond its size on disk, so cap
        // the decompressed stream as it is read.
        Ok(file) => tar_extract(
            tar::Archive::new(Limited::new(flate2::read::GzDecoder::new(file))),
            directory,
        ),
        Err(error) => error_text(&format!("cannot read {path}: {error}")),
    }
}

// --- ops --------------------------------------------------------------------

export_string!(compress_gzip, args, {
    compress_in_memory(Codec::Gzip, args.string(0))
});
export_string!(compress_gunzip, args, {
    decompress_in_memory(Codec::Gzip, args.string(0))
});
export_string!(compress_gzip_file, args, {
    compress_file(Codec::Gzip, args.string(0), args.string(1))
});
export_string!(compress_gunzip_file, args, {
    decompress_file(Codec::Gzip, args.string(0), args.string(1))
});

export_string!(compress_zlib, args, {
    compress_in_memory(Codec::Zlib, args.string(0))
});
export_string!(compress_unzlib, args, {
    decompress_in_memory(Codec::Zlib, args.string(0))
});
export_string!(compress_zlib_file, args, {
    compress_file(Codec::Zlib, args.string(0), args.string(1))
});
export_string!(compress_unzlib_file, args, {
    decompress_file(Codec::Zlib, args.string(0), args.string(1))
});

export_string!(compress_zstd, args, {
    compress_in_memory(Codec::Zstd, args.string(0))
});
export_string!(compress_unzstd, args, {
    decompress_in_memory(Codec::Zstd, args.string(0))
});
export_string!(compress_zstd_file, args, {
    compress_file(Codec::Zstd, args.string(0), args.string(1))
});
export_string!(compress_unzstd_file, args, {
    decompress_file(Codec::Zstd, args.string(0), args.string(1))
});

export_string!(compress_brotli, args, {
    compress_in_memory(Codec::Brotli, args.string(0))
});
export_string!(compress_unbrotli, args, {
    decompress_in_memory(Codec::Brotli, args.string(0))
});
export_string!(compress_brotli_file, args, {
    compress_file(Codec::Brotli, args.string(0), args.string(1))
});
export_string!(compress_unbrotli_file, args, {
    decompress_file(Codec::Brotli, args.string(0), args.string(1))
});

export_string!(compress_lz4, args, {
    compress_in_memory(Codec::Lz4, args.string(0))
});
export_string!(compress_unlz4, args, {
    decompress_in_memory(Codec::Lz4, args.string(0))
});
export_string!(compress_lz4_file, args, {
    compress_file(Codec::Lz4, args.string(0), args.string(1))
});
export_string!(compress_unlz4_file, args, {
    decompress_file(Codec::Lz4, args.string(0), args.string(1))
});

export_string!(compress_zip_create, args, {
    zip_create(args.string(0), args.string(1))
});
export_string!(compress_zip_list, args, { zip_list(args.string(0)) });
export_string!(compress_zip_read, args, {
    zip_read(args.string(0), args.string(1))
});
export_string!(compress_zip_extract, args, {
    zip_extract(args.string(0), args.string(1))
});

export_string!(compress_tar_create, args, {
    tar_create(args.string(0), args.string(1))
});
export_string!(compress_tar_list, args, { tar_list(args.string(0)) });
export_string!(compress_tar_extract, args, {
    tar_extract_plain(args.string(0), args.string(1))
});

export_string!(compress_tar_gz_create, args, {
    tar_gz_create(args.string(0), args.string(1))
});
export_string!(compress_tar_gz_extract, args, {
    tar_gz_extract(args.string(0), args.string(1))
});

const OPS: &[(&str, &str, &str)] = &[
    ("gzipCompress", "compress_gzip", "cdecl:cstring(...)"),
    ("gzipDecompress", "compress_gunzip", "cdecl:cstring(...)"),
    (
        "gzipCompressFile",
        "compress_gzip_file",
        "cdecl:cstring(...)",
    ),
    (
        "gzipDecompressFile",
        "compress_gunzip_file",
        "cdecl:cstring(...)",
    ),
    ("zlibCompress", "compress_zlib", "cdecl:cstring(...)"),
    ("zlibDecompress", "compress_unzlib", "cdecl:cstring(...)"),
    (
        "zlibCompressFile",
        "compress_zlib_file",
        "cdecl:cstring(...)",
    ),
    (
        "zlibDecompressFile",
        "compress_unzlib_file",
        "cdecl:cstring(...)",
    ),
    ("zstdCompress", "compress_zstd", "cdecl:cstring(...)"),
    ("zstdDecompress", "compress_unzstd", "cdecl:cstring(...)"),
    (
        "zstdCompressFile",
        "compress_zstd_file",
        "cdecl:cstring(...)",
    ),
    (
        "zstdDecompressFile",
        "compress_unzstd_file",
        "cdecl:cstring(...)",
    ),
    ("brotliCompress", "compress_brotli", "cdecl:cstring(...)"),
    (
        "brotliDecompress",
        "compress_unbrotli",
        "cdecl:cstring(...)",
    ),
    (
        "brotliCompressFile",
        "compress_brotli_file",
        "cdecl:cstring(...)",
    ),
    (
        "brotliDecompressFile",
        "compress_unbrotli_file",
        "cdecl:cstring(...)",
    ),
    ("lz4Compress", "compress_lz4", "cdecl:cstring(...)"),
    ("lz4Decompress", "compress_unlz4", "cdecl:cstring(...)"),
    ("lz4CompressFile", "compress_lz4_file", "cdecl:cstring(...)"),
    (
        "lz4DecompressFile",
        "compress_unlz4_file",
        "cdecl:cstring(...)",
    ),
    ("zipCreate", "compress_zip_create", "cdecl:cstring(...)"),
    ("zipList", "compress_zip_list", "cdecl:cstring(...)"),
    ("zipRead", "compress_zip_read", "cdecl:cstring(...)"),
    ("zipExtract", "compress_zip_extract", "cdecl:cstring(...)"),
    ("tarCreate", "compress_tar_create", "cdecl:cstring(...)"),
    ("tarList", "compress_tar_list", "cdecl:cstring(...)"),
    ("tarExtract", "compress_tar_extract", "cdecl:cstring(...)"),
    (
        "tarGzCreate",
        "compress_tar_gz_create",
        "cdecl:cstring(...)",
    ),
    (
        "tarGzExtract",
        "compress_tar_gz_extract",
        "cdecl:cstring(...)",
    ),
];

lynxer_module!(OPS);

#[cfg(test)]
mod tests {
    use super::*;

    fn round_trip(codec: Codec) {
        let data = b"the quick brown fox the quick brown fox the quick brown fox";
        let compressed = compress(codec, data).unwrap();
        assert_eq!(decompress(codec, &compressed).unwrap(), data);
    }

    #[test]
    fn every_codec_round_trips() {
        round_trip(Codec::Gzip);
        round_trip(Codec::Zlib);
        round_trip(Codec::Zstd);
        round_trip(Codec::Brotli);
        round_trip(Codec::Lz4);
    }

    #[test]
    fn unsafe_names_are_rejected() {
        assert!(is_safe_name("a/b.txt"));
        assert!(!is_safe_name("../escape"));
        assert!(!is_safe_name("/absolute"));
        assert!(!is_safe_name(""));
    }
}
