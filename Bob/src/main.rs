//! bob — the Lynxer package manager.
//!
//! Named after the bobcat, a species of lynx. bob is deliberately **separate**
//! from the Lynxer interpreter for now: it carries its own version and only
//! knows which Lynxer version it supports. It can scaffold projects and publish
//! reusable modules to a configured Bob registry.

mod package;

use std::env;
use std::fs;
use std::path::Path;
use std::process::ExitCode;

/// The bob version, taken from `Cargo.toml`.
const BOB_VERSION: &str = env!("CARGO_PKG_VERSION");

/// The Lynxer version bob is written against. Hard-coded while bob stays
/// separate from the interpreter; `todo.md` tracks deriving it from the Lynxer
/// build so the two cannot drift.
const SUPPORTED_LYNXER_VERSION: &str = "0.1.8.2";

/// The project-relative directory that holds the manifest, lock file and,
/// once installation is implemented, the downloaded packages.
const BOB_DIRECTORY: &str = "bob";
const MANIFEST_FILE: &str = "bob.toml";
const LOCK_FILE: &str = "bob-lock.toml";
const PACKAGES_DIRECTORY: &str = "packages";

/// A module project keeps its manifest at the root and its source under `src/`,
/// the way Cargo lays out a crate.
const MODULE_FILE: &str = "module.toml";
const SOURCE_DIRECTORY: &str = "src";
const SOURCE_FILE: &str = "main.lynx";

fn main() -> ExitCode {
    let arguments: Vec<String> = env::args().skip(1).collect();
    match arguments.first().map(String::as_str) {
        Some("-h") | Some("--help") => {
            print_usage();
            ExitCode::SUCCESS
        }
        Some("-V") | Some("--ver") | Some("--version") => {
            print_versions();
            ExitCode::SUCCESS
        }
        Some("--init") => init(&arguments[1..]),
        Some("publish") => publish(&arguments[1..]),
        Some(other) => {
            eprintln!("bob: unknown option '{other}'");
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
        "  bob --init             Create bob/bob.toml and bob/bob-lock.toml in this directory"
    );
    println!("  bob --init --module    Create module.toml and src/main.lynx in this directory");
    println!("  bob publish            Package and upload this module to the configured registry");
    println!("  bob --help             Show this help");
    println!();
    println!("Publishing requires BOB_REGISTRY_URL and BOB_PUBLISH_TOKEN.");
    println!("Dependency download and installation are not implemented yet.");
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

fn print_versions() {
    println!("bob {BOB_VERSION}");
    println!("supported lynxer {SUPPORTED_LYNXER_VERSION}");
}

/// `--init` scaffolding. `--module` selects a reusable module instead of an
/// application-style project.
fn init(rest: &[String]) -> ExitCode {
    let mut module = false;
    for argument in rest {
        match argument.as_str() {
            "--module" => module = true,
            other => {
                eprintln!("bob: unknown option '{other}' for --init");
                eprintln!("bob: usage: bob --init [--module]");
                return ExitCode::FAILURE;
            }
        }
    }
    if module {
        init_module()
    } else {
        init_project()
    }
}

/// The module scaffold: `module.toml` plus `src/main.lynx`. A module is
/// imported by name rather than run, so it declares `global setup(){}` and
/// exported `global` functions, with no `main()`.
fn init_module() -> ExitCode {
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

    if manifest.exists() {
        eprintln!(
            "bob: '{}' already exists; refusing to overwrite it",
            relative(BOB_DIRECTORY, MANIFEST_FILE)
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

    println!("Created {}", relative(BOB_DIRECTORY, MANIFEST_FILE));
    println!("Created {}", relative(BOB_DIRECTORY, LOCK_FILE));
    println!("Created {}/", relative(BOB_DIRECTORY, PACKAGES_DIRECTORY));
    println!();
    println!("Project '{name}' initialized (supported lynxer {SUPPORTED_LYNXER_VERSION}).");
    ExitCode::SUCCESS
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

fn manifest_contents(name: &str) -> String {
    format!(
        "# bob project manifest. Managed by bob; see bob/README.md.\n\
         [package]\n\
         name = \"{name}\"\n\
         version = \"0.1.0\"\n\
         edition = \"2026\"\n\
         \n\
         # Lynxer packages will be installed under bob/packages/ once package\n\
         # download and installation are implemented.\n\
         [dependencies]\n"
    )
}

/// The module manifest. `{name}` is the only placeholder, so the template stays
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

// Add two integers. Returns the sum.
global add(int left, int right) -> int { return left + right; }

// Greet someone by name. Returns the greeting.
global greet(str who) -> str { return "Hello, " + who + "!"; }
"#;

fn module_manifest_contents(name: &str) -> String {
    MODULE_MANIFEST_TEMPLATE.replace("{name}", name)
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
    format!("{directory}/{file}")
}
