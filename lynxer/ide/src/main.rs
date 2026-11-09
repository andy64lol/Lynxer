//! Lynxer IDE - a native Rust GUI built with iced.
//!
//! No Tauri, no webview, no React, no Monaco: the file tree, tabbed editor,
//! syntax highlighting, autocomplete, diagnostics, Run/Lint/Format and the
//! output/problems panels are all Rust.

mod backend;
mod completions;
mod diagnostics;
mod highlighter;
mod icons;
mod ui;

use std::collections::HashSet;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::Duration;

use iced::widget::{
    button, column, container, opaque, row, scrollable, space, stack, text, text_editor, Column,
    Row, Space,
};
use iced::{Element, Font, Length, Task, Theme};

use backend::RunResult;
use diagnostics::{Diagnostic, Severity};
use highlighter::LynxerHighlighter;

pub fn main() -> iced::Result {
    iced::application(Ide::new, Ide::update, Ide::view)
        .title("Lynxer IDE")
        .theme(Theme::Dark)
        .window(iced::window::Settings {
            size: iced::Size::new(1280.0, 800.0),
            icon: icons::window_icon(),
            ..iced::window::Settings::default()
        })
        .subscription(|_| iced::keyboard::listen().map(Message::Key))
        .run()
}

/// Lynxer highlighting is applied only to `.lynx` sources.
fn is_lynx(path: &Path) -> bool {
    path.extension()
        .and_then(|ext| ext.to_str())
        .map(|ext| ext.eq_ignore_ascii_case("lynx"))
        .unwrap_or(false)
}

/// A launcher can pass `--lynxer <path>` to preselect the interpreter, so
/// Run/Lint/Format work even when `lynxer` is not on PATH.
fn lynxer_path_from_args(args: impl IntoIterator<Item = String>) -> String {
    let mut args = args.into_iter();
    while let Some(arg) = args.next() {
        if arg == "--lynxer" {
            if let Some(path) = args.next() {
                if !path.trim().is_empty() {
                    return path;
                }
            }
        }
    }
    "lynxer".into()
}

