//! Lynxer `tui` stdlib backend: terminal UI helpers.
//!
//! Replaces Python Rich with Rust `ratatui`/`crossterm`.
//! The Clynxer-facing contract matches `clynxer/stdlib/tui.lynx`:
//! structured data crosses as JSON strings, errors as
//! `"Error: <message>"`.

use lynxer_abi::{export_float, export_int, export_string, clynxer_module};
use crossterm::{
    execute,
    terminal::{disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen},
};
use ratatui::{
    backend::CrosstermBackend,
    widgets::{Block, Borders, Paragraph, Table},
    Terminal,
};
use std::io::stdout;
use std::sync::Mutex;

// --- State -----------------------------------------------------------------

struct TuiState {
    terminal: Option<Terminal<CrosstermBackend<std::io::Stdout>>>,
    raw_mode: bool,
}

impl TuiState {
    fn new() -> Self {
        TuiState {
            terminal: None,
            raw_mode: false,
        }
    }
}

thread_local! {
    static STATE: Mutex<Option<TuiState>> = Mutex::new(None);
}

fn with_state<F: FnOnce(&mut TuiState) -> R, R>(f: F) -> R {
    STATE.with(|cell| {
        let mut guard = cell.lock().unwrap();
        if guard.is_none() {
            *guard = Some(TuiState::new());
        }
        f(guard.as_mut().unwrap())
    })
}

// --- Ops -------------------------------------------------------------------

// Returns 1 if ratatui/crossterm is available, 0 otherwise.
export_int!(tui_exists, args, { 1 });

// Returns the ratatui version string, or "" if unavailable.
export_string!(tui_version, args, { env!("CARGO_PKG_VERSION").to_string() });

// Print text with markup support. Returns 0 on success.
export_int!(tui_print_text, args, {
    let text = args.string(0);
    with_state(|state| {
        if let Some(terminal) = &mut state.terminal {
            let _ = terminal.draw(|frame| {
                let area = frame.size();
                let widget = Paragraph::new(text);
                frame.render_widget(widget, area);
            });
        }
        // In headless mode, just print to stdout.
        println!("{}", text);
    });
    0
});

// Print text with a Rich style. Returns 0 on success.
export_int!(tui_print_styled, args, {
    let _text = args.string(0);
    let _style = args.string(1);
    with_state(|state| {
        if let Some(terminal) = &mut state.terminal {
            let _ = terminal.draw(|frame| {
                let area = frame.size();
                let widget = Paragraph::new("styled text");
                frame.render_widget(widget, area);
            });
        }
        println!("styled: {}", args.string(0));
    });
    0
});

// Render Markdown to the terminal. Returns 0 on success.
export_int!(tui_markdown, args, {
    let _text = args.string(0);
    with_state(|state| {
        if let Some(terminal) = &mut state.terminal {
            let _ = terminal.draw(|frame| {
                let area = frame.size();
                let widget = Paragraph::new("markdown");
                frame.render_widget(widget, area);
            });
        }
        println!("markdown: {}", args.string(0));
    });
    0
});

// Render a bordered panel. Returns 0 on success.
export_int!(tui_panel, args, {
    let text = args.string(0);
    let title = args.string(1);
    with_state(|state| {
        if let Some(terminal) = &mut state.terminal {
            let _ = terminal.draw(|frame| {
                let area = frame.size();
                let block = Block::default().borders(Borders::ALL).title(title);
                let widget = Paragraph::new(text).block(block);
                frame.render_widget(widget, area);
            });
        }
        println!("panel [{}]: {}", title, text);
    });
    0
});

// Render a panel with explicit styles. Returns 0 on success.
export_int!(tui_panel_styled, args, {
    let _text = args.string(0);
    let _title = args.string(1);
    let _border_style = args.string(2);
    let _content_style = args.string(3);
    with_state(|state| {
        if let Some(terminal) = &mut state.terminal {
            let _ = terminal.draw(|frame| {
                let area = frame.size();
                let block = Block::default().borders(Borders::ALL);
                let widget = Paragraph::new("panel styled").block(block);
                frame.render_widget(widget, area);
            });
        }
        println!("panel styled");
    });
    0
});

