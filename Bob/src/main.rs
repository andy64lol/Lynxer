//! bob — the Lynxer package manager.
//!
//! Named after the bobcat, a species of lynx. bob is deliberately **separate**
//! from the Lynxer interpreter for now: it carries its own version and only
//! knows which Lynxer version it supports. It can scaffold projects and publish
//! reusable modules to a configured Bob registry.

mod package;

use package::{Registry, RegistryEntry};
use std::env;
use std::fs;
use std::fs::File;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::{Command, ExitCode};
use toml::Value;

/// The bob version, taken from `Cargo.toml`.
const BOB_VERSION: &str = "0.1.3";

/// Parse the `bob.toml` file to get the project name.
fn read_bob_toml(path: &Path) -> Result<String, String> {
    let mut file = File::open(path).map_err(|error| format!("cannot open bob.toml: {error}"))?;
    let mut contents = String::new();
    file.read_to_string(&mut contents)
        .map_err(|error| format!("cannot read bob.toml: {error}"))?;
    let value: Value = contents
        .parse()
        .map_err(|error| format!("cannot parse bob.toml: {error}"))?;
    value
        .get("package")
        .and_then(|v| v.get("name"))
        .and_then(|v| v.as_str())
        .map(|s| s.to_string())
        .ok_or("no 'name' field in bob.toml".to_string())
}

/// The Lynxer version bob is written against. Hard-coded while bob stays
/// separate from the interpreter; `todo.md` tracks deriving it from the Lynxer
/// build so the two cannot drift.
const SUPPORTED_LYNXER_VERSION: &str = "0.1.8.3";

/// The project-relative directory that holds the manifest, lock file and,
/// once installation is implemented, the downloaded packages.
const BOB_DIRECTORY: &str = "bob";
const MANIFEST_FILE: &str = "bob.toml";
const LOCK_FILE: &str = "bob-lock.toml";
const PACKAGES_DIRECTORY: &str = "packages";

/// The project manifest template. Same shape as a Cargo `[package]` table, with
/// an explicit `entry` so the Lynxer entry point is never implicit.
const PROJECT_MANIFEST_TEMPLATE: &str = r#"# bob project manifest. Managed by bob; see bob/README.md.
[package]
name = "{name}"
version = "0.1.0"
edition = "2026"
entry = "src/main.lynx"

# Lynxer packages will be installed under bob/packages/ once package
# download and installation are implemented.
[dependencies]
"#;

/// Default application source for `bob --init`.
const PROJECT_SOURCE_TEMPLATE: &str = r#"global setup(){}
global main(){println("Hello world");}
"#;

/// The module manifest template. `{name}` is the only placeholder, so the template stays
/// a plain string and never has to escape Lynxer's braces.
const MODULE_MANIFEST_TEMPLATE: &str = r#"# bob module manifest. Managed by bob; see bob/README.md.
[module]
name = "{name}"
version = "0.1.0"
edition = "2026"
entry = "src/main.lynx"

# Module dependencies will be installed under bob/packages/ once package
# download and installation are implemented.
[dependencies]
"#;

/// The module source skeleton, in the shape a pure Lynxer stdlib module uses:
/// a `////` documentation header, an empty `global setup()`, and exported
/// `global` functions.
const MODULE_SOURCE_TEMPLATE: &str = r#"////
{name}: a Lynxer module.
Replace this header with what the module does; `lynxer --list-stdlibs` prints
it for an installed module.
////

global setup(){}

// Main entry point for the module.
global main(){println("Hello world");}
"#;

/// A module project keeps its manifest at the root and its source under `src/`,
/// the way Cargo lays out a crate.
const MODULE_FILE: &str = "module.toml";
const SOURCE_DIRECTORY: &str = "src";
const SOURCE_FILE: &str = "main.lynx";

/// `--install-exec` writes the bob executable under this prefix; the
/// `BOB_PREFIX` environment variable overrides the platform default, the way
/// `LYNXER_PREFIX` does for Lynxer.
const PREFIX_ENVIRONMENT: &str = "BOB_PREFIX";

