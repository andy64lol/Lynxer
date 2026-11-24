//! Command-line interface for the `wynxer` binary.

use clap::{Parser, Subcommand};
use std::fs;

#[derive(Parser, Debug)]
#[command(name = "wynxer", version = "0.1.0", about = "Wynxer: a web language")]
pub struct Cli {
    #[command(subcommand)]
    pub command: Option<Command>,
}

#[derive(Subcommand, Debug)]
pub enum Command {
    /// Print the Wynxer version.
    Version,
    /// List the element kinds Wynxer understands in .wpf.
    Elements,
    /// Parse a .wpf file and print its structure.
    Parse {
        /// Path to the .wpf file.
        path: String,
    },
    /// Build a Wynxer project into JS, WASM, HTML and CSS.
    Build {
        /// The project root (defaults to the current directory).
        #[arg(default_value = ".")]
        path: String,
    },
    /// Run the development server.
    Serve {
        #[arg(default_value = ".")]
        path: String,
        #[arg(long, default_value = "3000")]
        port: u16,
    },
    /// Check a Wynxer project for errors.
    Check {
        #[arg(default_value = ".")]
        path: String,
    },
    /// Scaffold a new Wynxer project in the current directory.
    Init {
        /// Optional project name (defaults to the directory name).
        name: Option<String>,
    },
    /// Build the project and serve it on the configured port.
    Run {
        #[arg(default_value = ".")]
        path: String,
        /// Override the port from .wynxcfg.
        #[arg(long)]
        port: Option<u16>,
    },
    /// Print the parsed AST of a .wynx or .wpf file.
    Ast {
        /// Path to the source file.
        path: String,
    },
    /// Lint a .wynx script or .wpf page for common issues.
    Lint {
        /// Path to the source file.
        path: String,
    },
    /// Format a .wynx script (prints to stdout; use --write to save).
    Format {
        /// Path to the .wynx file.
        path: String,
        /// Write the formatted source back to the file.
        #[arg(long)]
        write: bool,
    },
}