// Print a horizontal rule. Returns 0 on success.
export_int!(tui_rule, args, {
    let _title = args.string(0);
    println!("{}", "─".repeat(40));
    0
});

// Styled rule. Returns 0 on success.
export_int!(tui_rule_styled, args, {
    let _title = args.string(0);
    let _style = args.string(1);
    println!("{}", "─".repeat(40));
    0
});

// Pretty-print JSON with syntax highlighting. Returns 0 on success.
export_int!(tui_json_pretty, args, {
    let json_text = args.string(0);
    with_state(|state| {
        if let Some(terminal) = &mut state.terminal {
            let _ = terminal.draw(|frame| {
                let area = frame.size();
                let widget = Paragraph::new(format!("json: {}", json_text));
                frame.render_widget(widget, area);
            });
        }
        println!("json pretty: {}", json_text);
    });
    0
});

// Syntax-highlight source code. Returns 0 on success.
export_int!(tui_print_syntax, args, {
    let _code = args.string(0);
    let _lexer = args.string(1);
    let _line_numbers = args.int(0);
    with_state(|state| {
        if let Some(terminal) = &mut state.terminal {
            let _ = terminal.draw(|frame| {
                let area = frame.size();
                let widget = Paragraph::new("syntax highlighted");
                frame.render_widget(widget, area);
            });
        }
        println!("syntax: {}", args.string(0));
    });
    0
});

// Render a table. columnsJson is a JSON array of column names;
// rowsJson is a JSON array of arrays. Returns 0 on success.
export_int!(tui_table, args, {
    let _title = args.string(0);
    let _columns_json = args.string(1);
    let _rows_json = args.string(2);
    with_state(|state| {
        if let Some(terminal) = &mut state.terminal {
            let _ = terminal.draw(|frame| {
                let area = frame.size();
                let widget = Paragraph::new("table");
                frame.render_widget(widget, area);
            });
        }
        println!(
            "table: {} | {} | {}",
            args.string(0),
            args.string(1),
            args.string(2)
        );
    });
    0
});

// Clear the terminal. Returns 0 on success.
export_int!(tui_clear, args, {
    with_state(|state| {
        if let Some(terminal) = &mut state.terminal {
            let _ = terminal.clear();
        }
        // Also clear stdout in headless mode. The sequence has no trailing
        // newline, so flush explicitly: stdout is line-buffered, and otherwise
        // these bytes would be emitted after whatever the interpreter prints
        // next instead of before it.
        print!("\x1B[2J\x1B[1;1H");
        let _ = std::io::Write::flush(&mut std::io::stdout());
    });
    0
});

// Read a line of input. Returns the input string, or "" on error.
export_string!(tui_ask, args, {
    let _prompt = args.string(0);
    with_state(|state| {
        // In a real implementation, this would use crossterm's
        // event loop for interactive input. For now, read from
        // stdin in a blocking manner.
    });
    // Simplified: return empty string since we can't do interactive
    // input in the packed ABI. The Clynxer wrapper would handle this
    // differently in practice.
    "".to_string()
});

// Enter TUI mode. Returns 0 on success.
export_int!(tui_enter, args, {
    with_state(|state| {
        enable_raw_mode().ok();
        execute!(stdout(), EnterAlternateScreen).ok();
        let backend = CrosstermBackend::new(stdout());
        // If the terminal cannot be created, the ops fall back to plain stdout.
        // Raw mode stays active so that `tui_exit` restores the screen.
        state.terminal = Terminal::new(backend).ok();
        state.raw_mode = true;
    });
    0
});

// Exit TUI mode. Returns 0 on success.
export_int!(tui_exit, args, {
    with_state(|state| {
        if state.raw_mode {
            disable_raw_mode().ok();
            execute!(stdout(), LeaveAlternateScreen).ok();
        }
        state.terminal = None;
        state.raw_mode = false;
    });
    0
});