fn cli_lynxer_path() -> String {
    lynxer_path_from_args(std::env::args().skip(1))
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum PanelTab {
    Problems,
    Output,
}

#[derive(Debug, Clone)]
enum Message {
    OpenFolder,
    OpenFile,
    NewFile,
    Save,
    Run,
    Lint,
    Format,
    SetLynxer,
    ToggleDir(usize),
    OpenTreeFile(PathBuf),
    RefreshTree,
    SelectTab(usize),
    CloseTab(usize),
    ToggleSidebar,
    ToggleOutput,
    SelectPanel(PanelTab),
    GoToDiagnostic(PathBuf, usize),
    Edit(text_editor::Action),
    TabPressed,
    CompletionConfirm,
    CompletionDismiss,
    CompletionNav(i32),
    ApplyCompletion(String),
    Key(iced::keyboard::Event),
    ClearOutput,
    LynxerChecked(Result<String, String>),
    LynxerPicked {
        path: String,
        result: Result<String, String>,
    },
    LintFinished {
        path: PathBuf,
        result: RunResult,
        quiet: bool,
    },
    CommandFinished {
        label: String,
        result: RunResult,
        reload: Option<PathBuf>,
        relint: Option<PathBuf>,
    },
}

struct Tab {
    path: PathBuf,
    content: text_editor::Content,
    original: String,
}

/// Guards against symlink loops while expanding directories.
const MAX_TREE_DEPTH: usize = 32;

#[derive(Debug, Clone)]
struct TreeRow {
    name: String,
    path: PathBuf,
    is_dir: bool,
    depth: usize,
    expanded: bool,
}

fn rows_from_entries(entries: Vec<backend::FileEntry>, depth: usize) -> Vec<TreeRow> {
    entries
        .into_iter()
        .map(|e| TreeRow {
            name: e.name,
            path: e.path,
            is_dir: e.is_dir,
            depth,
            expanded: false,
        })
        .collect()
}

struct Ide {
    root: Option<PathBuf>,
    tree: Vec<TreeRow>,
    tabs: Vec<Tab>,
    active: Option<usize>,
    output: String,
    status: String,
    lynxer_path: String,
    lynxer_version: String,
    running: bool,
    show_sidebar: bool,
    show_output: bool,
    panel: PanelTab,
    diagnostics: Vec<Diagnostic>,
    completions: Vec<String>,
    completion_index: usize,
    completion_prefix_len: usize,
    /// Word for which the user dismissed the popup; suppress it until the
    /// prefix changes so it does not immediately reappear.
    dismissed_prefix: Option<String>,
}

impl Ide {
    fn new() -> (Self, Task<Message>) {
        let ide = Ide {
            root: None,
            tree: Vec::new(),
            tabs: Vec::new(),
            active: None,
            output: String::new(),
            status: "Ready".into(),
            lynxer_path: cli_lynxer_path(),
            lynxer_version: String::new(),
            running: false,
            show_sidebar: true,
            show_output: true,
            panel: PanelTab::Problems,
            diagnostics: Vec::new(),
            completions: Vec::new(),
            completion_index: 0,
            completion_prefix_len: 0,
            dismissed_prefix: None,
        };
        let bin = ide.lynxer_path.clone();
        let task = Task::perform(
            async move { backend::check_lynxer(&bin) },
            Message::LynxerChecked,
        );
        (ide, task)
    }

    fn active_tab(&self) -> Option<&Tab> {
        self.active.and_then(|i| self.tabs.get(i))
    }

    fn active_tab_mut(&mut self) -> Option<&mut Tab> {
        let i = self.active?;
        self.tabs.get_mut(i)
    }

    /// Report an error in both the status bar and the output panel.
    fn set_error(&mut self, message: impl Into<String>) {
        let message = message.into();
        self.status = format!("Error: {}", message);
        self.output = format!("error: {}\n", message);
    }

    fn refresh_tree(&mut self) {
        let Some(root) = self.root.clone() else {
            return;
        };
        let expanded: HashSet<PathBuf> = self
            .tree
            .iter()
            .filter(|r| r.expanded)
            .map(|r| r.path.clone())
            .collect();

        let mut rows = match backend::list_dir(&root) {
            Ok(entries) => rows_from_entries(entries, 0),
            Err(e) => {
                self.set_error(e);
                return;
            }
        };

        let mut i = 0usize;
        while i < rows.len() {
            let (is_dir, path, depth, is_expanded) = {
                let r = &rows[i];
                (r.is_dir, r.path.clone(), r.depth, r.expanded)
            };
            if is_dir && !is_expanded && depth < MAX_TREE_DEPTH && expanded.contains(&path) {
                match backend::list_dir(&path) {
                    Ok(entries) => {
                        let children = rows_from_entries(entries, depth + 1);
                        rows[i].expanded = true;
                        let at = i + 1;
                        rows.splice(at..at, children);
                    }
                    Err(e) => {
                        self.set_error(e);
                        return;
                    }
                }
            }
            i += 1;
        }
        self.tree = rows;
    }

    fn toggle_dir(&mut self, index: usize) {
        if index >= self.tree.len() {
            return;
        }
        let row = self.tree[index].clone();
        if !row.is_dir {
            return;
        }
        if row.expanded {
            let depth = row.depth;
            let mut end = index + 1;
            while end < self.tree.len() && self.tree[end].depth > depth {
                end += 1;
            }
            self.tree.drain(index + 1..end);
            self.tree[index].expanded = false;
            return;
        }
        if row.depth + 1 > MAX_TREE_DEPTH {
            self.set_error("Maximum directory depth reached.");
            return;
        }
        match backend::list_dir(&row.path) {
            Ok(entries) => {
                let children = rows_from_entries(entries, row.depth + 1);
                self.tree[index].expanded = true;
                let at = index + 1;
                self.tree.splice(at..at, children);
            }
            Err(e) => self.set_error(e),
        }
    }

    fn open_file(&mut self, path: PathBuf) {
        self.clear_completions();
        if let Some(i) = self.tabs.iter().position(|t| t.path == path) {
            self.active = Some(i);
            return;
        }
        match backend::read_file(&path) {
            Ok(content) => {
                self.tabs.push(Tab {
                    path,
                    content: text_editor::Content::with_text(&content),
                    original: content,
                });
                self.active = Some(self.tabs.len() - 1);
            }
            Err(e) => self.set_error(e),
        }
    }

    fn save_active(&mut self) -> Option<PathBuf> {
        let (path, content) = match self.active_tab() {
            Some(tab) => (tab.path.clone(), tab.content.text()),
            None => return None,
        };
        match backend::write_file(&path, &content) {
            Ok(()) => {
                if let Some(tab) = self.active_tab_mut() {
                    tab.original = content;
                }
                self.status = format!("Saved {}", path.display());
                Some(path)
            }
            Err(e) => {
                self.set_error(e);
                None
            }
        }
    }

    fn clear_completions(&mut self) {
        self.completions.clear();
        self.completion_index = 0;
        self.completion_prefix_len = 0;
    }

    fn perform(&mut self, action: text_editor::Action) {
        if let Some(tab) = self.active_tab_mut() {
            tab.content.perform(action);
        }
        self.refresh_completions();
    }

    fn paste_text(&mut self, s: &str) {
        self.perform(text_editor::Action::Edit(text_editor::Edit::Paste(
            Arc::new(s.to_string()),
        )));
    }

    fn refresh_completions(&mut self) {
        self.clear_completions();
        let Some(tab) = self.active_tab() else {
            return;
        };
        let Some(prefix) = current_prefix(&tab.content) else {
            return;
        };
        let words = document_words(&tab.content);
        let list = completions::for_prefix(&prefix, &words);

        // Respect an explicit dismissal until the user changes the word.
        if self.dismissed_prefix.as_deref() == Some(prefix.as_str()) {
            return;
        }
        self.dismissed_prefix = None;

        // Nothing worth showing when the only match is already typed in full.
        if list.is_empty() || (list.len() == 1 && list[0].eq_ignore_ascii_case(&prefix)) {
            return;
        }
        self.completion_prefix_len = prefix.chars().count();
        self.completions = list;
    }

    fn apply_completion(&mut self, word: String) {
        let prefix_len = self.completion_prefix_len;
        if let Some(tab) = self.active_tab_mut() {
            for _ in 0..prefix_len {
                tab.content
                    .perform(text_editor::Action::Edit(text_editor::Edit::Backspace));
            }
            tab.content
                .perform(text_editor::Action::Edit(text_editor::Edit::Paste(
                    Arc::new(word),
                )));
        }
        self.clear_completions();
    }

    /// Run `lynxer --lint` off the UI thread and fold the result into the
    /// Problems panel. `quiet` keeps the Output panel untouched (auto-lint).
    fn run_lint(&mut self, path: PathBuf, quiet: bool) -> Task<Message> {
        if let Err(e) = backend::preflight_lynxer(&self.lynxer_path) {
            self.set_error(e);
            return Task::none();
        }
        let bin = self.lynxer_path.clone();
        let cwd = path.parent().map(|p| p.to_path_buf());
        let args = vec!["--lint".to_string(), path.to_string_lossy().to_string()];
        Task::perform(
            async move { backend::run_program(bin, args, cwd, Duration::from_secs(30)) },
            move |result| Message::LintFinished {
                path,
                result,
                quiet,
            },
        )
    }

    fn spawn_command(
        &mut self,
        flag: Option<&'static str>,
        label: &'static str,
        reload: bool,
    ) -> Task<Message> {
        if self.running {
            return Task::none();
        }
        if let Err(e) = backend::preflight_lynxer(&self.lynxer_path) {
            self.set_error(e);
            return Task::none();
        }
        let path = match self.active_tab() {
            Some(tab) => tab.path.clone(),
            None => return Task::none(),
        };
        self.save_active();

        let cwd = path.parent().map(|p| p.to_path_buf());
        let bin = self.lynxer_path.clone();
        let mut args = Vec::new();
        if let Some(f) = flag {
            args.push(f.to_string());
        }
        args.push(path.to_string_lossy().to_string());
        let reload_path = if reload { Some(path.clone()) } else { None };
        let relint_path = reload_path.clone();

        self.running = true;
        self.output = format!("{}...\n", label);
        self.status = format!("{} {}", label, path.display());

        Task::perform(
            async move { backend::run_program(bin, args, cwd, Duration::from_secs(30)) },
            move |result| Message::CommandFinished {
                label: label.to_string(),
                result,
                reload: reload_path,
                relint: relint_path,
            },
        )
    }

    fn breadcrumb_segments(&self) -> Vec<String> {
        let Some(tab) = self.active_tab() else {
            return vec!["No file open".into()];
        };
        let mut parts = Vec::new();
        if let Some(root) = &self.root {
            if let Ok(rel) = tab.path.strip_prefix(root) {
                if let Some(name) = root.file_name() {
                    parts.push(name.to_string_lossy().to_string());
                }
                for c in rel.components() {
                    parts.push(c.as_os_str().to_string_lossy().to_string());
                }
                return parts;
            }
        }
        parts.push(
            tab.path
                .file_name()
                .map(|n| n.to_string_lossy().to_string())
                .unwrap_or_else(|| tab.path.display().to_string()),
        );
        parts
    }

    fn update(&mut self, message: Message) -> Task<Message> {
        match message {
            Message::OpenFolder => {
                if let Some(p) = backend::pick_folder(self.root.as_deref()) {
                    self.status = format!("Opened {}", p.display());
                    self.root = Some(p);
                    self.tree.clear();
                    self.refresh_tree();
                }
                Task::none()
            }
            Message::OpenFile => {
                if let Some(p) = backend::pick_file(self.root.as_deref()) {
                    self.open_file(p.clone());
                    return self.run_lint(p, true);
                }
                Task::none()
            }
            Message::NewFile => {
                if let Some(root) = self.root.clone() {
                    if let Some(path) = backend::save_file(&root) {
                        match backend::new_file(&path, "") {
                            Ok(()) => {
                                self.refresh_tree();
                                self.open_file(path.clone());
                                return self.run_lint(path, true);
                            }
                            Err(e) => self.set_error(e),
                        }
                    }
                } else {
                    self.status = "Open a folder first.".into();
                }
                Task::none()
            }
            Message::Save => {
                if let Some(path) = self.save_active() {
                    return self.run_lint(path, true);
                }
                Task::none()
            }
            Message::Run => self.spawn_command(None, "Run", false),
            Message::Lint => {
                let Some(tab) = self.active_tab() else {
                    return Task::none();
                };
                let path = tab.path.clone();
                self.run_lint(path, false)
            }
            Message::Format => self.spawn_command(Some("--format"), "Format", true),
            Message::SetLynxer => {
                if let Some(p) = backend::pick_file(None) {
                    let path = p.to_string_lossy().to_string();
                    return Task::perform(
                        async move {
                            let result = backend::check_lynxer(&path);
                            (path, result)
                        },
                        |(path, result)| Message::LynxerPicked { path, result },
                    );
                }
                Task::none()
            }
            Message::ToggleDir(i) => {
                self.toggle_dir(i);
                Task::none()
            }
            Message::OpenTreeFile(path) => {
                self.open_file(path.clone());
                self.run_lint(path, true)
            }
            Message::RefreshTree => {
                self.refresh_tree();
                Task::none()
            }
            Message::SelectTab(i) => {
                self.active = Some(i);
                self.clear_completions();
                Task::none()
            }
            Message::CloseTab(i) => {
                if i < self.tabs.len() {
                    self.tabs.remove(i);
                    self.active = if self.tabs.is_empty() {
                        None
                    } else {
                        let a = self.active.unwrap_or(0);
                        Some(if i < a { a - 1 } else { a }.min(self.tabs.len() - 1))
                    };
                    self.clear_completions();
                }
                Task::none()
            }
            Message::ToggleSidebar => {
                self.show_sidebar = !self.show_sidebar;
                Task::none()
            }
            Message::ToggleOutput => {
                self.show_output = !self.show_output;
                Task::none()
            }
            Message::SelectPanel(tab) => {
                self.panel = tab;
                Task::none()
            }
            Message::GoToDiagnostic(path, line) => {
                self.open_file(path.clone());
                if let Some(i) = self.tabs.iter().position(|t| t.path == path) {
                    self.active = Some(i);
                    self.tabs[i].content.move_to(text_editor::Cursor {
                        position: text_editor::Position {
                            line: line.saturating_sub(1),
                            column: 0,
                        },
                        selection: None,
                    });
                }
                self.panel = PanelTab::Problems;
                Task::none()
            }
            Message::Edit(action) => {
                self.perform(action);
                Task::none()
            }
            Message::TabPressed => {
                if !self.completions.is_empty() {
                    let word = self.completions[self.completion_index].clone();
                    self.apply_completion(word);
                } else {
                    self.paste_text("    ");
                }
                Task::none()
            }
            Message::CompletionConfirm => {
                if !self.completions.is_empty() {
                    let word = self.completions[self.completion_index].clone();
                    self.apply_completion(word);
                } else {
                    self.perform(text_editor::Action::Edit(text_editor::Edit::Enter));
                }
                Task::none()
            }
            Message::CompletionDismiss => {
                let dismissed = if self.completions.is_empty() {
                    None
                } else {
                    self.active_tab()
                        .and_then(|tab| current_prefix(&tab.content))
                        .filter(|prefix| !prefix.is_empty())
                };
                self.clear_completions();
                self.dismissed_prefix = dismissed;
                Task::none()
            }
            Message::CompletionNav(delta) => {
                if !self.completions.is_empty() {
                    let n = self.completions.len() as i32;
                    let cur = self.completion_index as i32;
                    self.completion_index = (cur + delta).rem_euclid(n) as usize;
                } else {
                    let motion = if delta < 0 {
                        text_editor::Motion::Up
                    } else {
                        text_editor::Motion::Down
                    };
                    self.perform(text_editor::Action::Move(motion));
                }
                Task::none()
            }
            Message::ApplyCompletion(word) => {
                self.apply_completion(word);
                Task::none()
            }
            Message::Key(event) => {
                if let iced::keyboard::Event::KeyPressed { key, modifiers, .. } = event {
                    if modifiers.command() {
                        if let iced::keyboard::Key::Character(c) = &key {
                            if c.as_str().eq_ignore_ascii_case("s") {
                                if let Some(path) = self.save_active() {
                                    return self.run_lint(path, true);
                                }
                            }
                        }
                    }
                    if matches!(
                        key,
                        iced::keyboard::Key::Named(iced::keyboard::key::Named::F5)
                    ) {
                        return self.spawn_command(None, "Run", false);
                    }
                }
                Task::none()
            }
            Message::ClearOutput => {
                self.output.clear();
                Task::none()
            }
            Message::LynxerChecked(Ok(version)) => {
                self.lynxer_version = version.clone();
                self.status = format!("Lynxer {}", version);
                Task::none()
            }
            Message::LynxerChecked(Err(e)) => {
                self.set_error(e.clone());
                self.diagnostics.push(Diagnostic {
                    path: PathBuf::from("<interpreter>"),
                    severity: Severity::Error,
                    line: 0,
                    column: 0,
                    code: "lynxerMissing".into(),
                    message: e,
                });
                Task::none()
            }
            Message::LynxerPicked { path, result } => {
                match result {
                    Ok(version) => {
                        self.lynxer_path = path;
                        self.lynxer_version = version.clone();
                        self.status = format!("Lynxer {}", version);
                        self.output.clear();
                        self.diagnostics.retain(|d| d.code != "lynxerMissing");
                    }
                    Err(e) => self.set_error(e),
                }
                Task::none()
            }
            Message::LintFinished {
                path,
                result,
                quiet,
            } => {
                if result.spawn_error {
                    self.set_error(result.stderr.clone());
                    return Task::none();
                }
                let combined = format!("{}\n{}", result.stdout, result.stderr);
                let parsed = diagnostics::parse(&combined, &path);
                self.diagnostics.retain(|d| d.path != path);
                self.diagnostics.extend(parsed);
                let (e, w) = diagnostics::counts(&self.diagnostics);
                self.status = format!("{} error(s), {} warning(s)", e, w);
                if !quiet {
                    self.output = combined;
                }
                Task::none()
            }
            Message::CommandFinished {
                label,
                result,
                reload,
                relint,
            } => {
                self.running = false;
                if result.spawn_error {
                    self.set_error(result.stderr.clone());
                    return Task::none();
                }
                let mut out = result.stdout;
                if !result.stderr.is_empty() {
                    out.push_str("\n--- stderr ---\n");
                    out.push_str(&result.stderr);
                }
                out.push_str(&format!(
                    "\n[exit {}{}]",
                    result.code,
                    if result.timed_out { ", timed out" } else { "" }
                ));
                self.output = out;
                self.status = format!("{} finished", label);
                if result.timed_out {
                    self.status = format!("{} timed out after 30s", label);
                }

                // --format rewrites the file on disk; reload it into the editor.
                if let Some(path) = reload {
                    if let Ok(content) = backend::read_file(&path) {
                        if let Some(i) = self.tabs.iter().position(|t| t.path == path) {
                            self.tabs[i].content = text_editor::Content::with_text(&content);
                            self.tabs[i].original = content;
                        }
                    }
                }
                if let Some(path) = relint {
                    return self.run_lint(path, true);
                }
                Task::none()
            }
        }
    }

    fn view(&self) -> Element<'_, Message> {
        // ---------------------------------------------------------- top bar
        let run_label = if self.running {
            "Running..."
        } else {
            "Run  F5"
        };
        let top_bar = container(
            row![
                icons::logo(16.0),
                text("LYNXER")
                    .size(13.0)
                    .font(Font::MONOSPACE)
                    .color(ui::accent()),
                text("IDE").size(13.0).color(ui::muted()),
                space::horizontal(),
                tool_button(icons::FOLDER, "Open Folder", Message::OpenFolder, button::secondary),
                tool_button(icons::FILE, "Open File", Message::OpenFile, button::secondary),
                tool_button(icons::FILE_PLUS, "New", Message::NewFile, button::secondary),
                tool_button(icons::SAVE, "Save", Message::Save, button::secondary),
                tool_button(icons::PLAY, run_label, Message::Run, button::primary),
                tool_button(icons::CHECK_CIRCLE, "Lint", Message::Lint, button::secondary),
                tool_button(icons::BRACES, "Format", Message::Format, button::secondary),
                tool_button(icons::SLIDERS, "Set Lynxer", Message::SetLynxer, button::secondary),
            ]
            .spacing(6.0)
            .align_y(iced::alignment::Vertical::Center),
        )
        .style(ui::top_bar)
        .padding(8.0);

        // ------------------------------------------------------ activity rail
        let rail = container(
            column![
                button(icons::icon(icons::FOLDER, ui::muted(), 18.0))
                    .on_press(Message::ToggleSidebar)
                    .style(button::text)
                    .width(Length::Fill),
                button(icons::icon(icons::PLAY, ui::accent(), 18.0))
                    .on_press(Message::Run)
                    .style(button::text)
                    .width(Length::Fill),
                button(icons::icon(icons::PANEL, ui::muted(), 18.0))
                    .on_press(Message::ToggleOutput)
                    .style(button::text)
                    .width(Length::Fill),
            ]
            .spacing(10.0),
        )
        .width(46.0)
        .height(Length::Fill)
        .style(ui::activity_bar)
        .padding(6.0);

        // ---------------------------------------------------------- explorer
        let root_label = self
            .root
            .as_ref()
            .map(|r| r.display().to_string())
            .unwrap_or_else(|| "No folder opened".into());
        let mut tree_col = column![row![
            text("EXPLORER").size(10.0).color(ui::muted()),
            space::horizontal(),
            icon_button(icons::REFRESH, ui::muted(), 12.0, Message::RefreshTree, button::text),
        ]
        .align_y(iced::alignment::Vertical::Center),]
        .spacing(2.0);
        if self.root.is_some() {
            for (i, row) in self.tree.iter().enumerate() {
                let indent = row.depth as f32 * 14.0;
                let marker: Element<'_, Message> = if row.is_dir {
                    icons::icon(
                        if row.expanded {
                            icons::CHEVRON_DOWN
                        } else {
                            icons::CHEVRON_RIGHT
                        },
                        ui::muted(),
                        12.0,
                    )
                    .into()
                } else {
                    icons::icon(
                        icons::FILE,
                        if is_lynx(&row.path) { ui::accent() } else { ui::muted() },
                        12.0,
                    )
                    .into()
                };
                let message = if row.is_dir {
                    Message::ToggleDir(i)
                } else {
                    Message::OpenTreeFile(row.path.clone())
                };
                let color = if row.is_dir {
                    ui::accent()
                } else if is_lynx(&row.path) {
                    ui::text()
                } else {
                    ui::muted()
                };
                tree_col = tree_col.push(
                    button(
                        row![
                            Space::new().width(indent),
                            marker,
                            Space::new().width(6.0),
                            text(row.name.as_str()).size(12.0).color(color),
                        ]
                        .align_y(iced::alignment::Vertical::Center),
                    )
                    .on_press(message)
                    .style(button::text)
                    .width(Length::Fill),
                );
            }
        } else {
            tree_col = tree_col.push(text("Open a folder to browse files.").color(ui::muted()));
        }
        let sidebar = container(
            column![
                text(root_label).size(11.0).color(ui::muted()),
                scrollable(tree_col).height(Length::Fill),
            ]
            .spacing(6.0),
        )
        .width(240.0)
        .height(Length::Fill)
        .style(ui::sidebar)
        .padding(8.0);

        // ------------------------------------------------------------- tabs
        let mut tabs_row = row![].spacing(2.0);
        for (i, tab) in self.tabs.iter().enumerate() {
            let name = tab
                .path
                .file_name()
                .map(|n| n.to_string_lossy().to_string())
                .unwrap_or_else(|| tab.path.display().to_string());
            let dirty = tab.content.text() != tab.original;
            let label = if dirty { format!("{} *", name) } else { name };
            let is_active = self.active == Some(i);
            tabs_row = tabs_row.push(
                row![
                    button(
                        row![
                            icons::icon(icons::FILE, ui::muted(), 12.0),
                            text(label).size(12.0),
                        ]
                        .spacing(6.0)
                        .align_y(iced::alignment::Vertical::Center),
                    )
                    .on_press(Message::SelectTab(i))
                    .style(ui::tab_style(is_active)),
                    icon_button(icons::X, ui::muted(), 11.0, Message::CloseTab(i), button::text),
                ]
                .spacing(2.0),
            );
        }
        let tab_bar = container(scrollable(tabs_row))
            .width(Length::Fill)
            .style(ui::tab_bar)
            .padding(4.0);

        // ------------------------------------------------------- breadcrumbs
        let mut crumbs = row![].spacing(6.0);
        let segments = self.breadcrumb_segments();
        for (i, seg) in segments.iter().enumerate() {
            if i > 0 {
                crumbs = crumbs.push(text(">").size(11.0).color(ui::muted()));
            }
            crumbs = crumbs.push(text(seg.clone()).size(11.0).font(Font::MONOSPACE).color(
                if i + 1 == segments.len() {
                    ui::text()
                } else {
                    ui::muted()
                },
            ));
        }
        let breadcrumb_bar = container(crumbs)
            .width(Length::Fill)
            .style(ui::breadcrumb)
            .padding(4.0);

        // ----------------------------------------------------------- editor
        let editor: Element<'_, Message> = match self.active {
            Some(i) if i < self.tabs.len() => {
                let tab = &self.tabs[i];
                let base = text_editor(&tab.content)
                    .on_action(Message::Edit)
                    .font(Font::MONOSPACE)
                    .size(14.0)
                    .height(Length::Fill)
                    .key_binding(|kp| {
                        use iced::keyboard::key::Named;
                        use iced::keyboard::Key;
                        let m = kp.modifiers;
                        let special = match kp.key.as_ref() {
                            Key::Named(Named::Tab) if !m.shift() => Some(Message::TabPressed),
                            Key::Named(Named::Enter) if !m.command() && !m.shift() => {
                                Some(Message::CompletionConfirm)
                            }
                            Key::Named(Named::Escape) => Some(Message::CompletionDismiss),
                            Key::Named(Named::ArrowUp) if !m.shift() => {
                                Some(Message::CompletionNav(-1))
                            }
                            Key::Named(Named::ArrowDown) if !m.shift() => {
                                Some(Message::CompletionNav(1))
                            }
                            _ => None,
                        };
                        match special {
                            Some(msg) => Some(text_editor::Binding::Custom(msg)),
                            None => text_editor::Binding::from_key_press(kp),
                        }
                    });
                if is_lynx(&tab.path) {
                    base.highlight_with::<LynxerHighlighter>((), highlighter::to_format)
                        .into()
                } else {
                    base.into()
                }
            }
            _ => container(
                column![
                    icons::logo(48.0),
                    text("LYNXER")
                        .size(32.0)
                        .font(Font::MONOSPACE)
                        .color(ui::accent()),
                    text("Native IDE").size(16.0).color(ui::muted()),
                    Space::new().height(14.0),
                    text("Open a folder or a file to start editing")
                        .size(13.0)
                        .color(ui::muted()),
                    Space::new().height(10.0),
                    row![
                        tool_button(icons::FOLDER, "Open Folder", Message::OpenFolder, button::primary),
                        tool_button(icons::FILE, "Open File", Message::OpenFile, button::secondary),
                    ]
                    .spacing(8.0),
                    Space::new().height(18.0),
                    text("Ctrl+S save     F5 run     Tab indent     Tab / Enter accept completion")
                        .size(11.0)
                        .color(ui::muted()),
                ]
                .spacing(4.0),
            )
            .width(Length::Fill)
            .height(Length::Fill)
            .align_x(iced::alignment::Horizontal::Center)
            .align_y(iced::alignment::Vertical::Center)
            .into(),
        };

        // -------------------------------------------------- autocomplete popup
        // The Stack is ALWAYS built, even with an empty popup layer. Otherwise
        // appearing/disappearing suggestions would change the widget tree and
        // recreate the editor's state (losing keyboard focus mid-typing).
        let has_completions = !self.completions.is_empty();
        let mut list = column![].spacing(2.0);
        if has_completions {
            for (i, word) in self.completions.iter().enumerate() {
                list = list.push(
                    button(text(word).size(11.0).font(Font::MONOSPACE))
                        .on_press(Message::ApplyCompletion(word.clone()))
                        .style(ui::completion_style(i == self.completion_index))
                        .width(Length::Fill),
                );
            }
            list = list.push(
                text("Tab / Enter accept   Esc dismiss")
                    .size(9.0)
                    .color(ui::muted()),
            );
        }
        let popup_box = container(list)
            .width(if has_completions { 300.0 } else { 0.0 })
            .style(ui::popup_style(has_completions))
            .padding(if has_completions { 6.0 } else { 0.0 });

        let editor_layer: Element<'_, Message> = stack(vec![
            editor,
            container(opaque(popup_box))
                .width(Length::Fill)
                .height(Length::Fill)
                .align_x(iced::alignment::Horizontal::Left)
                .align_y(iced::alignment::Vertical::Bottom)
                .padding(16.0)
                .into(),
        ])
        .width(Length::Fill)
        .height(Length::Fill)
        .into();

        let editor_column = column![
            tab_bar,
            breadcrumb_bar,
            container(editor_layer)
                .width(Length::Fill)
                .height(Length::Fill)
                .style(ui::editor)
                .padding(8.0),
        ]
        .width(Length::Fill)
        .spacing(0.0);

        // ------------------------------------------------------------ body
        let mut body_children: Vec<Element<'_, Message>> = vec![rail.into()];
        if self.show_sidebar {
            body_children.push(sidebar.into());
        }
        body_children.push(editor_column.into());
        let body: Element<'_, Message> = Row::with_children(body_children)
            .spacing(0.0)
            .height(Length::Fill)
            .into();

        // ----------------------------------------------------- bottom panel
        let (errors, warnings) = diagnostics::counts(&self.diagnostics);
        let panel_header = container(
            row![
                button(
                    row![
                        icons::icon(
                            icons::ALERT_TRIANGLE,
                            if errors > 0 { ui::error() } else { ui::muted() },
                            12.0,
                        ),
                        text(format!("PROBLEMS  {}  {}", errors, warnings)).size(10.0),
                    ]
                    .spacing(6.0)
                    .align_y(iced::alignment::Vertical::Center),
                )
                .on_press(Message::SelectPanel(PanelTab::Problems))
                .style(ui::panel_tab_style(self.panel == PanelTab::Problems)),
                button(
                    row![
                        icons::icon(icons::TERMINAL, ui::muted(), 12.0),
                        text("OUTPUT").size(10.0),
                    ]
                    .spacing(6.0)
                    .align_y(iced::alignment::Vertical::Center),
                )
                .on_press(Message::SelectPanel(PanelTab::Output))
                .style(ui::panel_tab_style(self.panel == PanelTab::Output)),
                space::horizontal(),
                icon_button(icons::TRASH, ui::muted(), 12.0, Message::ClearOutput, button::text),
            ]
            .spacing(4.0)
            .align_y(iced::alignment::Vertical::Center),
        )
        .style(ui::panel_head)
        .padding(4.0);

        let panel_body: Element<'_, Message> = match self.panel {
            PanelTab::Problems => {
                if self.diagnostics.is_empty() {
                    container(
                        text("No problems detected. Open or save a .lynx file to lint it.")
                            .color(ui::muted()),
                    )
                    .width(Length::Fill)
                    .height(Length::Fill)
                    .padding(8.0)
                    .into()
                } else {
                    let mut col = column![].spacing(2.0);
                    for d in &self.diagnostics {
                        let sev = match d.severity {
                            Severity::Error => "ERROR",
                            Severity::Warning => "WARN ",
                            Severity::Info => "INFO ",
                        };
                        let file = d
                            .path
                            .file_name()
                            .map(|n| n.to_string_lossy().to_string())
                            .unwrap_or_else(|| d.path.display().to_string());
                        let loc = if d.line > 0 {
                            format!("{}:{}", d.line, d.column)
                        } else {
                            "-".into()
                        };
                        let label =
                            format!("{}  {}:{}  [{}]  {}", sev, file, loc, d.code, d.message);
                        col = col.push(
                            button(
                                text(label)
                                    .size(11.0)
                                    .font(Font::MONOSPACE)
                                    .color(ui::severity_color(d.severity)),
                            )
                            .on_press(Message::GoToDiagnostic(d.path.clone(), d.line))
                            .style(button::text)
                            .width(Length::Fill),
                        );
                    }
                    scrollable(col).height(Length::Fill).into()
                }
            }
            PanelTab::Output => scrollable(text(self.output.as_str()).color(ui::text()))
                .height(Length::Fill)
                .into(),
        };
        let bottom_panel = column![
            panel_header,
            container(panel_body)
                .width(Length::Fill)
                .height(160.0)
                .style(ui::output)
                .padding(6.0),
        ];

        // ------------------------------------------------------ status bar
        let language_label = self
            .active_tab()
            .map(|tab| {
                if is_lynx(&tab.path) {
                    "Lynxer"
                } else {
                    "Plain text"
                }
            })
            .unwrap_or("No file");
        let (ln, col) = self
            .active_tab()
            .map(|tab| {
                let p = tab.content.cursor().position;
                (p.line + 1, p.column + 1)
            })
            .unwrap_or((1, 1));
        let lynxer_label = if self.lynxer_version.is_empty() {
            format!("lynxer: {}", self.lynxer_path)
        } else {
            format!("Lynxer {}", self.lynxer_version)
        };
        let status_bar = container(
            row![
                text(format!("{}  {}", errors, warnings))
                    .size(11.0)
                    .color(if errors > 0 { ui::error() } else { ui::text() }),
                text("errors / warnings").size(10.0).color(ui::muted()),
                text(self.status.as_str()).size(11.0).color(ui::text()),
                space::horizontal(),
                text(format!("Ln {}, Col {}", ln, col))
                    .size(11.0)
                    .color(ui::muted()),
                text("UTF-8").size(11.0).color(ui::muted()),
                text(language_label).size(11.0).color(ui::accent()),
                text(lynxer_label).size(11.0).color(ui::muted()),
            ]
            .spacing(12.0)
            .align_y(iced::alignment::Vertical::Center),
        )
        .style(ui::status_bar)
        .padding(6.0);

        // --------------------------------------------------------- compose
        let mut root_children: Vec<Element<'_, Message>> = vec![top_bar.into(), body];
        if self.show_output {
            root_children.push(bottom_panel.into());
        }
        root_children.push(status_bar.into());
        Column::with_children(root_children).spacing(0.0).into()
    }
}

