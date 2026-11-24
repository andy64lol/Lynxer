//! Project scaffolding for `wynxer --init` and config loading for `--run`.
//!
//! Mirrors `lynxer --init`: refuses to overwrite existing files, creates a
//! source layout, a `.wynxcfg` configuration file, and a starter page/script/
//! stylesheet. `.wynxcfg` uses the same `key = value` format as
//! `lynxer.config`.

use std::collections::HashMap;
use std::fs;
use std::path::Path;

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
         \x20 .wynxcfg       project settings (name, entry, script, output, port)\n\
         \x20 .gitignore     ignores build/\n\
         \x20 build/         generated index.html, app.js, styles.css\n\n\
         Commands:\n\
         \x20 wynxer --build   generate build/\n\
         \x20 wynxer --run     build, then serve on the configured port\n\
         \x20 wynxer check     validate the project\n\n\
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
         name = {}\n\
         entry = index.wpf\n\
         script = app.wynx\n\
         output = build\n\
         port = 3000\n",
        name
    );

    let gitignore = "build/\n";

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