fn main() -> ExitCode {
    let arguments: Vec<String> = env::args().skip(1).collect();
    match arguments.first().map(|s| s.as_str()) {
        Some("--help") => {
            print_usage();
            ExitCode::SUCCESS
        }
        Some("--ver") | Some("--version") => {
            print_versions();
            ExitCode::SUCCESS
        }
        Some("--init") => init(&arguments[1..]),
        Some("--init-module") => init_module(&arguments[1..]),
        Some("--install-exec") => install_exec(&arguments[1..]),
        Some("--uninstall-exec") => uninstall_exec(&arguments[1..]),
        Some("publish") => publish(&arguments[1..]),
        Some("install") => install(&arguments[1..]),
        Some("run") => run(&arguments[1..]),
        Some("registry") => registry(&arguments[1..]),
        Some("config") => config_command(&arguments[1..]),
        Some(other) => {
            eprintln!("bob: unknown option '{}'", other);
            eprintln!();
            print_usage();
            ExitCode::FAILURE
        }
        None => {
            print_usage();
            ExitCode::FAILURE
        }
    }
}

fn print_usage() {
    println!("bob — the Lynxer package manager");
    println!();
    println!("Usage:");
    println!("  bob --ver              Print the bob version and the Lynxer version it supports");
    println!(
        "  bob --init             Create bob/bob.toml, bob/bob-lock.toml, and src/main.lynx"
    );
    println!("  bob --init-module      Create module.toml and src/main.lynx in this directory");
    println!("  bob --install-exec     Install the bob executable under the prefix (BOB_PREFIX)");
    println!("  bob --uninstall-exec   Remove installed bob (also ~/.local/bin and similar)");
    println!("  bob run [command]       Compile and run the project");
    println!("                          If no command is specified, defaults to src/main.lynx");
    println!(
        "                          Custom run commands can be defined in bob.toml under [run]"
    );
    println!("  bob publish            Package and upload this module to GitHub Releases");
    println!("  bob install <name> <version>  Install dependencies from bob.toml");
    println!("  bob registry add <name> <owner> <repository>  Add a custom package mapping");
    println!("  bob registry list       List all package mappings (default and custom)");
    println!("  bob registry config     Configure custom package mappings interactively");
    println!("  bob config set <key> <value>  Set rest-api or github-token");
    println!("  bob config get <key>    Print one configuration value");
    println!("  bob config show         Print the configuration (token masked)");
    println!("  bob config unset <key>  Remove a configuration value");
    println!("  bob config path         Print the configuration file path");
    println!("  bob --help             Show this help");
    println!();
    println!("Publishing and installing need a GitHub token and resolve packages via the");
    println!("REST registry when one is configured (env overrides: BOB_REST_API, GITHUB_TOKEN).");
    println!("Configuration is stored in ~/.bob/config.json.");
}

fn publish(rest: &[String]) -> ExitCode {
    if !rest.is_empty() {
        eprintln!("bob: 'publish' does not take arguments");
        eprintln!("bob: usage: bob publish");
        return ExitCode::FAILURE;
    }
    match package::publish_current_module() {
        Ok(message) => {
            println!("{message}");
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("bob: {error}");
            ExitCode::FAILURE
        }
    }
}

fn install(rest: &[String]) -> ExitCode {
    if rest.len() != 2 {
        eprintln!("bob: 'install' requires a name and version");
        eprintln!("bob: usage: bob install <name> <version>");
        return ExitCode::FAILURE;
    }
    let name = &rest[0];
    let version = &rest[1];
    match package::install_package(name, version) {
        Ok(message) => {
            println!("{message}");
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("bob: {error}");
            ExitCode::FAILURE
        }
    }
}

fn run(rest: &[String]) -> ExitCode {
    // Check if the project has a bob.toml file
    let bob_toml = Path::new("bob.toml");
    if !bob_toml.exists() {
        eprintln!("bob: no 'bob.toml' found in the project directory");
        eprintln!("bob: run this command from the root of a Lynxer project");
        return ExitCode::FAILURE;
    }

    // Parse bob.toml to get the project name
    let _project_name = match read_bob_toml(bob_toml) {
        Ok(name) => name,
        Err(error) => {
            eprintln!("bob: failed to parse bob.toml: {error}");
            return ExitCode::FAILURE;
        }
    };

    // Determine the entry point
    let entry_point = if !rest.is_empty() {
        rest[0].clone()
    } else {
        "src/main.lynx".to_string()
    };

    // Check if the entry point exists
    let entry_path = Path::new(&entry_point);
    if !entry_path.exists() {
        eprintln!("bob: entry point '{}' not found", entry_point);
        return ExitCode::FAILURE;
    }

    // Compile the project using lynxer
    let compile_status = Command::new("lynxer").arg("build").status();
    if compile_status.is_err() {
        eprintln!("bob: failed to compile");
        return ExitCode::FAILURE;
    }

    if !compile_status.unwrap().success() {
        eprintln!("bob: compilation failed");
        return ExitCode::FAILURE;
    }

    // Execute the compiled binary
    let binary_path = Path::new("./target/debug/bob");
    if !binary_path.exists() {
        eprintln!(
            "bob: compiled binary not found at: {}",
            binary_path.display()
        );
        return ExitCode::FAILURE;
    }

    let run_status = Command::new(&binary_path).status();
    if run_status.is_err() {
        eprintln!("bob: failed to execute");
        return ExitCode::FAILURE;
    }

    if !run_status.unwrap().success() {
        eprintln!("bob: execution failed");
        return ExitCode::FAILURE;
    }

    ExitCode::SUCCESS
}