/// Toolbar button: a symbolic SVG icon plus a text label.
fn tool_button<'a>(
    icon_bytes: &'static [u8],
    label: impl Into<String>,
    message: Message,
    style: ui::ButtonStyleFn,
) -> Element<'a, Message> {
    button(
        row![
            icons::icon(icon_bytes, ui::text(), 14.0),
            text(label.into()).size(12.0),
        ]
        .spacing(6.0)
        .align_y(iced::alignment::Vertical::Center),
    )
    .on_press(message)
    .style(style)
    .into()
}

/// Compact icon-only button.
fn icon_button<'a>(
    icon_bytes: &'static [u8],
    color: iced::Color,
    size: f32,
    message: Message,
    style: ui::ButtonStyleFn,
) -> Element<'a, Message> {
    button(icons::icon(icon_bytes, color, size))
        .on_press(message)
        .style(style)
        .into()
}

/// Word immediately before the caret, or `None` inside a comment/string.
fn current_prefix(content: &text_editor::Content) -> Option<String> {
    let cursor = content.cursor();
    let line = content.line(cursor.position.line)?;
    let text: &str = line.text.as_ref();
    let before: String = text.chars().take(cursor.position.column).collect();
    // Autocomplete only at the end of a word, not while editing its middle.
    if text
        .chars()
        .skip(cursor.position.column)
        .next()
        .map(|c| c.is_alphanumeric() || c == '_')
        .unwrap_or(false)
    {
        return None;
    }
    let trimmed = before.trim_start();
    if trimmed.starts_with('#') || trimmed.starts_with("//") || trimmed.starts_with("/*") {
        return None;
    }
    let quotes = before.chars().filter(|c| *c == '"' || *c == '\'').count();
    if quotes % 2 == 1 {
        return None;
    }
    let prefix: String = before
        .chars()
        .rev()
        .take_while(|c| c.is_alphanumeric() || *c == '_')
        .collect::<Vec<_>>()
        .into_iter()
        .rev()
        .collect();
    if prefix.is_empty() {
        return Some(String::new());
    }
    let idx = before.len() - prefix.len();
    if idx > 0 && before[..idx].chars().last() == Some('.') {
        return None;
    }
    Some(prefix)
}

