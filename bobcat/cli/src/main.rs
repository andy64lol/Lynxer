//! Bobcat CLI IDE — a standalone full-screen terminal IDE.
//!
//! Extracted from Lynxer's built-in `--cli-ide` (kept intact in `lynxer/`)
//! into an independent binary. It edits `.lynx`, `.wynx` and `.wpf` files and
//! drives the matching engine (`lynxer` or `wynxer`) for Run / Lint / Format.

mod project;

use crossterm::{
    event::{self, Event, KeyCode, KeyEvent, KeyEventKind, KeyModifiers},
    execute,
    terminal::{disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen},
};
use ratatui::{
    backend::CrosstermBackend,
    layout::{Constraint, Direction, Layout},
    style::{Color, Style},
    text::{Line, Span},
    widgets::{Block, Borders, Paragraph, Wrap},
    Frame, Terminal,
};
use std::fs;
use std::io::{self, stdout};
use std::path::PathBuf;
use std::process::Command;
use std::time::Duration;

#[derive(Clone, Copy, PartialEq, Eq)]
enum Action {
    Run,
    Lint,
    Format,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum PromptAction {
    Open,
}

struct App {
    path: Option<PathBuf>,
    lines: Vec<String>,
    cursor: (usize, usize), // (line, column) in characters
    scroll: usize,
    output: Vec<String>,
    status: String,
    show_output: bool,
    dirty: bool,
    quit: bool,
    prompt: Option<(PromptAction, String)>,
    /// Set when launched inside a Wynxer project (`.wynxcfg` + `.wpf`).
    project: Option<project::WynxerProject>,
}

impl App {
    fn new() -> Self {
        // Enter the specialised Wynxer mode automatically when the current
        // directory looks like a Wynxer project.
        let project = project::detect(std::path::Path::new("."));
        let mut app = Self {
            path: None,
            lines: vec![String::new()],
            cursor: (0, 0),
            scroll: 0,
            output: vec!["Bobcat IDE — press Ctrl+O to open, F1 for help.".to_string()],
            status: "Ready".to_string(),
            show_output: true,
            dirty: false,
            quit: false,
            prompt: None,
            project: project.clone(),
        };
        if let Some(p) = &project {
            // Open the configured entry page so the specialised mode starts
            // with the project's page already loaded.
            let entry = p.root.join(&p.entry);
            if entry.is_file() {
                app.open(entry);
            }
            app.status = format!(
                "Wynxer project: {} ({} -> {}/{}) — F5 build+serve, F6 check, F7 format",
                p.name, p.script, p.output, "index.html"
            );
            app.log(&app.status.clone());
        }
        app
    }

    fn engine(&self) -> &'static str {
        match self.path.as_deref().and_then(|p| p.extension()).and_then(|s| s.to_str()) {
            Some("wynx") | Some("wpf") => "wynxer",
            _ => "lynxer",
        }
    }

    fn open(&mut self, path: PathBuf) {
        match fs::read_to_string(&path) {
            Ok(text) => {
                let mut lines: Vec<String> = text.lines().map(str::to_string).collect();
                if lines.is_empty() {
                    lines.push(String::new());
                }
                self.lines = lines;
                self.path = Some(path);
                self.cursor = (0, 0);
                self.scroll = 0;
                self.dirty = false;
                self.status = format!("Opened {}", self.path.as_ref().unwrap().display());
                self.log(&self.status.clone());
            }
            Err(e) => {
                self.status = format!("Cannot open: {}", e);
                self.log(&self.status.clone());
            }
        }
    }

    fn save(&mut self) {
        let Some(path) = self.path.clone() else {
            self.status = "No file open (Ctrl+O to open)".to_string();
            return;
        };
        let mut text = self.lines.join("\n");
        text.push('\n');
        match fs::write(&path, text) {
            Ok(()) => {
                self.dirty = false;
                self.status = format!("Saved {}", path.display());
                self.log(&self.status.clone());
            }
            Err(e) => {
                self.status = format!("Cannot save: {}", e);
                self.log(&self.status.clone());
            }
        }
    }

