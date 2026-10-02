//! bob — the Lynxer package manager.
//!
//! Named after the bobcat, a species of lynx. bob is deliberately **separate**
//! from the Lynxer interpreter for now: it carries its own version and only
//! knows which Lynxer version it supports. There is no package registry yet, so
//! the only commands are `--ver` and `--init`.

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

/// The project-relative directory that holds the manifest, the lock file and,
/// once a registry exists, the downloaded packages.
const BOB_DIRECTORY: &str = "bob";
const MANIFEST_FILE: &str = "bob.toml";
const LOCK_FILE: &str = "bob-lock.toml";
const PACKAGES_DIRECTORY: &str = "packages";

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
        Some("--init") => init_project(),
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
    println!("  bob --ver     Print the bob version and the Lynxer version it supports");
    println!("  bob --init    Create bob/bob.toml and bob/bob-lock.toml in this directory");
    println!("  bob --help    Show this help");
    println!();
    println!("There is no package registry yet, so bob cannot fetch dependencies.");
}

fn print_versions() {
    println!("bob {BOB_VERSION}");
    println!("supported lynxer {SUPPORTED_LYNXER_VERSION}");
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
         # Lynxer packages are installed under bob/packages/ once a registry\n\
         # exists. There is no registry yet, so this stays empty for now.\n\
         [dependencies]\n"
    )
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

fn relative(directory: &str, file: &str) -> String {
    Path::new(directory).join(file).display().to_string()
}