/// `--install-exec` installs the running bob binary, mirroring `lynxer
/// --install`. bob is self-contained, so it is copied straight into
/// `<prefix>/bin` (`BOB_PREFIX` overrides the platform default) where it can be
/// run from any directory.
fn install_exec(rest: &[String]) -> ExitCode {
    if let Some(other) = rest.first() {
        eprintln!("bob: unknown option '{other}' for --install-exec");
        eprintln!("bob: usage: bob --install-exec");
        return ExitCode::FAILURE;
    }
    match install_self() {
        Ok(message) => {
            println!("{message}");
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("bob: install failed: {error}");
            #[cfg(not(windows))]
            eprintln!("bob: re-run with permission to write the prefix (for example with sudo)");
            ExitCode::FAILURE
        }
    }
}

fn install_self() -> Result<String, String> {
    let source = env::current_exe()
        .map_err(|error| format!("cannot locate the running executable: {error}"))?;
    let prefix = install_prefix();
    let bin_directory = prefix.join("bin");
    let target = bin_directory.join(executable_file_name());

    fs::create_dir_all(&bin_directory)
        .map_err(|error| format!("cannot create '{}': {error}", bin_directory.display()))?;

    // Re-installing over the same file (including the running one) is a no-op.
    if !same_file(&source, &target) {
        fs::copy(&source, &target).map_err(|error| {
            format!(
                "cannot copy '{}' to '{}': {error}",
                source.display(),
                target.display()
            )
        })?;
    }
    set_executable(&target)?;

    let mut message = format!("Installed {}", target.display());
    if !directory_on_path(&bin_directory) {
        message.push_str(&format!(
            "\nAdd {} to PATH to run `bob` from any directory",
            bin_directory.display()
        ));
    }
    if let Some(shadow) = first_bob_on_path() {
        if !same_file(&shadow, &target) {
            message.push_str(&format!(
                "\nbob: warning: '{}' is earlier on PATH and will still be used; \
remove it or adjust PATH (or reinstall with matching BOB_PREFIX)",
                shadow.display()
            ));
        }
    }
    Ok(message)
}

/// The prefix `--install-exec` writes into: `BOB_PREFIX` when set, else the
/// platform default.
fn install_prefix() -> PathBuf {
    if let Ok(value) = env::var(PREFIX_ENVIRONMENT) {
        if !value.is_empty() {
            return PathBuf::from(value);
        }
    }
    default_install_prefix()
}

/// `/usr` on Unix; on Windows a per-user directory that needs no elevation,
/// the way Windows tools such as VS Code install themselves by default.
fn default_install_prefix() -> PathBuf {
    #[cfg(windows)]
    {
        if let Ok(local) = env::var("LOCALAPPDATA") {
            if !local.is_empty() {
                return PathBuf::from(local).join("Programs").join("Bob");
            }
        }
        if let Ok(profile) = env::var("USERPROFILE") {
            if !profile.is_empty() {
                return PathBuf::from(profile).join("Bob");
            }
        }
        PathBuf::from("Bob")
    }
    #[cfg(not(windows))]
    {
        PathBuf::from("/usr")
    }
}

fn executable_file_name() -> &'static str {
    if cfg!(windows) {
        "bob.exe"
    } else {
        "bob"
    }
}

fn same_file(left: &Path, right: &Path) -> bool {
    match (fs::canonicalize(left), fs::canonicalize(right)) {
        (Ok(left), Ok(right)) => left == right,
        _ => false,
    }
}

fn set_executable(path: &Path) -> Result<(), String> {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(path, fs::Permissions::from_mode(0o755))
            .map_err(|error| format!("cannot mark '{}' executable: {error}", path.display()))?;
    }
    #[cfg(not(unix))]
    {
        let _ = path;
    }
    Ok(())
}