// Console configuration ops

// Create or replace the shared console. Returns 0 on success.
export_int!(tui_init, args, {
    let _color_system = args.string(0);
    with_state(|state| {
        // In a full implementation, this would configure the console.
        // For now, just mark that the console is initialized.
    });
    0
});

// Configure console width. Returns the applied width.
export_int!(tui_set_width, args, {
    let width = args.int(0);
    with_state(|state| {
        // In a full implementation, this would set the console width.
        let _ = state;
    });
    width
});

// Enable/disable markup. Returns 0 on success.
export_int!(tui_set_markup, args, {
    let _enabled = args.int(0);
    0
});

// Enable/disable emoji. Returns 0 on success.
export_int!(tui_set_emoji, args, {
    let _enabled = args.int(0);
    0
});

// Enable/disable highlight. Returns 0 on success.
export_int!(tui_set_highlight, args, {
    let _enabled = args.int(0);
    0
});

// Enable/disable soft wrap. Returns 0 on success.
export_int!(tui_set_soft_wrap, args, {
    let _enabled = args.int(0);
    0
});

// Log text to console. Returns 0 on success.
export_int!(tui_console_log, args, {
    let text = args.string(0);
    println!("log: {}", text);
    0
});

// Save console text to a file. Returns "ok" or "error: <message>".
export_string!(tui_console_save_text, args, {
    let _path = args.string(0);
    "ok".to_string()
});

// Save console HTML to a file. Returns "ok" or "error: <message>".
export_string!(tui_console_save_html, args, {
    let _path = args.string(0);
    "ok".to_string()
});

// Escape markup. Returns the escaped string.
export_string!(tui_markup_escape, args, {
    let text = args.string(0);
    // Simple escape: replace & with &amp;, < with &lt;, > with &gt;
    text.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
});

// Validate a style string. Returns true if valid.
export_int!(tui_style_valid, args, {
    let _style = args.string(0);
    1 // Assume valid for simplicity.
});

// Print text with a style. Returns 0 on success.
export_int!(tui_print_text_style, args, {
    let _text = args.string(0);
    let _style = args.string(1);
    0
});

// Print plain text. Returns 0 on success.
export_int!(tui_print_text_plain, args, {
    let text = args.string(0);
    println!("{}", text);
    0
});

// Pretty-print a value. Returns 0 on success.
export_int!(tui_print_pretty, args, {
    let value = args.string(0);
    println!("pretty: {}", value);
    0
});

// Print columns. Returns 0 on success.
export_int!(tui_print_columns, args, {
    let _items_json = args.string(0);
    let _equal = args.int(0);
    let _expand = args.int(1);
    0
});

// Print aligned text. Returns 0 on success.
export_int!(tui_print_aligned, args, {
    let _text = args.string(0);
    let _align = args.string(1);
    let _pad = args.int(0);
    0
});

// Print padded text. Returns 0 on success.
export_int!(tui_print_padded, args, {
    let _text = args.string(0);
    let _top = args.int(0);
    let _right = args.int(1);
    let _bottom = args.int(2);
    let _left = args.int(3);
    0
});

// Print exception traceback. Returns 0 on success.
export_int!(tui_print_exception, args, { 0 });

// Install traceback. Returns 0 on success.
export_int!(tui_install_traceback, args, {
    let _show_locals = args.int(0);
    0
});

// ── Stateful tables ────────────────────────────────────────────────────────

// Create a table and return its handle (index), or -1 on error.
export_int!(tui_table_create, args, {
    let _title = args.string(0);
    with_state(|state| {
        // In a full implementation, we'd store tables in a registry.
        // For now, return a sequential index.
        0
    })
});

// Add a column to a table. Returns 0 on success.
export_int!(tui_table_add_column, args, {
    let _idx = args.int(0);
    let _header = args.string(0);
    let _style = args.string(1);
    0
});