    fn log(&mut self, msg: &str) {
        self.output.push(msg.to_string());
        if self.output.len() > 500 {
            self.output.remove(0);
        }
    }

    fn run_command(&mut self, action: Action) {
        if self.dirty {
            self.save();
        }
        // Specialised Wynxer mode: act on the whole project, not just the
        // open file. Run builds and serves; Lint validates the project.
        if let Some(project) = self.project.clone() {
            let root = project.root.display().to_string();
            let (program, args): (String, Vec<String>) = match action {
                Action::Run => (
                    "wynxer".into(),
                    vec!["run".into(), root.clone(), "--port".into(), project.port.to_string()],
                ),
                Action::Lint => ("wynxer".into(), vec!["check".into(), root]),
                Action::Format => {
                    let file = self
                        .path
                        .as_ref()
                        .map(|p| p.display().to_string())
                        .unwrap_or_else(|| project.root.join(&project.entry).display().to_string());
                    ("wynxer".into(), vec!["format".into(), file, "--write".into()])
                }
            };
            self.log(&format!("> {} {}", program, args.join(" ")));
            self.show_output = true;
            match Command::new(&program).args(&args).output() {
                Ok(result) => {
                    for line in String::from_utf8_lossy(&result.stdout).lines() {
                        self.log(line);
                    }
                    for line in String::from_utf8_lossy(&result.stderr).lines() {
                        self.log(line);
                    }
                    self.status = format!(
                        "{} exited with code {}",
                        program,
                        result.status.code().unwrap_or(-1)
                    );
                    if action == Action::Format {
                        if let Some(path) = self.path.clone() {
                            if let Ok(text) = fs::read_to_string(&path) {
                                let mut lines: Vec<String> = text.lines().map(str::to_string).collect();
                                if lines.is_empty() {
                                    lines.push(String::new());
                                }
                                self.lines = lines;
                                self.cursor = (0, 0);
                                self.dirty = false;
                            }
                        }
                    }
                }
                Err(e) => {
                    self.status = format!("Cannot run {}: {}", program, e);
                    self.log(&self.status.clone());
                }
            }
            return;
        }

        let Some(path) = self.path.clone() else {
            self.status = "No file open (Ctrl+O to open)".to_string();
            return;
        };
        let engine = self.engine().to_string();
        let display = path.display().to_string();
        let dir = path
            .parent()
            .map(|p| p.to_path_buf())
            .unwrap_or_else(|| PathBuf::from("."));

        let (program, args): (String, Vec<String>) = match (engine.as_str(), action) {
            ("wynxer", Action::Run) => ("wynxer".into(), vec!["build".into(), dir.display().to_string()]),
            ("wynxer", Action::Lint) => ("wynxer".into(), vec!["lint".into(), display.clone()]),
            ("wynxer", Action::Format) => ("wynxer".into(), vec!["format".into(), display.clone(), "--write".into()]),
            (_, Action::Run) => ("lynxer".into(), vec![display.clone()]),
            (_, Action::Lint) => ("lynxer".into(), vec!["--lint".into(), display.clone()]),
            (_, Action::Format) => ("lynxer".into(), vec!["--format".into(), display.clone()]),
        };

        self.log(&format!("> {} {}", program, args.join(" ")));
        self.show_output = true;
        match Command::new(&program).args(&args).output() {
            Ok(result) => {
                for line in String::from_utf8_lossy(&result.stdout).lines() {
                    self.log(line);
                }
                for line in String::from_utf8_lossy(&result.stderr).lines() {
                    self.log(line);
                }
                self.status = format!(
                    "{} exited with code {}",
                    program,
                    result.status.code().unwrap_or(-1)
                );
                if action == Action::Format {
                    if let Ok(text) = fs::read_to_string(&path) {
                        let mut lines: Vec<String> = text.lines().map(str::to_string).collect();
                        if lines.is_empty() {
                            lines.push(String::new());
                        }
                        self.lines = lines;
                        self.cursor = (0, 0);
                        self.dirty = false;
                    }
                }
            }
            Err(e) => {
                self.status = format!("Cannot run {}: {}", program, e);
                self.log(&self.status.clone());
            }
        }
    }