/// True when `directory` is one of the entries in `PATH`, so install only tells
/// the user to update `PATH` when it actually needs to.
fn directory_on_path(directory: &Path) -> bool {
    let path = match env::var("PATH") {
        Ok(value) => value,
        Err(_) => return false,
    };
    let separator = if cfg!(windows) { ';' } else { ':' };
    let wanted = match fs::canonicalize(directory) {
        Ok(w) => Some(w),
        Err(_) => None,
    };
    for entry in path.split(separator) {
        if entry.is_empty() {
            continue;
        }
        let candidate_path = PathBuf::from(entry);
        if let Ok(candidate) = fs::canonicalize(&candidate_path) {
            if let Some(wanted_path) = &wanted {
                if candidate == *wanted_path {
                    return true;
                }
            }
        }
    }
    false
}

/// `--uninstall-exec` removes the executable `--install-exec` wrote, mirroring
/// `lynxer --uninstall`.
///
/// With `BOB_PREFIX` set, only that prefix is touched. With it unset, bob also
/// sweeps common install locations (`/usr/bin`, `/usr/local/bin`,
/// `$HOME/.local/bin`, and a running `…/bin/bob`) so a leftover
/// `BOB_PREFIX=$HOME/.local` install is cleared without remembering the prefix.
fn uninstall_exec(rest: &[String]) -> ExitCode {
    if let Some(other) = rest.first() {
        eprintln!("bob: unknown option '{other}' for --uninstall-exec");
        eprintln!("bob: usage: bob --uninstall-exec");
        return ExitCode::FAILURE;
    }

    let candidates = uninstall_candidate_paths();
    let mut removed = Vec::new();
    let mut permission_errors = Vec::new();

    for target in &candidates {
        match fs::remove_file(target) {
            Ok(()) => removed.push(target.clone()),
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
            Err(error) if error.kind() == std::io::ErrorKind::PermissionDenied => {
                permission_errors.push(format!("{}: {error}", target.display()));
            }
            Err(error) => {
                eprintln!("bob: could not remove '{}': {error}", target.display());
                #[cfg(not(windows))]
                eprintln!(
                    "bob: re-run with permission to write the install directory (for example with sudo)"
                );
                return ExitCode::FAILURE;
            }
        }
    }

    if !removed.is_empty() {
        for path in &removed {
            println!("Removed {}", path.display());
        }
        return ExitCode::SUCCESS;
    }

    if !permission_errors.is_empty() {
        for error in &permission_errors {
            eprintln!("bob: could not remove {error}");
        }
        #[cfg(not(windows))]
        eprintln!(
            "bob: re-run with permission to write the install directory (for example with sudo)"
        );
        return ExitCode::FAILURE;
    }

    let primary = &candidates[0];
    eprintln!("bob: nothing to uninstall at {}", primary.display());
    if env::var_os(PREFIX_ENVIRONMENT).is_some() {
        eprintln!("bob: --uninstall-exec must use the same BOB_PREFIX as --install-exec");
    } else {
        eprintln!(
            "bob: also checked common locations (for example \"$HOME/.local/bin\"); \
pass BOB_PREFIX if you installed elsewhere"
        );
    }
    ExitCode::FAILURE
}

/// Paths `--uninstall-exec` should try to remove.
///
/// Always includes the active prefix. When `BOB_PREFIX` is unset, also includes
/// common user/system install locations and the running binary when it looks
/// like an install (`…/bin/bob`).
fn uninstall_candidate_paths() -> Vec<PathBuf> {
    let name = executable_file_name();
    let mut paths = Vec::new();
    let push_unique = |paths: &mut Vec<PathBuf>, path: PathBuf| {
        if !paths.iter().any(|existing| same_file(existing, &path) || existing == &path) {
            paths.push(path);
        }
    };

    push_unique(
        &mut paths,
        install_prefix().join("bin").join(name),
    );

    if env::var_os(PREFIX_ENVIRONMENT).is_none() {
        #[cfg(not(windows))]
        {
            if let Some(home) = dirs::home_dir() {
                push_unique(&mut paths, home.join(".local").join("bin").join(name));
            }
            push_unique(&mut paths, PathBuf::from("/usr/local/bin").join(name));
        }
        #[cfg(windows)]
        {
            if let Ok(local) = env::var("LOCALAPPDATA") {
                if !local.is_empty() {
                    push_unique(
                        &mut paths,
                        PathBuf::from(local)
                            .join("Programs")
                            .join("Bob")
                            .join("bin")
                            .join(name),
                    );
                }
            }
            if let Some(home) = dirs::home_dir() {
                push_unique(&mut paths, home.join(".local").join("bin").join(name));
            }
        }

        if let Ok(running) = env::current_exe() {
            let running = fs::canonicalize(&running).unwrap_or(running);
            if looks_like_installed_bob(&running) {
                push_unique(&mut paths, running);
            }
        }
    }

    paths
}