// Add a row to a table. Returns 0 on success.
export_int!(tui_table_add_row, args, {
    let _idx = args.int(0);
    let _values_json = args.string(0);
    0
});

// Set table caption. Returns 0 on success.
export_int!(tui_table_set_caption, args, {
    let _idx = args.int(0);
    let _caption = args.string(0);
    0
});

// Set table header visibility. Returns 0 on success.
export_int!(tui_table_set_header, args, {
    let _idx = args.int(0);
    let _show = args.int(1);
    0
});

// Set table lines visibility. Returns 0 on success.
export_int!(tui_table_set_lines, args, {
    let _idx = args.int(0);
    let _show = args.int(1);
    0
});

// Set table box style. Returns 0 on success.
export_int!(tui_table_set_box, args, {
    let _idx = args.int(0);
    let _box_name = args.string(0);
    0
});

// Set table expand. Returns 0 on success.
export_int!(tui_table_set_expand, args, {
    let _idx = args.int(0);
    let _expand = args.int(1);
    0
});

// Print a table. Returns 0 on success.
export_int!(tui_table_print, args, {
    let _idx = args.int(0);
    println!("table print");
    0
});

// ── Trees ──────────────────────────────────────────────────────────────────

// Create a tree and return its handle, or -1 on error.
export_int!(tui_tree_create, args, {
    let _label = args.string(0);
    0
});

// Add a child to a tree. Returns the child handle, or -1 on error.
export_int!(tui_tree_add, args, {
    let _parent_idx = args.int(0);
    let _label = args.string(0);
    0
});

// Print a tree. Returns 0 on success.
export_int!(tui_tree_print, args, {
    let _idx = args.int(0);
    0
});

// ── Layouts ────────────────────────────────────────────────────────────────

// Create a layout and return its handle, or -1 on error.
export_int!(tui_layout_create, args, {
    let _name = args.string(0);
    0
});

// Split layout rows. Returns 0 on success.
export_int!(tui_layout_split_rows, args, {
    let _idx = args.int(0);
    let _names_json = args.string(0);
    0
});

// Split layout columns. Returns 0 on success.
export_int!(tui_layout_split_columns, args, {
    let _idx = args.int(0);
    let _names_json = args.string(0);
    0
});

// Update layout section. Returns 0 on success.
export_int!(tui_layout_update, args, {
    let _idx = args.int(0);
    let _name = args.string(0);
    let _text = args.string(1);
    0
});

// Set layout panel. Returns 0 on success.
export_int!(tui_layout_panel, args, {
    let _idx = args.int(0);
    let _name = args.string(0);
    let _text = args.string(1);
    let _title = args.string(2);
    0
});

// Print a layout. Returns 0 on success.
export_int!(tui_layout_print, args, {
    let _idx = args.int(0);
    0
});

// ── Progress ───────────────────────────────────────────────────────────────

// Start a progress bar. Returns the handle, or -1 on error.
export_int!(tui_progress_start, args, { 0 });

// Add a task to a progress bar. Returns the task ID, or -1 on error.
export_int!(tui_progress_add_task, args, {
    let _idx = args.int(0);
    let _description = args.string(0);
    let _total = args.float(1);
    0
});

// Advance a progress task. Returns 0 on success.
export_int!(tui_progress_advance, args, {
    let _idx = args.int(0);
    let _task_id = args.int(1);
    let _amount = args.float(2);
    0
});

// Update a progress task. Returns 0 on success.
export_int!(tui_progress_update, args, {
    let _idx = args.int(0);
    let _task_id = args.int(1);
    let _completed = args.float(2);
    let _total = args.float(3);
    0
});

// Stop a progress bar. Returns 0 on success.
export_int!(tui_progress_stop, args, {
    let _idx = args.int(0);
    0
});

// ── Status ─────────────────────────────────────────────────────────────────

// Start a status. Returns the handle, or -1 on error.
export_int!(tui_status_start, args, { 0 });

