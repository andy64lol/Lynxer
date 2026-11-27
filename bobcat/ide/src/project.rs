//! Wynxer project detection for the native IDE.
//!
//! A Wynxer project is a directory containing `.wynxcfg` and a `.wpf` page.
//! Bobcat switches to a specialised mode when it is launched in such a
//! directory, so Run / Lint / Format operate on the whole project.

use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};

#[derive(Debug, Clone)]
pub struct WynxerProject {
    pub root: PathBuf,
    pub name: String,
    pub entry: String,
    pub script: String,
    pub output: String,
    pub port: u16,
}

/// Read a `.wynxcfg` into a key/value map.
fn read_config(root: &Path) -> HashMap<String, String> {
    let mut out = HashMap::new();
    if let Ok(text) = fs::read_to_string(root.join(".wynxcfg")) {
        for line in text.lines() {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            if let Some((key, value)) = line.split_once('=') {
                out.insert(key.trim().to_string(), value.trim().to_string());
            }
        }
    }
    out
}

fn has_wpf(root: &Path) -> bool {
    fs::read_dir(root)
        .map(|entries| {
            entries.flatten().any(|e| {
                e.path()
                    .extension()
                    .and_then(|ext| ext.to_str())
                    .map(|ext| ext.eq_ignore_ascii_case("wpf"))
                    .unwrap_or(false)
            })
        })
        .unwrap_or(false)
}

/// Detect a Wynxer project in `dir`. Requires `.wynxcfg` plus a `.wpf` page.
pub fn detect(dir: &Path) -> Option<WynxerProject> {
    if !dir.join(".wynxcfg").is_file() || !has_wpf(dir) {
        return None;
    }
    let config = read_config(dir);
    let get = |key: &str, default: &str| {
        config
            .get(key)
            .filter(|v| !v.is_empty())
            .cloned()
            .unwrap_or_else(|| default.to_string())
    };
    let port = get("port", "3000").parse().unwrap_or(3000);
    Some(WynxerProject {
        root: dir.to_path_buf(),
        name: get("name", "wynxer-app"),
        entry: get("entry", "index.wpf"),
        script: get("script", "app.wynx"),
        output: get("output", "build"),
        port,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn project_dir() -> PathBuf {
        let dir = std::env::temp_dir().join(format!("bobcat-project-test-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join(".wynxcfg"), "name = Demo\nentry = index.wpf\nport = 8080\n").unwrap();
        fs::write(dir.join("index.wpf"), "page index { }\n").unwrap();
        dir
    }

    #[test]
    fn detects_wynxer_project() {
        let dir = project_dir();
        let project = detect(&dir).unwrap();
        assert_eq!(project.name, "Demo");
        assert_eq!(project.entry, "index.wpf");
        assert_eq!(project.port, 8080);
        fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn rejects_non_project_directory() {
        let dir = std::env::temp_dir().join(format!("bobcat-not-project-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        assert!(detect(&dir).is_none());
        fs::remove_dir_all(&dir).ok();
    }
}