/// True for `<prefix>/bin/bob` (or `bob.exe`) layouts produced by `--install-exec`.
fn looks_like_installed_bob(path: &Path) -> bool {
    let file_name = path.file_name().and_then(|name| name.to_str());
    if file_name != Some(executable_file_name()) {
        return false;
    }
    path.parent()
        .and_then(|parent| parent.file_name())
        .and_then(|name| name.to_str())
        == Some("bin")
}

/// The first `bob` executable found on `PATH`, if any.
fn first_bob_on_path() -> Option<PathBuf> {
    let path = match env::var_os("PATH") {
        Some(p) => p,
        None => return None,
    };
    let name = executable_file_name();
    for entry in env::split_paths(&path) {
        if entry.as_os_str().is_empty() {
            continue;
        }
        let candidate = entry.join(name);
        if candidate.is_file() {
            return Some(candidate);
        }
    }
    None
}

fn registry(rest: &[String]) -> ExitCode {
    if rest.is_empty() {
        eprintln!("bob: 'registry' requires a subcommand");
        eprintln!("bob: usage: bob registry add <name> <owner> <repository>");
        eprintln!("bob: usage: bob registry list");
        eprintln!("bob: usage: bob registry config");
        return ExitCode::FAILURE;
    }
    match rest[0].as_str() {
        "add" => add_registry_mapping(&rest[1..]),
        "list" => list_registry_mappings(),
        "config" => config_registry(),
        _ => {
            eprintln!("bob: unknown registry subcommand '{}'", rest[0]);
            eprintln!("bob: usage: bob registry add <name> <owner> <repository>");
            eprintln!("bob: usage: bob registry list");
            eprintln!("bob: usage: bob registry config");
            ExitCode::FAILURE
        }
    }
}

fn config_command(rest: &[String]) -> ExitCode {
    if rest.is_empty() {
        eprintln!("bob: 'config' requires a subcommand");
        print_config_usage();
        return ExitCode::FAILURE;
    }
    match rest[0].as_str() {
        "set" => config_set(&rest[1..]),
        "get" => config_get(&rest[1..]),
        "show" => config_show(),
        "unset" => config_unset(&rest[1..]),
        "path" => {
            println!("{}", package::config_path().display());
            ExitCode::SUCCESS
        }
        other => {
            eprintln!("bob: unknown config subcommand '{}'", other);
            print_config_usage();
            ExitCode::FAILURE
        }
    }
}

fn print_config_usage() {
    eprintln!("bob: usage: bob config set <rest-api|github-token> <value>");
    eprintln!("bob: usage: bob config get <rest-api|github-token>");
    eprintln!("bob: usage: bob config show");
    eprintln!("bob: usage: bob config unset <rest-api|github-token>");
    eprintln!("bob: usage: bob config path");
}

/// Set a configuration value in `~/.bob/config.json`.
fn config_set(rest: &[String]) -> ExitCode {
    if rest.len() != 2 {
        eprintln!("bob: 'config set' requires a key and a value");
        print_config_usage();
        return ExitCode::FAILURE;
    }
    let key = rest[0].as_str();
    let value = rest[1].trim();
    if value.is_empty() {
        eprintln!("bob: config value must not be empty");
        return ExitCode::FAILURE;
    }

    let mut config = package::load_config();
    match key {
        "rest-api" => {
            if !is_http_url(value) {
                eprintln!("bob: 'rest-api' must be an http:// or https:// URL");
                return ExitCode::FAILURE;
            }
            config.rest_api = Some(value.trim_end_matches('/').to_string());
        }
        "github-token" => config.github_token = Some(value.to_string()),
        other => {
            eprintln!("bob: unknown config key '{}'", other);
            eprintln!("bob: valid keys: rest-api, github-token");
            return ExitCode::FAILURE;
        }
    }

    match package::save_config(&config) {
        Ok(()) => {
            println!("Set {} in {}", key, package::config_path().display());
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("bob: {error}");
            ExitCode::FAILURE
        }
    }
}