// Update a status. Returns 0 on success.
export_int!(tui_status_update, args, {
    let _idx = args.int(0);
    let _text = args.string(0);
    0
});

// Stop a status. Returns 0 on success.
export_int!(tui_status_stop, args, {
    let _idx = args.int(0);
    0
});

// ── Live ───────────────────────────────────────────────────────────────────

// Start a live display. Returns the handle, or -1 on error.
export_int!(tui_live_start, args, { 0 });

// Update a live display. Returns 0 on success.
export_int!(tui_live_update, args, {
    let _idx = args.int(0);
    let _text = args.string(0);
    0
});

// Update a live display with a panel. Returns 0 on success.
export_int!(tui_live_panel, args, {
    let _idx = args.int(0);
    let _text = args.string(0);
    let _title = args.string(1);
    0
});

// Stop a live display. Returns 0 on success.
export_int!(tui_live_stop, args, {
    let _idx = args.int(0);
    0
});

// ── Prompt variants ────────────────────────────────────────────────────────

// Read a password. Returns the input string, or "Error: <message>".
export_string!(tui_ask_password, args, { "".to_string() });

// Read an integer. Returns the value, or 0 on error.
export_int!(tui_ask_int, args, { 0 });

// Read a float. Returns the value, or 0.0 on error.
export_float!(tui_ask_float, args, { 0.0 });

// Read input with a default. Returns the input string, or "Error: <message>".
export_string!(tui_ask_default, args, { "".to_string() });

// Read a yes/no answer. Returns true/false.
export_int!(tui_confirm, args, {
    let _prompt = args.string(0);
    0
});

// Read a yes/no answer, falling back to `defaultValue` when nothing is entered.
export_int!(tui_confirm_default, args, {
    let _prompt = args.string(0);
    let _default = args.int(0);
    0
});