pub fn run() -> i32 {
    let cli = Cli::parse();
    match cli.command {
        None | Some(Command::Version) => {
            println!("wynxer {}", env!("CARGO_PKG_VERSION"));
            0
        }
        Some(Command::Elements) => {
            for kind in crate::wpf::element_kinds() {
                println!("{}", kind);
            }
            0
        }
        Some(Command::Parse { path }) => {
            match fs::read_to_string(&path) {
                Ok(source) => {
                    if path.ends_with(".wpf") {
                        match crate::wpf::parse(&source) {
                            Ok(file) => {
                                println!("imports: {}", file.imports.len());
                                for p in &file.pages {
                                    println!("page: {} (title={:?}, contents={}, scripts={})",
                                             p.name, p.title, p.contents.len(), p.scripts.len());
                                    for c in &p.contents {
                                        match c {
                                            crate::wpf::Content::Element(e) => {
                                                println!("  element.{}({}) style={:?}",
                                                         e.kind, e.args.join(", "), e.style);
                                            }
                                            crate::wpf::Content::Ignore(t) => {
                                                println!("  ignore({})", t);
                                            }
                                            crate::wpf::Content::Assignment { key, value } => {
                                                println!("  {} = {}", key, value);
                                            }
                                        }
                                    }
                                }
                                0
                            }
                            Err(e) => {
                                eprintln!("{}", crate::diagnostic::format_parse_error(&e, &source, Some(&path)));
                                1
                            }
                        }
                    } else {
                        match crate::parser::parse_module(&source) {
                            Ok(m) => {
                                println!("functions: {}", m.functions.len());
                                for f in &m.functions {
                                    println!("  fn {}({}) -> {:?}",
                                             f.name,
                                             f.params.iter().map(|(n, t)| format!("{}: {}", n, t)).collect::<Vec<_>>().join(", "),
                                             f.return_type);
                                    println!("    statements: {}", f.body.len());
                                }
                                println!("imports: {}", m.imports.len());
                                0
                            }
                            Err(e) => {
                                eprintln!("{}", crate::diagnostic::format_parse_error(&e, &source, Some(&path)));
                                1
                            }
                        }
                    }
                }
                Err(e) => {
                    eprintln!("wynxer: cannot read '{}': {}", path, e);
                    1
                }
            }
        }
        Some(Command::Build { path }) => {
            let dir = std::path::Path::new(&path);
            match crate::build::build_project(dir) {
                Ok(()) => 0,
                Err(e) => { eprintln!("wynxer: build failed: {}", e); 1 }
            }
        }
        Some(Command::Serve { path, port }) => {
            let dir = std::path::Path::new(&path);
            // Build first so there is something to serve.
            let _ = crate::build::build_project(dir);
            match crate::build::serve(&dir.join("build"), port) {
                Ok(()) => 0,
                Err(e) => { eprintln!("wynxer: serve failed: {}", e); 1 }
            }
        }
        Some(Command::Check { path }) => {
            let dir = std::path::Path::new(&path);
            match crate::build::check_project(dir) {
                Ok(()) => 0,
                Err(e) => { eprintln!("wynxer: check failed: {}", e); 1 }
            }
        }
        Some(Command::Init { name }) => {
            let dir = std::path::Path::new(".");
            match crate::init::init_project(dir, name.as_deref()) {
                Ok(msg) => { println!("{}", msg); 0 }
                Err(e) => { eprintln!("wynxer: {}", e); 1 }
            }
        }
        Some(Command::Run { path, port }) => {
            let dir = std::path::Path::new(&path);
            // Build first (mirrors `lynxer --run` building before launching).
            if let Err(e) = crate::build::build_project(dir) {
                eprintln!("wynxer: build failed: {}", e);
                return 1;
            }
            // Determine port from .wynxcfg, with an optional CLI override.
            let config = crate::init::read_config(dir);
            let port = port.unwrap_or_else(|| {
                use crate::init::config_get;
                config_get(&config, "port", "3000")
                    .parse::<u16>()
                    .unwrap_or(3000)
            });
            let output = crate::init::config_get(&config, "output", "build");
            let serve_dir = dir.join(output);
            match crate::build::serve(&serve_dir, port) {
                Ok(()) => 0,
                Err(e) => { eprintln!("wynxer: serve failed: {}", e); 1 }
            }
        }
        Some(Command::Ast { path }) => {
            match fs::read_to_string(&path) {
                Ok(source) => {
                    if path.ends_with(".wpf") {
                        match crate::wpf::parse(&source) {
                            Ok(file) => {
                                println!("{:#?}", file);
                                0
                            }
                            Err(e) => {
                                eprintln!("{}", crate::diagnostic::format_parse_error(&e, &source, Some(&path)));
                                1
                            }
                        }
                    } else {
                        match crate::parser::parse_module(&source) {
                            Ok(module) => {
                                println!("{:#?}", module);
                                0
                            }
                            Err(e) => {
                                eprintln!("{}", crate::diagnostic::format_parse_error(&e, &source, Some(&path)));
                                1
                            }
                        }
                    }
                }
                Err(e) => {
                    eprintln!("wynxer: cannot read '{}': {}", path, e);
                    1
                }
            }
        }
        Some(Command::Lint { path }) => {
            match fs::read_to_string(&path) {
                Ok(source) => {
                    let (diagnostics, had_error) = if path.ends_with(".wpf") {
                        crate::lint::lint_wpf(&source, Some(&path))
                    } else {
                        crate::lint::lint_script(&source, Some(&path))
                    };
                    for d in &diagnostics {
                        println!("{}", d.render(Some(&source)));
                    }
                    if had_error { 1 } else { 0 }
                }
                Err(e) => {
                    eprintln!("wynxer: cannot read '{}': {}", path, e);
                    1
                }
            }
        }
        Some(Command::Format { path, write }) => {
            match fs::read_to_string(&path) {
                Ok(source) => {
                    let formatted = crate::format::format_script(&source);
                    if write {
                        if let Err(e) = fs::write(&path, &formatted) {
                            eprintln!("wynxer: cannot write '{}': {}", path, e);
                            return 1;
                        }
                        println!("wynxer: formatted {}", path);
                    } else {
                        print!("{}", formatted);
                    }
                    0
                }
                Err(e) => {
                    eprintln!("wynxer: cannot read '{}': {}", path, e);
                    1
                }
            }
        }
    }
}
