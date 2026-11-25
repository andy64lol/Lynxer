//! Project scaffolding for `wynxer --init` and config loading for `--run`.
//!
//! Mirrors `lynxer --init`: refuses to overwrite existing files, creates a
//! source layout, a `.wynxcfg` configuration file, and a starter page/script/
//! stylesheet. `.wynxcfg` uses the same `key = value` format as
//! `lynxer.config`.

use std::collections::HashMap;
use std::fs;
use std::path::Path;
use std::process::Command;

/// The name of the Wynxer project configuration file.
pub const CONFIG_FILE: &str = ".wynxcfg";

/// Read a `key = value` config file. Lines starting with `#` are comments;
/// `\n` escapes are expanded. Missing files yield an empty map (defaults).
pub fn read_config(dir: &Path) -> HashMap<String, String> {
    let mut config = HashMap::new();
    let path = dir.join(CONFIG_FILE);
    if let Ok(text) = fs::read_to_string(&path) {
        for line in text.lines() {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            if let Some((key, value)) = line.split_once('=') {
                let value = value.trim().replace("\\n", "\n");
                config.insert(key.trim().to_string(), value);
            }
        }
    }
    config
}

/// Get a config value (or a default when absent/empty).
pub fn config_get<'a>(
    config: &'a HashMap<String, String>,
    key: &str,
    default: &'a str,
) -> &'a str {
    match config.get(key) {
        Some(v) if !v.is_empty() => v.as_str(),
        _ => default,
    }
}

/// Scaffold a Wynxer project in `dir`. Refuses to overwrite existing files.
/// `name` defaults to the directory name. Returns a status message on success.
pub fn init_project(dir: &Path, name: Option<&str>) -> Result<String, String> {
    let name = match name {
        Some(n) if !n.is_empty() => n.to_string(),
        _ => dir
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("wynxer-project")
            .to_string(),
    };
    if name == "." || name == ".." || name.contains('/') || name.contains('\\') {
        return Err(format!("invalid project name '{}'", name));
    }

    // Targets created by --init; none may already exist.
    let targets = [
        "README.md",
        "index.wpf",
        "app.wynx",
        "styles.css",
        CONFIG_FILE,
        ".gitignore",
        "package.json",
    ];
    for target in &targets {
        if dir.join(target).exists() {
            return Err(format!(
                "'{}' already exists; --init will not overwrite files",
                target
            ));
        }
    }

    fs::create_dir_all(dir.join("build"))
        .map_err(|e| format!("cannot create build/: {}", e))?;

    let readme = format!(
        "# {}\n\nA Wynxer web project.\n\nLayout:\n\
         \x20 README.md      this file\n\
         \x20 index.wpf      page (Wynxer Page Format)\n\
         \x20 app.wynx       logic script\n\
         \x20 styles.css     stylesheet\n\
         \x20 .wynxcfg       settings (name, entry, script, output, port, deps, node, type)\n\
         \x20 package.json   generated from .wynxcfg for npm/node tooling\n\
         \x20 .gitignore     ignores build/ and node_modules/\n\
         \x20 build/         generated index.html, app.js, styles.css\n\n\
         Commands:\n\
         \x20 wynxer --build       generate build/\n\
         \x20 wynxer --run         build, then serve on the configured port\n\
         \x20 wynxer --add <pkg>   install an npm package (uses node/npm)\n\
         \x20 wynxer --remove <pkg>  uninstall an npm package\n\
         \x20 wynxer check         validate the project\n\n\
         Edit index.wpf and app.wynx, then run `wynxer --run`.\n",
        name
    );

    let wpf = format!(
        "import script app;\n\
         import styles styles;\n\n\
         page index {{\n\
         \x20 page.title = \"{}\";\n\n\
         \x20 contents {{\n\
         \x20     element.header1(\"Hello, Wynxer!\");\n\
         \x20     element.text(\"Edit index.wpf to change this page.\");\n\
         \x20     element.button(\"Click me!\", app.onClick);\n\
         \x20 }}\n\n\
         \x20 scripts {{\n\
         \x20 }}\n\
         }}\n",
        name
    );

    let wynx = "fn onClick() {\n    int x = 5;\n}\n\nfn main() {\n    int count = 0;\n}\n";

    let css = "body {\n    font-family: sans-serif;\n    margin: 2rem;\n}\n";

    let config = format!(
        "# Wynxer project configuration.\n\
         # Format: one key = value pair per line. Lines starting with '#' are comments.\n\
         # entry:  page file compiled to build/index.html.\n\
         # script: logic file compiled to build/app.js.\n\
         # output: build directory.\n\
         # port:   port used by --run / --serve.\n\
         # deps:   npm packages (comma separated) — use `wynxer --add <pkg>`.\n\
         # devDeps: npm dev packages — use `wynxer --add --dev <pkg>`.\n\
         # node:   minimum Node.js version written to package.json engines.\n\
         # type:   JS module system (commonjs or module).\n\
         # package_manager: npm, pnpm or yarn.\n\
         # target:  browser (default) or node (uses `type` for module system).\n\
         name = {}\n\
         entry = index.wpf\n\
         script = app.wynx\n\
         output = build\n\
         port = 3000\n\
         deps = \n\
         devDeps = \n\
         node = 20\n\
         type = commonjs\n\
         package_manager = npm\n\
         target = browser\n",
        name
    );

    let gitignore = "build/\nnode_modules/\n";

    let files = [
        ("README.md", readme.as_str()),
        ("index.wpf", wpf.as_str()),
        ("app.wynx", wynx),
        ("styles.css", css),
        (CONFIG_FILE, config.as_str()),
        (".gitignore", gitignore),
    ];
    for (path, contents) in &files {
        fs::write(dir.join(path), contents)
            .map_err(|e| format!("cannot write '{}': {}", path, e))?;
    }
    // Generate package.json from the fresh .wynxcfg.
    sync_package_json(dir)?;

    let here = fs::canonicalize(dir)
        .unwrap_or_else(|_| dir.to_path_buf())
        .display()
        .to_string();
    Ok(format!(
        "Initialized Wynxer project '{}' in {}\n\
         \x20 README.md, index.wpf, app.wynx, styles.css, .wynxcfg, .gitignore, build/\n\
         Run 'wynxer --run' to build and serve it.",
        name, here
    ))
}