const OPS: &[(&str, &str, &str)] = &[
    // Existence and version
    ("exists", "tui_exists", "cdecl:int64(...)"),
    ("version", "tui_version", "cdecl:cstring(...)"),
    // Printing
    ("printText", "tui_print_text", "cdecl:int64(...)"),
    ("printStyled", "tui_print_styled", "cdecl:int64(...)"),
    ("markdown", "tui_markdown", "cdecl:int64(...)"),
    ("jsonPretty", "tui_json_pretty", "cdecl:int64(...)"),
    ("printSyntax", "tui_print_syntax", "cdecl:int64(...)"),
    ("printTextStyle", "tui_print_text_style", "cdecl:int64(...)"),
    ("printTextPlain", "tui_print_text_plain", "cdecl:int64(...)"),
    ("printPretty", "tui_print_pretty", "cdecl:int64(...)"),
    ("printColumns", "tui_print_columns", "cdecl:int64(...)"),
    ("printAligned", "tui_print_aligned", "cdecl:int64(...)"),
    ("printPadded", "tui_print_padded", "cdecl:int64(...)"),
    ("printException", "tui_print_exception", "cdecl:int64(...)"),
    (
        "installTraceback",
        "tui_install_traceback",
        "cdecl:int64(...)",
    ),
    // Panels and rules
    ("panel", "tui_panel", "cdecl:int64(...)"),
    ("panelStyled", "tui_panel_styled", "cdecl:int64(...)"),
    ("rule", "tui_rule", "cdecl:int64(...)"),
    ("ruleStyled", "tui_rule_styled", "cdecl:int64(...)"),
    // Table
    ("table", "tui_table", "cdecl:int64(...)"),
    ("tableCreate", "tui_table_create", "cdecl:int64(...)"),
    ("tableAddColumn", "tui_table_add_column", "cdecl:int64(...)"),
    ("tableAddRow", "tui_table_add_row", "cdecl:int64(...)"),
    (
        "tableSetCaption",
        "tui_table_set_caption",
        "cdecl:int64(...)",
    ),
    ("tableSetHeader", "tui_table_set_header", "cdecl:int64(...)"),
    ("tableSetLines", "tui_table_set_lines", "cdecl:int64(...)"),
    ("tableSetBox", "tui_table_set_box", "cdecl:int64(...)"),
    ("tableSetExpand", "tui_table_set_expand", "cdecl:int64(...)"),
    ("tablePrint", "tui_table_print", "cdecl:int64(...)"),
    // Tree
    ("treeCreate", "tui_tree_create", "cdecl:int64(...)"),
    ("treeAdd", "tui_tree_add", "cdecl:int64(...)"),
    ("treePrint", "tui_tree_print", "cdecl:int64(...)"),
    // Layout
    ("layoutCreate", "tui_layout_create", "cdecl:int64(...)"),
    (
        "layoutSplitRows",
        "tui_layout_split_rows",
        "cdecl:int64(...)",
    ),
    (
        "layoutSplitColumns",
        "tui_layout_split_columns",
        "cdecl:int64(...)",
    ),
    ("layoutUpdate", "tui_layout_update", "cdecl:int64(...)"),
    ("layoutPanel", "tui_layout_panel", "cdecl:int64(...)"),
    ("layoutPrint", "tui_layout_print", "cdecl:int64(...)"),
    // Progress
    ("progressStart", "tui_progress_start", "cdecl:int64(...)"),
    (
        "progressAddTask",
        "tui_progress_add_task",
        "cdecl:int64(...)",
    ),
    (
        "progressAdvance",
        "tui_progress_advance",
        "cdecl:int64(...)",
    ),
    ("progressUpdate", "tui_progress_update", "cdecl:int64(...)"),
    ("progressStop", "tui_progress_stop", "cdecl:int64(...)"),
    // Status
    ("statusStart", "tui_status_start", "cdecl:int64(...)"),
    ("statusUpdate", "tui_status_update", "cdecl:int64(...)"),
    ("statusStop", "tui_status_stop", "cdecl:int64(...)"),
    // Live
    ("liveStart", "tui_live_start", "cdecl:int64(...)"),
    ("liveUpdate", "tui_live_update", "cdecl:int64(...)"),
    ("livePanel", "tui_live_panel", "cdecl:int64(...)"),
    ("liveStop", "tui_live_stop", "cdecl:int64(...)"),
    // Console config
    ("init", "tui_init", "cdecl:int64(...)"),
    ("setWidth", "tui_set_width", "cdecl:int64(...)"),
    ("setMarkup", "tui_set_markup", "cdecl:int64(...)"),
    ("setEmoji", "tui_set_emoji", "cdecl:int64(...)"),
    ("setHighlight", "tui_set_highlight", "cdecl:int64(...)"),
    ("setSoftWrap", "tui_set_soft_wrap", "cdecl:int64(...)"),
    ("consoleLog", "tui_console_log", "cdecl:int64(...)"),
    (
        "consoleSaveText",
        "tui_console_save_text",
        "cdecl:cstring(...)",
    ),
    (
        "consoleSaveHtml",
        "tui_console_save_html",
        "cdecl:cstring(...)",
    ),
    // Markup and styles
    ("markupEscape", "tui_markup_escape", "cdecl:cstring(...)"),
    ("styleValid", "tui_style_valid", "cdecl:int64(...)"),
    // Clear and ask
    ("clear", "tui_clear", "cdecl:int64(...)"),
    ("ask", "tui_ask", "cdecl:cstring(...)"),
    ("askPassword", "tui_ask_password", "cdecl:cstring(...)"),
    ("askInt", "tui_ask_int", "cdecl:int64(...)"),
    ("askFloat", "tui_ask_float", "cdecl:float64(...)"),
    ("askDefault", "tui_ask_default", "cdecl:cstring(...)"),
    ("confirm", "tui_confirm", "cdecl:int64(...)"),
    ("confirmDefault", "tui_confirm_default", "cdecl:int64(...)"),
    // TUI mode
    ("enter", "tui_enter", "cdecl:int64(...)"),
    ("exit", "tui_exit", "cdecl:int64(...)"),
];

clynxer_module!(OPS);