/// Print one configuration value; the token is masked.
fn config_get(rest: &[String]) -> ExitCode {
    if rest.len() != 1 {
        eprintln!("bob: 'config get' requires a key");
        print_config_usage();
        return ExitCode::FAILURE;
    }
    let config = package::load_config();
    match rest[0].as_str() {
        "rest-api" => match &config.rest_api {
            Some(value) => println!("{value}"),
            None => println!("(unset)"),
        },
        "github-token" => match &config.github_token {
            Some(value) => println!("{}", mask_token(value)),
            None => println!("(unset)"),
        },
        other => {
            eprintln!("bob: unknown config key '{}'", other);
            eprintln!("bob: valid keys: rest-api, github-token");
            return ExitCode::FAILURE;
        }
    }
    ExitCode::SUCCESS
}

/// Print the whole configuration; the token is masked.
fn config_show() -> ExitCode {
    let config = package::load_config();
    match &config.rest_api {
        Some(value) => println!("rest-api = {value}"),
        None => println!("rest-api = (unset)"),
    }
    match &config.github_token {
        Some(value) => println!("github-token = {}", mask_token(value)),
        None => println!("github-token = (unset)"),
    }
    println!("config file: {}", package::config_path().display());
    ExitCode::SUCCESS
}

/// Remove a configuration value from `~/.bob/config.json`.
fn config_unset(rest: &[String]) -> ExitCode {
    if rest.len() != 1 {
        eprintln!("bob: 'config unset' requires a key");
        print_config_usage();
        return ExitCode::FAILURE;
    }
    let key = rest[0].as_str();
    let mut config = package::load_config();
    match key {
        "rest-api" => config.rest_api = None,
        "github-token" => config.github_token = None,
        other => {
            eprintln!("bob: unknown config key '{}'", other);
            eprintln!("bob: valid keys: rest-api, github-token");
            return ExitCode::FAILURE;
        }
    }
    match package::save_config(&config) {
        Ok(()) => {
            println!("Unset {key}");
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("bob: {error}");
            ExitCode::FAILURE
        }
    }
}

/// A minimal http/https URL check for the REST registry link.
fn is_http_url(value: &str) -> bool {
    (value.starts_with("http://") && value.len() > "http://".len())
        || (value.starts_with("https://") && value.len() > "https://".len())
}

/// Mask a token for display, keeping a short prefix/suffix for recognisability.
fn mask_token(token: &str) -> String {
    let count = token.chars().count();
    if count <= 8 {
        return "*".repeat(count.max(1));
    }
    let prefix: String = token.chars().take(4).collect();
    let suffix: String = token.chars().skip(count - 4).collect();
    format!("{prefix}****{suffix}")
}

fn print_versions() {
    println!("bob {}", BOB_VERSION);
    println!("supported lynxer {}", SUPPORTED_LYNXER_VERSION);
}

/// `--init` scaffolding: an application-style project.
fn init(rest: &[String]) -> ExitCode {
    if let Some(other) = rest.first() {
        eprintln!("bob: unknown option '{other}' for --init");
        eprintln!("bob: usage: bob --init");
        return ExitCode::FAILURE;
    }
    init_project()
}

/// The module scaffold: `module.toml` plus `src/main.lynx`. A module is
/// imported by name rather than run, so it declares `global setup(){}` and
/// exported `global` functions, with no `main()`.
fn init_module(rest: &[String]) -> ExitCode {
    if let Some(other) = rest.first() {
        eprintln!("bob: unknown option '{other}' for --init-module");
        eprintln!("bob: usage: bob --init-module");
        return ExitCode::FAILURE;
    }
    let directory = match env::current_dir() {
        Ok(directory) => directory,
        Err(error) => {
            eprintln!("bob: cannot determine the current directory: {error}");
            return ExitCode::FAILURE;
        }
    };
    let name = project_name(&directory);

    let manifest = directory.join(MODULE_FILE);
    let source_directory = directory.join(SOURCE_DIRECTORY);
    let source = source_directory.join(SOURCE_FILE);

    if manifest.exists() {
        eprintln!("bob: '{MODULE_FILE}' already exists; refusing to overwrite it");
        return ExitCode::FAILURE;
    }
    if source.exists() {
        eprintln!(
            "bob: '{}/{}' already exists; refusing to overwrite it",
            SOURCE_DIRECTORY, SOURCE_FILE
        );
        return ExitCode::FAILURE;
    }

    if let Err(error) = fs::create_dir_all(&source_directory) {
        eprintln!(
            "bob: cannot create '{}': {error}",
            source_directory.display()
        );
        return ExitCode::FAILURE;
    }
    if let Err(error) = fs::write(&manifest, module_manifest_contents(&name)) {
        eprintln!("bob: cannot write '{}': {error}", manifest.display());
        return ExitCode::FAILURE;
    }
    if let Err(error) = fs::write(&source, module_source_contents(&name)) {
        eprintln!("bob: cannot write '{}': {error}", source.display());
        return ExitCode::FAILURE;
    }

    println!("Created {MODULE_FILE}");
    println!("Created {SOURCE_DIRECTORY}/{SOURCE_FILE}");
    println!();
    println!("Module '{name}' initialized (supported lynxer {SUPPORTED_LYNXER_VERSION}).");
    ExitCode::SUCCESS
}

