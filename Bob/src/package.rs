use std::{
    env, fs,
    io::{Cursor, Write},
    path::{Component, Path},
};

use serde::Deserialize;
use ureq::Agent;
use zip::{write::SimpleFileOptions, CompressionMethod, ZipWriter};

const MAX_ARCHIVE_BYTES: usize = 9_999_999;
const MAX_UNPACKED_BYTES: u64 = 128 * 1024 * 1024;
const MAX_SOURCE_FILES: usize = 1023;
const MAX_MANIFEST_BYTES: usize = 64 * 1024;

#[derive(Debug, Deserialize)]
struct PublishResponse {
    name: String,
    version: String,
    sha256: String,
}

pub fn publish_current_module() -> Result<String, String> {
    let registry = env::var("BOB_REGISTRY_URL")
        .map_err(|_| "BOB_REGISTRY_URL is required for publishing".to_string())?;
    let token = env::var("BOB_PUBLISH_TOKEN")
        .map_err(|_| "BOB_PUBLISH_TOKEN is required for publishing".to_string())?;
    if token.trim().is_empty() {
        return Err("BOB_PUBLISH_TOKEN must not be empty".to_string());
    }
    let registry = registry.trim_end_matches('/');
    let parsed_registry = url::Url::parse(registry)
        .map_err(|_| "BOB_REGISTRY_URL must be a valid URL".to_string())?;
    if !matches!(parsed_registry.scheme(), "http" | "https") {
        return Err("BOB_REGISTRY_URL must use HTTP or HTTPS".to_string());
    }
    if parsed_registry.scheme() == "http"
        && !matches!(
            parsed_registry.host_str(),
            Some("localhost" | "127.0.0.1" | "::1")
        )
    {
        return Err("BOB_REGISTRY_URL must use HTTPS except for local development".to_string());
    }

    let (name, version, archive) = archive_current_module()?;
    if archive.len() > MAX_ARCHIVE_BYTES {
        return Err("module archive must be below 10 MB".to_string());
    }
    let endpoint = format!(
        "{registry}/v1/modules/{}/{}",
        name,
        version.replace('+', "%2B")
    );
    let agent_config = Agent::config_builder().http_status_as_error(false).build();
    let agent = Agent::new_with_config(agent_config);
    let mut response = agent
        .put(&endpoint)
        .header("Authorization", &format!("Bearer {token}"))
        .header("Content-Type", "application/zip")
        .send(archive.as_slice())
        .map_err(|_| "could not reach the Bob registry".to_string())?;
    let status = response.status().as_u16();
    let response_body = response
        .body_mut()
        .read_to_vec()
        .map_err(|_| "could not read the Bob registry response".to_string())?;
    if !(200..300).contains(&status) {
        let message = serde_json::from_slice::<serde_json::Value>(&response_body)
            .ok()
            .and_then(|value| {
                value
                    .get("error")
                    .and_then(serde_json::Value::as_str)
                    .map(str::to_owned)
            })
            .unwrap_or_else(|| format!("registry returned HTTP {status}"));
        return Err(message);
    }
    let published: PublishResponse = serde_json::from_slice(&response_body)
        .map_err(|_| "registry returned an invalid publish response".to_string())?;
    Ok(format!(
        "Published {}@{} (sha256:{})",
        published.name, published.version, published.sha256
    ))
}

fn archive_current_module() -> Result<(String, String, Vec<u8>), String> {
    let root =
        env::current_dir().map_err(|error| format!("cannot read current directory: {error}"))?;
    let manifest_path = root.join("module.toml");
    let manifest = fs::read_to_string(&manifest_path)
        .map_err(|_| "module.toml not found; run bob --init --module first".to_string())?;
    if manifest.len() > MAX_MANIFEST_BYTES {
        return Err("module.toml exceeds the 64 KiB registry limit".to_string());
    }
    let (name, version, entry) = parse_module_manifest(&manifest)?;
    let entry_path = root.join(&entry);
    if !entry_path.is_file() {
        return Err(format!("module entry '{entry}' does not exist"));
    }

    let source_root = root.join("src");
    if !source_root.is_dir() {
        return Err("module source directory 'src/' does not exist".to_string());
    }
    let mut files = Vec::new();
    let mut total_source_bytes = manifest.len() as u64;
    collect_source_files(
        &source_root,
        &source_root,
        &mut files,
        &mut total_source_bytes,
    )?;
    files.sort_by(|left, right| left.0.cmp(&right.0));

    let mut writer = ZipWriter::new(Cursor::new(Vec::new()));
    let options = SimpleFileOptions::default().compression_method(CompressionMethod::Deflated);
    writer
        .start_file("manifest.toml", options)
        .map_err(|_| "could not create module ZIP".to_string())?;
    writer
        .write_all(manifest.as_bytes())
        .map_err(|_| "could not write module manifest into ZIP".to_string())?;
    for (archive_path, contents) in files {
        writer
            .start_file(&archive_path, options)
            .map_err(|_| "could not create module ZIP entry".to_string())?;
        writer
            .write_all(&contents)
            .map_err(|_| "could not write module source into ZIP".to_string())?;
    }
    let bytes = writer
        .finish()
        .map_err(|_| "could not finish module ZIP".to_string())?
        .into_inner();
    Ok((name, version, bytes))
}