// ---- Node / JS package management -----------------------------------------

/// Set (or insert) a `key = value` line in `.wynxcfg`, preserving comments
/// and all other keys.
pub fn set_config_key(dir: &Path, key: &str, value: &str) -> Result<(), String> {
    let path = dir.join(CONFIG_FILE);
    let text = fs::read_to_string(&path)
        .map_err(|e| format!("cannot read '{}': {}", CONFIG_FILE, e))?;
    let mut lines: Vec<String> = text.lines().map(str::to_string).collect();
    let mut found = false;
    for line in &mut lines {
        if let Some((k, _)) = line.split_once('=') {
            if k.trim() == key {
                *line = format!("{} = {}", key, value);
                found = true;
                break;
            }
        }
    }
    if !found {
        lines.push(format!("{} = {}", key, value));
    }
    let mut out = lines.join("\n");
    out.push('\n');
    fs::write(&path, out).map_err(|e| format!("cannot write '{}': {}", CONFIG_FILE, e))
}

/// Read a comma/space separated dependency list from a config key.
fn read_list(config: &HashMap<String, String>, key: &str) -> Vec<String> {
    config
        .get(key)
        .map(|v| crate::jsabi::parse_deps(v))
        .unwrap_or_default()
}

/// Generate a `package.json` from `.wynxcfg` so npm/node tooling and Wynxer
/// agree on dependencies. Existing manual edits to package.json are preserved
/// except for the dependency maps, which are regenerated from the config.
pub fn sync_package_json(dir: &Path) -> Result<(), String> {
    let config = read_config(dir);
    let name = config_get(&config, "name", "wynxer-app").to_string();
    let module_type = config_get(&config, "type", "commonjs");
    let node_version = config_get(&config, "node", "20");
    let deps = read_list(&config, "deps");
    let dev_deps = read_list(&config, "devDeps");

    let deps_json = deps
        .iter()
        .map(|d| format!("    \"{}\": \"latest\"", d))
        .collect::<Vec<_>>()
        .join(",\n");
    let dev_json = dev_deps
        .iter()
        .map(|d| format!("    \"{}\": \"latest\"", d))
        .collect::<Vec<_>>()
        .join(",\n");

    let mut json = format!(
        "{{\n  \"name\": \"{}\",\n  \"version\": \"1.0.0\",\n  \"private\": true,\n  \"type\": \"{}\",\n  \"engines\": {{\n    \"node\": \">={}\"\n  }},\n",
        escape_json(&name), module_type, node_version
    );
    json.push_str("  \"dependencies\": {\n");
    json.push_str(&deps_json);
    json.push_str("\n  },\n  \"devDependencies\": {\n");
    json.push_str(&dev_json);
    json.push_str("\n  }\n}\n");

    fs::write(dir.join("package.json"), json)
        .map_err(|e| format!("cannot write 'package.json': {}", e))
}

fn escape_json(s: &str) -> String {
    s.replace('\\', "\\\\").replace('"', "\\\"")
}