fn init_project() -> ExitCode {
    let directory = match env::current_dir() {
        Ok(directory) => directory,
        Err(error) => {
            eprintln!("bob: cannot determine the current directory: {error}");
            return ExitCode::FAILURE;
        }
    };
    let name = project_name(&directory);

    let bob_directory = directory.join(BOB_DIRECTORY);
    let manifest = bob_directory.join(MANIFEST_FILE);
    let lock = bob_directory.join(LOCK_FILE);
    let packages = bob_directory.join(PACKAGES_DIRECTORY);
    let source_directory = directory.join(SOURCE_DIRECTORY);
    let source = source_directory.join(SOURCE_FILE);

    if manifest.exists() {
        eprintln!(
            "bob: '{}' already exists; refusing to overwrite it",
            relative(BOB_DIRECTORY, MANIFEST_FILE)
        );
        return ExitCode::FAILURE;
    }
    if source.exists() {
        eprintln!(
            "bob: '{}/{}' already exists; refusing to overwrite it",
            SOURCE_DIRECTORY, SOURCE_FILE
        );
        return ExitCode::FAILURE;
    }

    if let Err(error) = fs::create_dir_all(&packages) {
        eprintln!("bob: cannot create '{}': {error}", packages.display());
        return ExitCode::FAILURE;
    }
    if let Err(error) = fs::write(&manifest, manifest_contents(&name)) {
        eprintln!("bob: cannot write '{}': {error}", manifest.display());
        return ExitCode::FAILURE;
    }
    if let Err(error) = fs::write(&lock, lock_contents(&name)) {
        eprintln!("bob: cannot write '{}': {error}", lock.display());
        return ExitCode::FAILURE;
    }

    if let Err(error) = fs::create_dir_all(&source_directory) {
        eprintln!(
            "bob: cannot create '{}': {error}",
            source_directory.display()
        );
        return ExitCode::FAILURE;
    }
    if let Err(error) = fs::write(&source, PROJECT_SOURCE_TEMPLATE) {
        eprintln!("bob: cannot write '{}': {error}", source.display());
        return ExitCode::FAILURE;
    }

    println!("Created {}", relative(BOB_DIRECTORY, MANIFEST_FILE));
    println!("Created {}", relative(BOB_DIRECTORY, LOCK_FILE));
    println!("Created {}/", relative(BOB_DIRECTORY, PACKAGES_DIRECTORY));
    println!("Created {}/{}", SOURCE_DIRECTORY, SOURCE_FILE);
    println!();
    println!(
        "Project '{}' initialized (supported lynxer {}).",
        name, SUPPORTED_LYNXER_VERSION
    );
    ExitCode::SUCCESS
}

/// Add a custom package mapping to the user's registry.
fn add_registry_mapping(rest: &[String]) -> ExitCode {
    if rest.len() != 3 {
        eprintln!("bob: 'registry add' requires a name, owner, and repository");
        eprintln!("bob: usage: bob registry add <name> <owner> <repository>");
        return ExitCode::FAILURE;
    }
    let name = &rest[0];
    let owner = &rest[1];
    let repository = &rest[2];

    let registry_path = get_registry_path();
    let mut registry = load_registry(&registry_path);

    if registry.packages.contains_key(name) {
        eprintln!("bob: package '{}' already exists in the registry", name);
        return ExitCode::FAILURE;
    }

    registry.packages.insert(
        name.to_string(),
        RegistryEntry {
            owner: owner.to_string(),
            repository: repository.to_string(),
        },
    );

    if let Err(error) = save_registry(&registry, &registry_path) {
        eprintln!("bob: cannot save registry: {error}");
        return ExitCode::FAILURE;
    }

    println!("Added package '{}' to the registry", name);
    ExitCode::SUCCESS
}