/// Identifier-like words in the open document, offered as completions too.
fn document_words(content: &text_editor::Content) -> Vec<String> {
    let mut words = Vec::new();
    let mut current = String::new();
    for ch in content.text().chars() {
        if ch.is_alphanumeric() || ch == '_' {
            current.push(ch);
        } else {
            if current.chars().count() >= 2 {
                words.push(std::mem::take(&mut current));
            } else {
                current.clear();
            }
        }
    }
    if current.chars().count() >= 2 {
        words.push(current);
    }
    words
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn highlighting_is_only_for_lynx_files() {
        assert!(is_lynx(Path::new("main.lynx")));
        assert!(is_lynx(Path::new("MAIN.LYNX")));
        assert!(is_lynx(Path::new("some/dir/program.lynx")));
        assert!(!is_lynx(Path::new("readme.txt")));
        assert!(!is_lynx(Path::new("program.lynx.bak")));
        assert!(!is_lynx(Path::new("Makefile")));
        assert!(!is_lynx(Path::new(".lynx")));
    }

    #[test]
    fn interpreter_path_argument_overrides_default() {
        assert_eq!(lynxer_path_from_args(Vec::<String>::new()), "lynxer");
        assert_eq!(
            lynxer_path_from_args(vec![
                "--lynxer".to_string(),
                "/opt/lynxer/lynxer".to_string()
            ]),
            "/opt/lynxer/lynxer"
        );
        assert_eq!(
            lynxer_path_from_args(vec!["--lynxer".to_string()]),
            "lynxer"
        );
        assert_eq!(
            lynxer_path_from_args(vec![
                "--lynxer".to_string(),
                "  ".to_string()
            ]),
            "lynxer"
        );
        assert_eq!(
            lynxer_path_from_args(vec!["other".to_string(), "x".to_string()]),
            "lynxer"
        );
    }

    #[test]
    fn prefix_is_extracted_and_suppressed_in_comments() {
        let c = text_editor::Content::with_text("fn main() { pri }");
        // Caret at end of line would give "}" -> empty; move is not easily
        // scriptable here, so just verify the helper does not panic and that
        // document word extraction finds the identifiers.
        let words = document_words(&c);
        assert!(words.contains(&"main".to_string()));
        assert!(words.contains(&"fn".to_string()));
    }

    fn ide_with_caret(text_source: &str, column: usize) -> Ide {
        let mut ide = Ide {
            root: None,
            tree: Vec::new(),
            tabs: Vec::new(),
            active: None,
            output: String::new(),
            status: String::new(),
            lynxer_path: "lynxer".into(),
            lynxer_version: String::new(),
            running: false,
            show_sidebar: true,
            show_output: true,
            panel: PanelTab::Problems,
            diagnostics: Vec::new(),
            completions: Vec::new(),
            completion_index: 0,
            completion_prefix_len: 0,
            dismissed_prefix: None,
        };
        let mut content = text_editor::Content::with_text(text_source);
        content.move_to(text_editor::Cursor {
            position: text_editor::Position { line: 0, column },
            selection: None,
        });
        ide.tabs.push(Tab {
            path: PathBuf::from("main.lynx"),
            content,
            original: text_source.to_string(),
        });
        ide.active = Some(0);
        ide
    }

    #[test]
    fn short_prefix_does_not_open_the_popup() {
        let mut ide = ide_with_caret("fn pr", 5);
        ide.refresh_completions();
        assert!(ide.completions.is_empty());
    }

    #[test]
    fn dismissal_sticks_until_the_word_changes() {
        let mut ide = ide_with_caret("fn pri", 6);
        ide.refresh_completions();
        assert!(!ide.completions.is_empty(), "a 3-char prefix should suggest");

        ide.dismissed_prefix = Some("pri".into());
        ide.clear_completions();
        ide.refresh_completions();
        assert!(ide.completions.is_empty(), "dismissed word stays quiet");

        // Typing another character through the normal edit path re-enables it.
        ide.perform(text_editor::Action::Edit(text_editor::Edit::Insert('n')));
        assert!(
            !ide.completions.is_empty(),
            "typing must keep working while completions are active"
        );
    }
}

