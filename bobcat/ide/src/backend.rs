//! Pure-Rust backend for the native IDE.
//!
//! This is the old Tauri `src-tauri/src/lib.rs` command layer, rewritten as
//! plain functions: file I/O, native dialogs (rfd) and running the `lynxer`
//! interpreter with a timeout. No Tauri, no webview, no JS bridge.
//!
//! The IDE has a hard runtime dependency on the `lynxer` executable: the
//! startup check and every Run / Lint / Format action shell out to it.

use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::time::{Duration, Instant};

#[derive(Debug, Clone)]
pub struct RunResult {
    pub stdout: String,
    pub stderr: String,
    pub code: i32,
    pub timed_out: bool,
    /// The interpreter could not be started at all (missing / not executable).
    pub spawn_error: bool,
}

#[derive(Debug, Clone)]
pub struct FileEntry {
    pub name: String,
    pub path: PathBuf,
    pub is_dir: bool,
}

pub fn list_dir(path: &Path) -> Result<Vec<FileEntry>, String> {
    if !path.is_dir() {
        return Err(format!("Not a directory: {}", path.display()));
    }
    let mut out = Vec::new();
    let entries = std::fs::read_dir(path)
        .map_err(|e| format!("Cannot list {}: {e}", path.display()))?;
    for entry in entries {
        let entry = entry
            .map_err(|e| format!("Cannot read an entry in {}: {e}", path.display()))?;
        let md = entry
            .metadata()
            .map_err(|e| format!("Cannot stat {}: {e}", entry.path().display()))?;
        out.push(FileEntry {
            name: entry.file_name().to_string_lossy().to_string(),
            path: entry.path(),
            is_dir: md.is_dir(),
        });
    }
    out.sort_by(|a, b| {
        b.is_dir
            .cmp(&a.is_dir)
            .then(a.name.to_lowercase().cmp(&b.name.to_lowercase()))
    });
    Ok(out)
}

pub fn read_file(path: &Path) -> Result<String, String> {
    if !path.is_file() {
        return Err(format!("Not a file: {}", path.display()));
    }
    std::fs::read_to_string(path)
        .map_err(|e| format!("Cannot read {} as UTF-8 text: {e}", path.display()))
}

fn write(path: &Path, content: &str) -> Result<(), String> {
    if path.is_dir() {
        return Err(format!("Cannot write: {} is a directory", path.display()));
    }
    if let Some(parent) = path.parent() {
        if !parent.as_os_str().is_empty() {
            std::fs::create_dir_all(parent)
                .map_err(|e| format!("Cannot create {}: {e}", parent.display()))?;
        }
    }
    std::fs::write(path, content)
        .map_err(|e| format!("Cannot write {}: {e}", path.display()))
}

pub fn write_file(path: &Path, content: &str) -> Result<(), String> {
    write(path, content)
}

pub fn new_file(path: &Path, content: &str) -> Result<(), String> {
    write(path, content)
}

pub fn pick_file(start: Option<&Path>) -> Option<PathBuf> {
    let mut dialog = rfd::FileDialog::new();
    if let Some(dir) = start {
        dialog = dialog.set_directory(dir);
    }
    dialog.pick_file()
}

pub fn pick_folder(start: Option<&Path>) -> Option<PathBuf> {
    let mut dialog = rfd::FileDialog::new();
    if let Some(dir) = start {
        dialog = dialog.set_directory(dir);
    }
    dialog.pick_folder()
}

pub fn save_file(start: &Path) -> Option<PathBuf> {
    rfd::FileDialog::new().set_directory(start).save_file()
}

fn looks_like_path(bin: &str) -> bool {
    bin.contains('/') || bin.contains('\\')
}

const MISSING_HINT: &str =
    "Build Lynxer with 'make build' from the repo root, or pick it with 'Set Lynxer'.";

/// Validate that `bin` is a usable Lynxer interpreter.
///
/// An explicit path must exist and be a file; then `bin --version` must exit
/// cleanly with a non-empty version. Every failure returns an actionable
/// message rather than a silent or cryptic error.
pub fn check_lynxer(bin: &str) -> Result<String, String> {
    if bin.trim().is_empty() {
        return Err("The Lynxer executable path is empty. Use 'Set Lynxer'.".into());
    }
    if looks_like_path(bin) {
        let path = Path::new(bin);
        if !path.exists() {
            return Err(format!(
                "Lynxer executable not found at '{}'. {}",
                path.display(),
                MISSING_HINT
            ));
        }
        if !path.is_file() {
            return Err(format!("Lynxer path '{}' is not a file.", path.display()));
        }
    }

    let result = run_program(
        bin.to_string(),
        vec!["--version".to_string()],
        None,
        Duration::from_secs(10),
    );
    if result.timed_out {
        return Err(format!("Lynxer '{}' did not respond within 10s.", bin));
    }
    if result.spawn_error || result.code != 0 {
        return Err(format!(
            "Lynxer '{}' failed to run (exit {}): {}",
            bin,
            result.code,
            result.stderr.trim()
        ));
    }
    let version = result.stdout.trim().to_string();
    if version.is_empty() {
        return Err(format!("Lynxer '{}' printed no version on --version.", bin));
    }
    Ok(version)
}