    // ---- editing ----

    fn line_len(&self) -> usize {
        self.lines.get(self.cursor.0).map(|l| l.chars().count()).unwrap_or(0)
    }

    fn move_cursor(&mut self, dx: isize, dy: isize) {
        let (line, col) = self.cursor;
        let new_line = (line as isize + dy).max(0).min(self.lines.len().saturating_sub(1) as isize) as usize;
        let col_len = self.lines.get(new_line).map(|l| l.chars().count()).unwrap_or(0);
        let new_col = (col as isize + dx).max(0).min(col_len as isize) as usize;
        self.cursor = (new_line, new_col);
        self.ensure_visible();
    }

    fn ensure_visible(&mut self) {
        let (line, _) = self.cursor;
        if line < self.scroll {
            self.scroll = line;
        }
    }

    fn insert(&mut self, text: &str) {
        let (line, col) = self.cursor;
        if line >= self.lines.len() {
            return;
        }
        let chars: Vec<char> = self.lines[line].chars().collect();
        let mut new_line = String::new();
        for c in chars.iter().take(col) {
            new_line.push(*c);
        }
        new_line.push_str(text);
        for c in chars.iter().skip(col) {
            new_line.push(*c);
        }
        self.lines[line] = new_line;
        self.cursor.1 += text.chars().count();
        self.dirty = true;
    }

    fn backspace(&mut self) {
        let (line, col) = self.cursor;
        if col > 0 {
            if let Some(cur) = self.lines.get_mut(line) {
                let mut chars: Vec<char> = cur.chars().collect();
                if col <= chars.len() {
                    chars.remove(col - 1);
                    *cur = chars.into_iter().collect();
                }
            }
            self.cursor.1 -= 1;
        } else if line > 0 {
            let current = self.lines.remove(line);
            let prev_len = self.lines[line - 1].chars().count();
            self.lines[line - 1].push_str(&current);
            self.cursor = (line - 1, prev_len);
        }
        self.ensure_visible();
        self.dirty = true;
    }

    fn delete(&mut self) {
        let (line, col) = self.cursor;
        if let Some(cur) = self.lines.get_mut(line) {
            let mut chars: Vec<char> = cur.chars().collect();
            if col < chars.len() {
                chars.remove(col);
                *cur = chars.into_iter().collect();
                self.dirty = true;
            }
        }
    }

    fn enter(&mut self) {
        let (line, col) = self.cursor;
        let current = self.lines[line].clone();
        let chars: Vec<char> = current.chars().collect();
        let before: String = chars.iter().take(col).collect();
        let after: String = chars.iter().skip(col).collect();
        self.lines[line] = before;
        self.lines.insert(line + 1, after);
        self.cursor = (line + 1, 0);
        self.ensure_visible();
        self.dirty = true;
    }