fn run_npm(dir: &Path, args: &[&str]) -> Result<(), String> {
    let status = Command::new("npm")
        .args(args)
        .current_dir(dir)
        .status()
        .map_err(|e| format!("failed to run npm: {} (is Node.js installed?)", e))?;
    if !status.success() {
        return Err(format!("npm {} failed with status {}", args.join(" "), status));
    }
    Ok(())
}

/// `wynxer --add <package>...`: install packages with npm and record them in
/// `.wynxcfg` (`deps` or `devDeps`), then regenerate `package.json`.
pub fn add_packages(dir: &Path, packages: &[String], dev: bool) -> Result<String, String> {
    if packages.is_empty() {
        return Err("no package names given".to_string());
    }
    let mut args = vec!["install"];
    if dev {
        args.push("--save-dev");
    }
    let pkg_refs: Vec<&str> = packages.iter().map(String::as_str).collect();
    args.extend(pkg_refs.iter().copied());
    run_npm(dir, &args)?;

    let config = read_config(dir);
    let key = if dev { "devDeps" } else { "deps" };
    let mut current = read_list(&config, key);
    for p in packages {
        if !current.contains(p) {
            current.push(p.clone());
        }
    }
    set_config_key(dir, key, &current.join(", "))?;
    sync_package_json(dir)?;
    Ok(format!(
        "Added {} package(s) to {} and package.json: {}",
        packages.len(),
        key,
        packages.join(", ")
    ))
}

/// `wynxer --remove <package>...`: uninstall packages with npm and remove them
/// from `.wynxcfg`, then regenerate `package.json`.
pub fn remove_packages(dir: &Path, packages: &[String]) -> Result<String, String> {
    if packages.is_empty() {
        return Err("no package names given".to_string());
    }
    let mut args = vec!["uninstall"];
    let pkg_refs: Vec<&str> = packages.iter().map(String::as_str).collect();
    args.extend(pkg_refs.iter().copied());
    run_npm(dir, &args)?;

    let config = read_config(dir);
    for key in ["deps", "devDeps"] {
        let current = read_list(&config, key);
        let remaining: Vec<String> = current
            .into_iter()
            .filter(|p| !packages.contains(p))
            .collect();
        set_config_key(dir, key, &remaining.join(", "))?;
    }
    sync_package_json(dir)?;
    Ok(format!(
        "Removed {} package(s) and updated .wynxcfg / package.json: {}",
        packages.len(),
        packages.join(", ")
    ))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::sync::atomic::{AtomicUsize, Ordering};

    static COUNTER: AtomicUsize = AtomicUsize::new(0);

    fn temp_project() -> std::path::PathBuf {
        let n = COUNTER.fetch_add(1, Ordering::SeqCst);
        let dir = std::env::temp_dir().join(format!(
            "wynxer-init-test-{}-{}",
            std::process::id(),
            n
        ));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn test_init_generates_config_and_package_json() {
        let dir = temp_project();
        let msg = init_project(&dir, Some("TestApp")).unwrap();
        assert!(msg.contains("TestApp"));
        let config = read_config(&dir);
        assert_eq!(config_get(&config, "deps", ""), "");
        assert_eq!(config_get(&config, "node", ""), "20");
        assert_eq!(config_get(&config, "target", ""), "browser");
        let pkg = fs::read_to_string(dir.join("package.json")).unwrap();
        assert!(pkg.contains("\"name\": \"TestApp\""));
        assert!(pkg.contains("\"node\": \">=20\""));
        fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn test_set_config_key_updates_in_place() {
        let dir = temp_project();
        init_project(&dir, Some("T")).unwrap();
        set_config_key(&dir, "deps", "lodash, chalk").unwrap();
        let config = read_config(&dir);
        assert_eq!(config_get(&config, "deps", ""), "lodash, chalk");
        // Ensure no duplicate key was inserted.
        let text = fs::read_to_string(dir.join(CONFIG_FILE)).unwrap();
        assert_eq!(text.lines().filter(|l| l.starts_with("deps =")).count(), 1);
        fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn test_sync_package_json_with_deps() {
        let dir = temp_project();
        init_project(&dir, Some("T")).unwrap();
        set_config_key(&dir, "deps", "lodash, chalk").unwrap();
        set_config_key(&dir, "devDeps", "typescript").unwrap();
        sync_package_json(&dir).unwrap();
        let pkg = fs::read_to_string(dir.join("package.json")).unwrap();
        assert!(pkg.contains("\"lodash\": \"latest\""));
        assert!(pkg.contains("\"typescript\": \"latest\""));
        fs::remove_dir_all(&dir).ok();
    }
}
