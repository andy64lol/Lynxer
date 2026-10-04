use std::{
    env, fs,
    io::{Cursor, Write},
    path::{Component, Path, PathBuf},
    collections::HashMap,
};

use base64::engine::general_purpose::URL_SAFE;
use base64::Engine as _;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use ureq::Agent;
use zip::{write::SimpleFileOptions, CompressionMethod, ZipWriter};

#[derive(Debug, Serialize, Deserialize, Default)]
pub(crate) struct RegistryEntry {
    pub(crate) owner: String,
    pub(crate) repository: String,
}

#[derive(Debug, Serialize, Deserialize, Default)]
pub(crate) struct Registry {
    pub(crate) packages: HashMap<String, RegistryEntry>,
}

/// User configuration for the REST registry link and the GitHub API token.
/// Stored at `~/.bob/config.json`.
#[derive(Debug, Serialize, Deserialize, Default)]
pub(crate) struct Config {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub(crate) rest_api: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub(crate) github_token: Option<String>,
}

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
    let github_token = github_token()?;

    let (name, version, archive) = archive_current_module()?;
    if archive.len() > MAX_ARCHIVE_BYTES {
        return Err("module archive must be below 10 MB".to_string());
    }

    let sha256 = compute_sha256(&archive);
    let release_name = format!("{}-{}", name, version);

    // For publishing, we assume the user has already added the package to their registry.
    // If not, they can add it manually using `bob registry add`.
    let release = create_github_release(
        &github_token,
        &name,
        &version,
        &release_name,
        &archive,
        &sha256,
    )?;

    Ok(format!(
        "Published {}@{}(sha256:{})",
        release.name, release.version, release.sha256
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

/// Compute the SHA-256 checksum of a byte slice.
fn compute_sha256(bytes: &[u8]) -> String {
    let mut hasher = Sha256::new();
    hasher.update(bytes);
    let result = hasher.finalize();
    URL_SAFE.encode(result)
}

/// Create a GitHub release for the module and upload its archive as an asset.
fn create_github_release(
    token: &str,
    name: &str,
    version: &str,
    release_name: &str,
    archive: &[u8],
    sha256: &str,
) -> Result<PublishResponse, String> {
    let agent_config = Agent::config_builder().http_status_as_error(false).build();
    let agent = Agent::new_with_config(agent_config);

    // Resolve the GitHub repository for this package using the user's registry.
    let (repo_owner, repo_name) = resolve_repository(name)?;

    let api_url = format!("https://api.github.com/repos/{}/{}/releases", repo_owner, repo_name);

    // Create the release.
    let release_body = format!("Module {} version {}", name, version);
    let json_body = serde_json::to_vec(&serde_json::json!({
        "tag_name": version,
        "name": release_name,
        "body": release_body,
        "draft": false,
        "prerelease": false,
    })).unwrap();
    let mut response = agent
        .post(&api_url)
        .header("Authorization", &format!("token {}", token))
        .header("Content-Type", "application/json")
        .send(&json_body)
        .map_err(|_| "could not create GitHub release")?;

    let status = response.status().as_u16();
    if !(200..300).contains(&status) {
        let response_body = String::from_utf8_lossy(&response.body_mut().read_to_vec().unwrap_or_default()).into_owned();
        let message = serde_json::from_str::<serde_json::Value>(&response_body)
            .ok()
            .and_then(|value| {
                value
                    .get("message")
                    .and_then(|m| m.as_str())
                    .map(|s| s.to_string())
            })
            .unwrap_or_else(|| format!("GitHub returned HTTP {status}"));
        return Err(message);
    }

    let body_bytes = response
        .body_mut()
        .read_to_vec()
        .map_err(|_| "could not parse GitHub release response")?;
    let release: serde_json::Value = serde_json::from_slice(&body_bytes)
        .map_err(|_| "could not parse GitHub release response")?;
    let release_id = release
        .get("id")
        .and_then(|id| id.as_u64())
        .ok_or_else(|| "could not extract release ID")?;

    // Upload the archive as a release asset. The asset name must match the
    // `{name}-{version}.zip` that `bob install` looks for.
    let upload_url = format!(
        "https://uploads.github.com/repos/{}/{}/releases/{}/assets?name={}-{}.zip",
        repo_owner, repo_name, release_id, name, version
    );
    let mut upload_response = agent
        .post(&upload_url)
        .header("Authorization", &format!("token {}", token))
        .header("Content-Type", "application/octet-stream")
        .send(archive)
        .map_err(|_| "could not upload asset to GitHub")?;

    let upload_status = upload_response.status().as_u16();
    if !(200..300).contains(&upload_status) {
        let upload_body = String::from_utf8_lossy(&upload_response.body_mut().read_to_vec().unwrap_or_default()).into_owned();
        let message = serde_json::from_str::<serde_json::Value>(&upload_body)
            .ok()
            .and_then(|value| {
                value
                    .get("message")
                    .and_then(|m| m.as_str())
                    .map(|s| s.to_string())
            })
            .unwrap_or_else(|| format!("GitHub upload returned HTTP {}", upload_status));
        return Err(message);
    }

    Ok(PublishResponse {
        name: name.to_string(),
        version: version.to_string(),
        sha256: sha256.to_string(),
    })
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

    #[test]
    fn config_round_trips_and_defaults_to_empty() {
        let config = Config {
            rest_api: Some("http://localhost:3000".to_string()),
            github_token: Some("token".to_string()),
        };
        let text = serde_json::to_string(&config).unwrap();
        let parsed: Config = serde_json::from_str(&text).unwrap();
        assert_eq!(parsed.rest_api.as_deref(), Some("http://localhost:3000"));
        assert_eq!(parsed.github_token.as_deref(), Some("token"));

        let empty: Config = serde_json::from_str("{}").unwrap();
        assert!(empty.rest_api.is_none());
        assert!(empty.github_token.is_none());
    }

    #[test]
    fn builds_the_resolve_endpoint() {
        assert_eq!(
            resolve_endpoint("http://localhost:3000"),
            "http://localhost:3000/api/resolve"
        );
        assert_eq!(
            resolve_endpoint("http://localhost:3000/"),
            "http://localhost:3000/api/resolve"
        );
    }

    #[test]
    fn parses_resolve_responses() {
        let parsed = parse_resolve_body(br#"{"owner":"acme","repository":"math-utils"}"#).unwrap();
        assert_eq!(parsed, ("acme".to_string(), "math-utils".to_string()));
        assert!(parse_resolve_body(br#"{}"#).is_err());
        assert!(parse_resolve_body(b"not json").is_err());
    }
}

/// Fetch and install a package from GitHub Releases.
pub fn install_package(name: &str, version: &str) -> Result<String, String> {
    let github_token = github_token()?;

    // Resolve the GitHub repository for this package using the user's registry.
    let (repo_owner, repo_name) = resolve_repository(name)?;

    let asset_url = fetch_asset_from_github(&github_token, &repo_owner, &repo_name, name, version)?;
    let archive = download_asset(&asset_url)?;
    let sha256 = compute_sha256(&archive);

    let packages_dir = env::current_dir()
        .map_err(|error| format!("cannot determine current directory: {error}"))?
        .join("bob")
        .join("packages");
    fs::create_dir_all(&packages_dir)
        .map_err(|error| format!("cannot create packages directory: {error}"))?;

    let package_dir = packages_dir.join(&format!("{}-{}", name, version));
    fs::create_dir_all(&package_dir)
        .map_err(|error| format!("cannot create package directory: {error}"))?;

    let archive_path = package_dir.join("archive.zip");
    fs::write(&archive_path, &archive)
        .map_err(|error| format!("cannot write archive: {error}"))?;

    Ok(format!("Installed {}@{}(sha256:{})", name, version, sha256))
}

/// Resolve the GitHub repository for a package.
///
/// Resolution order: the REST registry (when `rest_api` is configured), then the
/// local `~/.bob/registry.json`, then the built-in default mappings.
fn resolve_repository(name: &str) -> Result<(String, String), String> {
    // Prefer the REST registry when one is configured.
    let config = load_config();
    if let Some(base) = resolved_rest_api(&config) {
        match resolve_via_rest(&base, name) {
            Ok(Some(repository)) => return Ok(repository),
            Ok(None) => {}
            Err(warning) => eprintln!("bob: warning: {warning}; falling back to the local registry"),
        }
    }

    // Load the user's registry.
    let user_registry = load_user_registry();

    // Check if the package is in the user's registry.
    if let Some(entry) = user_registry.packages.get(name) {
        return Ok((entry.owner.clone(), entry.repository.clone()));
    }

    // If not found in the user's registry, check the default mappings.
    let default_registry = Registry {
        packages: {
            let mut packages = HashMap::new();
            packages.insert(
                "lynxer".to_string(),
                RegistryEntry {
                    owner: "lynxer-lang".to_string(),
                    repository: "lynxer".to_string(),
                },
            );
            packages.insert(
                "math-utils".to_string(),
                RegistryEntry {
                    owner: "lynxer-lang".to_string(),
                    repository: "math-utils".to_string(),
                },
            );
            packages.insert(
                "game-engine".to_string(),
                RegistryEntry {
                    owner: "lynxer-lang".to_string(),
                    repository: "game-engine".to_string(),
                },
            );
            packages.insert(
                "ui-components".to_string(),
                RegistryEntry {
                    owner: "lynxer-lang".to_string(),
                    repository: "ui-components".to_string(),
                },
            );
            packages.insert(
                "networking".to_string(),
                RegistryEntry {
                    owner: "lynxer-lang".to_string(),
                    repository: "networking".to_string(),
                },
            );
            packages
        },
    };

    if let Some(entry) = default_registry.packages.get(name) {
        Ok((entry.owner.clone(), entry.repository.clone()))
    } else {
        Err(format!("Package '{}' not found in the registry", name))
    }
}

/// Load the user's registry from ~/.bob/registry.json.
fn load_user_registry() -> Registry {
    let home_dir = match dirs::home_dir() {
        Some(dir) => dir,
        None => {
            eprintln!("bob: cannot determine home directory");
            std::process::exit(1);
        }
    };
    let registry_path = home_dir.join(".bob").join("registry.json");

    if !registry_path.exists() {
        return Registry {
            packages: HashMap::new(),
        };
    }

    let content = match fs::read_to_string(&registry_path) {
        Ok(content) => content,
        Err(error) => {
            eprintln!("bob: cannot read registry: {error}");
            std::process::exit(1);
        }
    };

    match serde_json::from_str(&content) {
        Ok(registry) => registry,
        Err(error) => {
            eprintln!("bob: invalid registry format: {error}");
            std::process::exit(1);
        }
    }
}

/// Path to the user's Bob config file (`~/.bob/config.json`).
///
/// `BOB_CONFIG_DIR` overrides the directory, which keeps tests hermetic and
/// works the same on Windows (where `dirs::home_dir` ignores `HOME`).
pub(crate) fn config_path() -> PathBuf {
    if let Ok(dir) = env::var("BOB_CONFIG_DIR") {
        let dir = dir.trim();
        if !dir.is_empty() {
            return PathBuf::from(dir).join("config.json");
        }
    }
    let home_dir = match dirs::home_dir() {
        Some(dir) => dir,
        None => {
            eprintln!("bob: cannot determine home directory");
            std::process::exit(1);
        }
    };
    home_dir.join(".bob").join("config.json")
}

/// Load the user's config from `~/.bob/config.json`.
pub(crate) fn load_config() -> Config {
    let path = config_path();
    if !path.exists() {
        return Config::default();
    }
    let content = match fs::read_to_string(&path) {
        Ok(content) => content,
        Err(error) => {
            eprintln!("bob: cannot read config: {error}");
            std::process::exit(1);
        }
    };
    match serde_json::from_str(&content) {
        Ok(config) => config,
        Err(error) => {
            eprintln!("bob: invalid config format: {error}");
            std::process::exit(1);
        }
    }
}

/// Save the user's config, restricting permissions on Unix because it can hold
/// a token.
pub(crate) fn save_config(config: &Config) -> Result<(), String> {
    let path = config_path();
    let parent = path
        .parent()
        .ok_or_else(|| "cannot determine config directory".to_string())?;
    fs::create_dir_all(parent)
        .map_err(|error| format!("cannot create config directory: {error}"))?;
    let content = serde_json::to_string_pretty(config)
        .map_err(|error| format!("cannot serialize config: {error}"))?;
    fs::write(&path, content).map_err(|error| format!("cannot write config: {error}"))?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(&path, fs::Permissions::from_mode(0o600))
            .map_err(|error| format!("cannot set config permissions: {error}"))?;
    }
    Ok(())
}

/// The REST registry link: `BOB_REST_API` overrides the config file.
pub(crate) fn resolved_rest_api(config: &Config) -> Option<String> {
    if let Ok(value) = env::var("BOB_REST_API") {
        let value = value.trim();
        if !value.is_empty() {
            return Some(value.to_string());
        }
    }
    config
        .rest_api
        .as_ref()
        .map(|value| value.trim())
        .filter(|value| !value.is_empty())
        .map(str::to_string)
}

/// The GitHub API token: `GITHUB_TOKEN` overrides the config file.
pub(crate) fn resolved_github_token(config: &Config) -> Option<String> {
    if let Ok(value) = env::var("GITHUB_TOKEN") {
        let value = value.trim();
        if !value.is_empty() {
            return Some(value.to_string());
        }
    }
    config
        .github_token
        .as_ref()
        .map(|value| value.trim())
        .filter(|value| !value.is_empty())
        .map(str::to_string)
}

/// The GitHub API token required by publish/install.
fn github_token() -> Result<String, String> {
    resolved_github_token(&load_config()).ok_or_else(|| {
        "GitHub token is not configured; run 'bob config set github-token <token>' or set GITHUB_TOKEN"
            .to_string()
    })
}

/// Build the REST registry resolve endpoint from its base link.
fn resolve_endpoint(base: &str) -> String {
    format!("{}/api/resolve", base.trim_end_matches('/'))
}

/// Parse a `/api/resolve` response body into `(owner, repository)`.
fn parse_resolve_body(bytes: &[u8]) -> Result<(String, String), String> {
    let value: serde_json::Value =
        serde_json::from_slice(bytes).map_err(|_| "registry API returned invalid JSON".to_string())?;
    let owner = value
        .get("owner")
        .and_then(|value| value.as_str())
        .ok_or_else(|| "registry API response is missing 'owner'".to_string())?;
    let repository = value
        .get("repository")
        .and_then(|value| value.as_str())
        .ok_or_else(|| "registry API response is missing 'repository'".to_string())?;
    Ok((owner.to_string(), repository.to_string()))
}

/// Ask the REST registry to resolve a package name. Returns `Ok(None)` when the
/// registry does not know the package (HTTP 404).
fn resolve_via_rest(base: &str, name: &str) -> Result<Option<(String, String)>, String> {
    let endpoint = resolve_endpoint(base);
    let agent_config = Agent::config_builder().http_status_as_error(false).build();
    let agent = Agent::new_with_config(agent_config);
    let body = serde_json::to_vec(&serde_json::json!({ "name": name }))
        .map_err(|error| format!("could not encode registry request: {error}"))?;

    let mut response = agent
        .post(&endpoint)
        .header("Content-Type", "application/json")
        .send(&body)
        .map_err(|error| format!("registry API request failed: {error}"))?;

    let status = response.status().as_u16();
    if status == 404 {
        return Ok(None);
    }
    let body_bytes = response
        .body_mut()
        .read_to_vec()
        .map_err(|error| format!("could not read registry API response: {error}"))?;
    if !(200..300).contains(&status) {
        return Err(format!("registry API returned HTTP {status}"));
    }
    parse_resolve_body(&body_bytes).map(Some)
}

fn fetch_asset_from_github(
    token: &str,
    repo_owner: &str,
    repo_name: &str,
    name: &str,
    version: &str,
) -> Result<String, String> {
    let api_url = format!("https://api.github.com/repos/{}/{}/releases/tags/{}", repo_owner, repo_name, version);

    let agent_config = Agent::config_builder().http_status_as_error(false).build();
    let agent = Agent::new_with_config(agent_config);

    let mut response = agent
        .get(&api_url)
        .header("Authorization", &format!("token {}", token))
        .header("Accept", "application/vnd.github.v3+json")
        .call()
        .map_err(|_| "could not fetch GitHub release")?;

    let status = response.status().as_u16();
    if !(200..300).contains(&status) {
        let response_body = String::from_utf8_lossy(&response.body_mut().read_to_vec().unwrap_or_default()).into_owned();
        return Err(format!("GitHub returned HTTP {status}: {response_body}"));
    }

    let body_bytes = response
        .body_mut()
        .read_to_vec()
        .map_err(|_| "could not parse GitHub release response")?;
    let release: serde_json::Value = serde_json::from_slice(&body_bytes)
        .map_err(|_| "could not parse GitHub release response")?;

    let assets = release
        .get("assets")
        .and_then(|assets| assets.as_array())
        .ok_or_else(|| "could not find assets in release")?;

    let asset = assets.iter().find(|asset| {
        asset.get("name")
            .and_then(|name| name.as_str())
            .map(|n| n == format!("{}-{}.zip", name, version))
            .unwrap_or(false)
    }).ok_or_else(|| format!("could not find asset for {}-{}", name, version))?;

    let browser_download_url = asset
        .get("browser_download_url")
        .and_then(|url| url.as_str())
        .ok_or_else(|| "could not extract browser_download_url")?
        .to_string();

    Ok(browser_download_url)
}

/// Download an asset from a URL.
fn download_asset(url: &str) -> Result<Vec<u8>, String> {
    let agent_config = Agent::config_builder().http_status_as_error(false).build();
    let agent = Agent::new_with_config(agent_config);

    let mut response = agent
        .get(url)
        .call()
        .map_err(|_| format!("could not download asset from {}", url))?;

    let status = response.status().as_u16();
    if !(200..300).contains(&status) {
        let response_body = String::from_utf8_lossy(&response.body_mut().read_to_vec().unwrap_or_default()).into_owned();
        return Err(format!("Download returned HTTP {status}: {response_body}"));
    }

    let bytes = response
        .body_mut()
        .read_to_vec()
        .map_err(|e| format!("could not read asset bytes from {}: {}", url, e))?;
    Ok(bytes)
}