    fn handle_key(&mut self, key: KeyEvent) {
        if key.kind != KeyEventKind::Press {
            return;
        }
        let ctrl = key.modifiers.contains(KeyModifiers::CONTROL);

        // Prompt input takes priority.
        if let Some((action, ref mut value)) = self.prompt {
            match key.code {
                KeyCode::Esc => self.prompt = None,
                KeyCode::Enter => {
                    let path = PathBuf::from(value.clone());
                    self.prompt = None;
                    match action {
                        PromptAction::Open => self.open(path),
                    }
                }
                KeyCode::Backspace => {
                    value.pop();
                }
                KeyCode::Char(c) => value.push(c),
                _ => {}
            }
            return;
        }

        match key.code {
            KeyCode::Char('q') if ctrl => self.quit = true,
            KeyCode::Esc => self.show_output = false,
            KeyCode::Char('s') if ctrl => self.save(),
            KeyCode::Char('o') if ctrl => {
                self.prompt = Some((PromptAction::Open, String::new()));
            }
            KeyCode::Char('p') if ctrl => self.show_output = !self.show_output,
            KeyCode::F(1) => {
                if let Some(project) = &self.project {
                    self.log(&format!(
                        "Wynxer project mode ({}): F5 build+serve :{} | F6 check | F7 format {} | Ctrl+O open | Ctrl+Q quit",
                        project.name, project.port, project.entry
                    ));
                } else {
                    self.log("Keys: Ctrl+O open | Ctrl+S save | F5 run | F6 lint | F7 format | Ctrl+P panel | Ctrl+Q quit");
                }
                self.show_output = true;
            }
            KeyCode::F(5) => self.run_command(Action::Run),
            KeyCode::F(6) => self.run_command(Action::Lint),
            KeyCode::F(7) => self.run_command(Action::Format),
            KeyCode::Left => self.move_cursor(-1, 0),
            KeyCode::Right => self.move_cursor(1, 0),
            KeyCode::Up => self.move_cursor(0, -1),
            KeyCode::Down => self.move_cursor(0, 1),
            KeyCode::Home => self.cursor.1 = 0,
            KeyCode::End => self.cursor.1 = self.line_len(),
            KeyCode::PageUp => self.move_cursor(0, -10),
            KeyCode::PageDown => self.move_cursor(0, 10),
            KeyCode::Backspace => self.backspace(),
            KeyCode::Delete => self.delete(),
            KeyCode::Enter => self.enter(),
            KeyCode::Tab => self.insert("    "),
            KeyCode::Char(c) if !ctrl => self.insert(&c.to_string()),
            _ => {}
        }
    }
}

fn ui(frame: &mut Frame, app: &App) {
    let size = frame.size();
    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(1),
            Constraint::Min(3),
            Constraint::Length(if app.show_output { 10 } else { 1 }),
            Constraint::Length(1),
        ])
        .split(size);

    // Title bar — show the specialised mode when a Wynxer project was found.
    let title = if let Some(project) = &app.project {
        format!(
            " Bobcat IDE [Wynxer project: {}] — {}{}",
            project.name,
            app.path
                .as_ref()
                .and_then(|p| p.file_name())
                .and_then(|s| s.to_str())
                .unwrap_or(&project.entry),
            if app.dirty { " *" } else { "" }
        )
    } else {
        match &app.path {
            Some(p) => format!(
                " Bobcat IDE — {} [{}]{}",
                p.file_name().and_then(|s| s.to_str()).unwrap_or("?"),
                app.engine(),
                if app.dirty { " *" } else { "" }
            ),
            None => " Bobcat IDE — no file (Ctrl+O)".to_string(),
        }
    };
    frame.render_widget(
        Paragraph::new(title).style(Style::default().bg(Color::DarkGray).fg(Color::White)),
        chunks[0],
    );

    // Editor with line numbers.
    let editor_chunks = Layout::default()
        .direction(Direction::Horizontal)
        .constraints([Constraint::Length(5), Constraint::Min(1)])
        .split(chunks[1]);

    let visible_height = editor_chunks[1].height as usize;
    let gutter: Vec<Line> = (app.scroll..app.scroll + visible_height)
        .map(|n| {
            if n < app.lines.len() {
                Line::from(format!("{:>4}", n + 1))
            } else {
                Line::from("    ")
            }
        })
        .collect();
    frame.render_widget(
        Paragraph::new(gutter).style(Style::default().fg(Color::DarkGray)),
        editor_chunks[0],
    );

    let body: Vec<Line> = app
        .lines
        .iter()
        .skip(app.scroll)
        .take(visible_height)
        .enumerate()
        .map(|(i, text)| {
            let absolute = app.scroll + i;
            if absolute == app.cursor.0 {
                let col = app.cursor.1;
                let chars: Vec<char> = text.chars().collect();
                let before: String = chars.iter().take(col).collect();
                let at: String = chars.iter().skip(col).take(1).collect();
                let after: String = chars.iter().skip(col + 1).collect();
                Line::from(vec![
                    Span::styled(before, Style::default()),
                    Span::styled(
                        if at.is_empty() { " ".to_string() } else { at },
                        Style::default().bg(Color::Yellow).fg(Color::Black),
                    ),
                    Span::styled(after, Style::default()),
                ])
            } else {
                Line::from(text.clone())
            }
        })
        .collect();
    frame.render_widget(
        Paragraph::new(body).wrap(Wrap { trim: false }),
        editor_chunks[1],
    );

    // Output pane.
    if app.show_output {
        let out: Vec<Line> = app.output.iter().rev().take(10).rev().map(|l| Line::from(l.clone())).collect();
        frame.render_widget(
            Paragraph::new(out)
                .block(Block::default().borders(Borders::TOP).title(" Output "))
                .wrap(Wrap { trim: false }),
            chunks[2],
        );
    } else {
        frame.render_widget(Paragraph::new(""), chunks[2]);
    }

    // Status bar / prompt.
    let status = if let Some((_, value)) = &app.prompt {
        format!(" Open file: {}_", value)
    } else {
        let (line, col) = app.cursor;
        format!(
            " {} | Ln {}, Col {} | {} | Ctrl+O open  Ctrl+S save  F5 run  F6 lint  F7 format  Ctrl+Q quit",
            app.status,
            line + 1,
            col + 1,
            app.engine()
        )
    };
    frame.render_widget(
        Paragraph::new(status).style(Style::default().bg(Color::Blue).fg(Color::White)),
        chunks[3],
    );
}