/// Cheap, non-blocking pre-flight for an explicit interpreter path. A bare
/// name on `PATH` cannot be checked without spawning, so `run_program`
/// validates it when the command is actually launched.
pub fn preflight_lynxer(bin: &str) -> Result<(), String> {
    if bin.trim().is_empty() {
        return Err("The Lynxer executable path is empty. Use 'Set Lynxer'.".into());
    }
    if looks_like_path(bin) {
        let path = Path::new(bin);
        if !path.is_file() {
            return Err(format!(
                "Lynxer executable not found at '{}'. {}",
                path.display(),
                MISSING_HINT
            ));
        }
    }
    Ok(())
}

/// Run `bin` with `args` in `cwd`, capturing stdout/stderr. Kills the child
/// after `timeout` using `Child::kill()` (works on Linux, macOS and Windows).
pub fn run_program(
    bin: String,
    args: Vec<String>,
    cwd: Option<PathBuf>,
    timeout: Duration,
) -> RunResult {
    let mut cmd = Command::new(&bin);
    cmd.args(&args);
    if let Some(dir) = &cwd {
        cmd.current_dir(dir);
    }

    let mut child: Child = match cmd
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
    {
        Ok(child) => child,
        Err(e) => {
            let message = if e.kind() == std::io::ErrorKind::NotFound {
                format!("Cannot execute '{}': not found. {}", bin, MISSING_HINT)
            } else {
                format!("Cannot execute '{}': {e}", bin)
            };
            return RunResult {
                stdout: String::new(),
                stderr: message,
                code: -1,
                timed_out: false,
                spawn_error: true,
            };
        }
    };

    let mut stdout_pipe = child.stdout.take();
    let mut stderr_pipe = child.stderr.take();

    // Drain both pipes on their own threads so a full buffer cannot deadlock.
    let stdout_handle = std::thread::spawn(move || {
        let mut buf = String::new();
        if let Some(mut pipe) = stdout_pipe.take() {
            let _ = pipe.read_to_string(&mut buf);
        }
        buf
    });
    let stderr_handle = std::thread::spawn(move || {
        let mut buf = String::new();
        if let Some(mut pipe) = stderr_pipe.take() {
            let _ = pipe.read_to_string(&mut buf);
        }
        buf
    });

    let deadline = Instant::now() + timeout;
    let timed_out = loop {
        match child.try_wait() {
            Ok(Some(_)) => break false,
            Ok(None) => {
                if Instant::now() >= deadline {
                    let _ = child.kill();
                    break true;
                }
                std::thread::sleep(Duration::from_millis(20));
            }
            Err(_) => break false,
        }
    };

    let status = child.wait();
    let stdout = stdout_handle.join().unwrap_or_default();
    let stderr = stderr_handle.join().unwrap_or_default();
    let code = status.ok().and_then(|s| s.code()).unwrap_or(-1);

    RunResult {
        stdout,
        stderr,
        code,
        timed_out,
        spawn_error: false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn spawn_failure_is_flagged_and_explained() {
        let r = run_program(
            "definitely-not-a-real-lynxer-binary-xyz".into(),
            vec!["--version".into()],
            None,
            Duration::from_secs(5),
        );
        assert!(r.spawn_error);
        assert_eq!(r.code, -1);
        assert!(r.stderr.contains("Cannot execute"));
        assert!(r.stderr.contains("Set Lynxer"));
    }

    #[test]
    fn check_lynxer_rejects_missing_path() {
        let err = check_lynxer("/no/such/lynxer/binary").unwrap_err();
        assert!(err.contains("not found"), "{err}");
        assert!(err.contains("Set Lynxer"), "{err}");
    }

    #[test]
    fn preflight_rejects_empty_and_missing_paths() {
        assert!(preflight_lynxer("").is_err());
        assert!(preflight_lynxer("/no/such/lynxer").is_err());
        // A bare name on PATH is deferred to the actual spawn.
        assert!(preflight_lynxer("lynxer").is_ok());
    }

    #[test]
    fn file_validation_rejects_wrong_kinds() {
        assert!(list_dir(Path::new("/definitely/not/a/dir")).is_err());
        assert!(read_file(Path::new("/definitely/not/a/file")).is_err());
    }
}