/// List all custom package mappings in the user's registry.
fn list_registry_mappings() -> ExitCode {
    println!("Package mappings are handled internally by Bob.");
    println!("Use 'bob registry config' to add custom mappings.");
    ExitCode::SUCCESS
}

/// Configure custom package mappings interactively.
fn config_registry() -> ExitCode {
    println!("Bob Registry Configuration");
    println!("-----------------------");
    println!();
    println!("This will guide you through setting up custom package mappings.");
    println!("Mappings are stored in ~/.bob/registry.json.");
    println!();

    let registry_path = get_registry_path();
    let mut registry = load_registry(&registry_path);

    loop {
        println!("Enter the package name (or 'done' to finish):");
        let mut name_input = String::new();
        std::io::stdin()
            .read_line(&mut name_input)
            .expect("Failed to read input");
        let name = name_input.trim();

        if name.eq_ignore_ascii_case("done") {
            break;
        }

        println!("Enter the GitHub owner for {}:", name);
        let mut owner_input = String::new();
        std::io::stdin()
            .read_line(&mut owner_input)
            .expect("Failed to read input");
        let owner = owner_input.trim().to_string();

        println!("Enter the GitHub repository for {}:", name);
        let mut repository_input = String::new();
        std::io::stdin()
            .read_line(&mut repository_input)
            .expect("Failed to read input");
        let repository = repository_input.trim().to_string();

        registry.packages.insert(
            name.to_string(),
            RegistryEntry {
                owner: owner.clone(),
                repository: repository.clone(),
            },
        );

        println!(
            "Added package '{}' with owner '{}' and repository '{}'",
            name, owner, repository
        );
    }

    if let Err(error) = save_registry(&registry, &registry_path) {
        eprintln!("bob: cannot save registry: {error}");
        return ExitCode::FAILURE;
    }

    println!();
    println!("Registry configuration saved to ~/.bob/registry.json");
    println!("You can now use 'bob install' to install packages.");
    ExitCode::SUCCESS
}

/// Get the path to the user's registry file.
fn get_registry_path() -> PathBuf {
    let home_dir = match dirs::home_dir() {
        Some(dir) => dir,
        None => {
            eprintln!("bob: cannot determine home directory");
            std::process::exit(1);
        }
    };
    home_dir.join(".bob").join("registry.json")
}

/// Load the registry from the user's registry file.
fn load_registry(path: &Path) -> Registry {
    if !path.exists() {
        return Registry {
            packages: Default::default(),
        };
    }

    let content = match fs::read_to_string(path) {
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

/// Save the registry to the user's registry file.
fn save_registry(registry: &Registry, path: &Path) -> Result<(), String> {
    let parent = path
        .parent()
        .ok_or_else(|| "cannot determine parent directory")?;
    if let Err(error) = fs::create_dir_all(parent) {
        return Err(format!("cannot create directory: {error}"));
    }

    let content = serde_json::to_string_pretty(registry)
        .map_err(|error| format!("cannot serialize registry: {error}"))?;
    fs::write(path, content).map_err(|error| format!("cannot write registry: {error}"))
}

/// A manifest is named after its directory, the way Cargo names a crate.
fn project_name(directory: &Path) -> String {
    let candidate = directory
        .file_name()
        .and_then(|name| name.to_str())
        .unwrap_or("lynxer-project");
    let mut name = String::with_capacity(candidate.len());
    for character in candidate.chars() {
        if character.is_ascii_alphanumeric() || character == '-' || character == '_' {
            name.push(character);
        } else {
            name.push('-');
        }
    }
    let trimmed = name.trim_matches('-');
    if trimmed.is_empty() {
        "lynxer-project".to_string()
    } else {
        trimmed.to_string()
    }
}

fn module_manifest_contents(name: &str) -> String {
    MODULE_MANIFEST_TEMPLATE.replace("{name}", name)
}

fn manifest_contents(name: &str) -> String {
    PROJECT_MANIFEST_TEMPLATE.replace("{name}", name)
}

fn module_source_contents(name: &str) -> String {
    MODULE_SOURCE_TEMPLATE.replace("{name}", name)
}

fn lock_contents(name: &str) -> String {
    format!(
        "# Generated by bob. Do not edit by hand.\n\
         version = 1\n\
         \n\
         [[package]]\n\
         name = \"{name}\"\n\
         version = \"0.1.0\"\n"
    )
}

/// A project-relative path for display. Always uses `/`, so bob's output is the
/// same on Linux, macOS and Windows.
fn relative(directory: &str, file: &str) -> String {
    format!("{}/{}", directory, file)
}