fn run_app(terminal: &mut Terminal<CrosstermBackend<io::Stdout>>, app: &mut App) -> io::Result<()> {
    while !app.quit {
        terminal.draw(|f| ui(f, app))?;
        if event::poll(Duration::from_millis(100))? {
            if let Event::Key(key) = event::read()? {
                app.handle_key(key);
            }
        }
    }
    Ok(())
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    enable_raw_mode()?;
    let mut stdout = stdout();
    execute!(stdout, EnterAlternateScreen)?;
    let backend = CrosstermBackend::new(stdout);
    let mut terminal = Terminal::new(backend)?;

    let mut app = App::new();
    // Optional first argument: a file to open on startup.
    if let Some(arg) = std::env::args().nth(1) {
        app.open(PathBuf::from(arg));
    }

    let result = run_app(&mut terminal, &mut app);

    disable_raw_mode()?;
    execute!(terminal.backend_mut(), LeaveAlternateScreen)?;
    terminal.show_cursor()?;

    result?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_insert_and_backspace() {
        let mut app = App::new();
        app.insert("int x = 5;");
        assert_eq!(app.lines[0], "int x = 5;");
        assert_eq!(app.cursor.1, 10);
        app.backspace();
        assert_eq!(app.lines[0], "int x = 5");
        assert_eq!(app.cursor.1, 9);
    }

    #[test]
    fn test_enter_splits_line() {
        let mut app = App::new();
        app.insert("abc");
        app.cursor.1 = 1;
        app.enter();
        assert_eq!(app.lines, vec!["a", "bc"]);
        assert_eq!(app.cursor, (1, 0));
    }

    #[test]
    fn test_tab_inserts_four_spaces() {
        let mut app = App::new();
        app.insert("    ");
        assert_eq!(app.lines[0], "    ");
    }

    #[test]
    fn test_engine_detection() {
        let mut app = App::new();
        assert_eq!(app.engine(), "lynxer");
        app.path = Some(PathBuf::from("app.wynx"));
        assert_eq!(app.engine(), "wynxer");
        app.path = Some(PathBuf::from("index.wpf"));
        assert_eq!(app.engine(), "wynxer");
    }
}