fn parse_module_manifest(text: &str) -> Result<(String, String, String), String> {
    let value: toml::Value =
        toml::from_str(text).map_err(|_| "module.toml is not valid TOML".to_string())?;
    let module = value
        .get("module")
        .and_then(toml::Value::as_table)
        .ok_or_else(|| "module.toml must contain a [module] table".to_string())?;
    let name = module
        .get("name")
        .and_then(toml::Value::as_str)
        .ok_or_else(|| "module.toml [module] needs a string name".to_string())?
        .to_string();
    let version = module
        .get("version")
        .and_then(toml::Value::as_str)
        .ok_or_else(|| "module.toml [module] needs a string version".to_string())?
        .to_string();
    let entry = module
        .get("entry")
        .and_then(toml::Value::as_str)
        .ok_or_else(|| "module.toml [module] needs a string entry".to_string())?
        .to_string();
    if !valid_module_name(&name) {
        return Err("module name must contain only letters, digits, '.', '_' or '-'".to_string());
    }
    let parsed_version = semver::Version::parse(&version).map_err(|_| {
        "module version must be semantic versioning (for example 1.2.3)".to_string()
    })?;
    if parsed_version.to_string() != version {
        return Err("module version must use canonical semantic-version syntax".to_string());
    }
    if !safe_source_path(&entry) || !entry.ends_with(".lynx") {
        return Err("module entry must be a safe .lynx path under src/".to_string());
    }
    Ok((name, version, entry))
}

fn valid_module_name(name: &str) -> bool {
    !name.is_empty()
        && name.len() <= 100
        && name.as_bytes()[0].is_ascii_alphanumeric()
        && name
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.'))
        && name != "."
        && name != ".."
}

fn safe_source_path(path: &str) -> bool {
    path.starts_with("src/")
        && !path.contains('\\')
        && !path.contains('\0')
        && Path::new(path)
            .components()
            .all(|component| matches!(component, Component::Normal(_)))
        && path
            .split('/')
            .all(|part| !part.is_empty() && part != "." && part != "..")
}

fn collect_source_files(
    source_root: &Path,
    directory: &Path,
    files: &mut Vec<(String, Vec<u8>)>,
    total_source_bytes: &mut u64,
) -> Result<(), String> {
    let mut entries = fs::read_dir(directory)
        .map_err(|error| format!("cannot read '{}': {error}", directory.display()))?
        .collect::<Result<Vec<_>, _>>()
        .map_err(|error| format!("cannot read module source entry: {error}"))?;
    entries.sort_by_key(|entry| entry.file_name());

    for entry in entries {
        let path = entry.path();
        let metadata = fs::symlink_metadata(&path)
            .map_err(|error| format!("cannot inspect '{}': {error}", path.display()))?;
        if metadata.file_type().is_symlink() {
            return Err(format!(
                "symbolic links are not allowed in modules: {}",
                path.display()
            ));
        }
        if metadata.is_dir() {
            collect_source_files(source_root, &path, files, total_source_bytes)?;
        } else if metadata.is_file() {
            if files.len() >= MAX_SOURCE_FILES {
                return Err("module contains more than 1023 source files".to_string());
            }
            *total_source_bytes = (*total_source_bytes)
                .checked_add(metadata.len())
                .ok_or_else(|| "module source exceeds the 128 MiB registry limit".to_string())?;
            if *total_source_bytes > MAX_UNPACKED_BYTES {
                return Err("module source exceeds the 128 MiB registry limit".to_string());
            }
            let relative = path
                .strip_prefix(source_root)
                .map_err(|_| "module source path escaped src/".to_string())?;
            let relative = relative.to_string_lossy().replace('\\', "/");
            let archive_path = format!("src/{relative}");
            if !safe_source_path(&archive_path) {
                return Err(format!("unsafe module source path '{archive_path}'"));
            }
            let contents = fs::read(&path)
                .map_err(|error| format!("cannot read '{}': {error}", path.display()))?;
            if contents.len() as u64 != metadata.len() {
                return Err(format!(
                    "module source changed while reading: {}",
                    path.display()
                ));
            }
            files.push((archive_path, contents));
        } else {
            return Err(format!(
                "unsupported module source entry: {}",
                path.display()
            ));
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_manifest_metadata_and_rejects_bad_paths() {
        let valid =
            "[module]\nname = \"math-utils\"\nversion = \"1.2.3\"\nentry = \"src/main.lynx\"\n";
        assert_eq!(
            parse_module_manifest(valid).unwrap(),
            (
                "math-utils".to_string(),
                "1.2.3".to_string(),
                "src/main.lynx".to_string()
            )
        );

        let invalid =
            "[module]\nname = \"../outside\"\nversion = \"1.2.3\"\nentry = \"src/main.lynx\"\n";
        assert!(parse_module_manifest(invalid).is_err());
        let unsafe_entry =
            "[module]\nname = \"safe\"\nversion = \"1.2.3\"\nentry = \"src/../outside.lynx\"\n";
        assert!(parse_module_manifest(unsafe_entry).is_err());
        let noncanonical =
            "[module]\nname = \"safe\"\nversion = \"v1.2.3\"\nentry = \"src/main.lynx\"\n";
        assert!(parse_module_manifest(noncanonical).is_err());
    }
}