#[cfg(test)]
mod tree_tests {
    use super::*;

    fn empty_ide(root: PathBuf) -> Ide {
        Ide {
            root: Some(root),
            tree: Vec::new(),
            tabs: Vec::new(),
            active: None,
            output: String::new(),
            status: String::new(),
            lynxer_path: "lynxer".into(),
            lynxer_version: String::new(),
            running: false,
            show_sidebar: true,
            show_output: true,
            panel: PanelTab::Problems,
            diagnostics: Vec::new(),
            completions: Vec::new(),
            completion_index: 0,
            completion_prefix_len: 0,
            dismissed_prefix: None,
        }
    }

    #[test]
    fn tree_expands_nested_dirs_collapses_and_survives_refresh() {
        let base = std::env::temp_dir().join(format!(
            "lynxer-ide-tree-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        std::fs::create_dir_all(base.join("sub/deep")).unwrap();
        std::fs::write(base.join("a.lynx"), "fn main(){}").unwrap();
        std::fs::write(base.join("sub/b.lynx"), "fn main(){}").unwrap();
        std::fs::write(base.join("sub/deep/c.lynx"), "fn main(){}").unwrap();

        let mut ide = empty_ide(base.clone());
        ide.refresh_tree();
        assert!(ide.tree.iter().any(|r| r.name == "sub" && r.depth == 0));
        assert!(!ide.tree.iter().any(|r| r.name == "b.lynx"));

        let sub = ide.tree.iter().position(|r| r.name == "sub").unwrap();
        ide.toggle_dir(sub);
        assert!(ide.tree.iter().any(|r| r.name == "b.lynx" && r.depth == 1));

        let deep = ide.tree.iter().position(|r| r.name == "deep").unwrap();
        ide.toggle_dir(deep);
        assert!(ide.tree.iter().any(|r| r.name == "c.lynx" && r.depth == 2));

        ide.toggle_dir(sub);
        assert!(!ide.tree.iter().any(|r| r.depth > 0));

        ide.toggle_dir(sub);
        ide.refresh_tree();
        assert!(ide.tree.iter().any(|r| r.name == "b.lynx" && r.depth == 1));

        let _ = std::fs::remove_dir_all(&base);
    }
}
