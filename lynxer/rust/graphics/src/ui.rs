//! Immediate-mode UI widgets.
//!
//! Widgets are addressed by an integer id. Values (checkbox state, slider
//! position, input text, combo selection) live in the module's own registry so
//! Lynxer can read and write them directly; button clicks land in the result
//! registry and are read with `uiResult(id)`.
//!
//! macroquad's window and group take a closure, which a flat op API cannot
//! nest. `uiWindowBegin`/`uiGroupBegin` therefore buffer the widgets issued
//! until the matching `*End`, then replay them inside that closure. One block
//! deep is supported; a `*Begin` while a block is open is rejected with `-1`.

use std::collections::HashMap;

use macroquad::color::Color;
use macroquad::math::vec2;
use macroquad::ui::widgets::Window;
use macroquad::ui::{root_ui, Id, Ui};

use crate::state::{with, UiBlock, UiCommand, UiValue};

/// macroquad's `Id` is a plain `u64`.
fn id_of(value: i64) -> Id {
    value.max(0) as u64
}

fn headless() -> bool {
    with(|state| state.headless)
}

fn buffering() -> bool {
    with(|state| state.buffering())
}

fn push(command: UiCommand) {
    with(|state| state.ui_buffer.push(command));
}

fn position(point: Option<(f32, f32)>) -> Option<macroquad::math::Vec2> {
    point.map(|(x, y)| vec2(x, y))
}

fn fraction(value: f32, min: f32, max: f32) -> f32 {
    if max > min {
        ((value - min) / (max - min)).clamp(0.0, 1.0)
    } else {
        0.0
    }
}

pub fn label(text: &str, point: Option<(f32, f32)>) {
    if buffering() {
        push(UiCommand::Label(text.to_string(), point));
        return;
    }
    if headless() {
        return;
    }
    let mut ui = root_ui();
    ui.label(position(point), text);
}

pub fn button(id: i64, text: &str, point: Option<(f32, f32)>) -> bool {
    if buffering() {
        push(UiCommand::Button {
            id,
            text: text.to_string(),
            position: point,
        });
        return false;
    }
    if headless() {
        return false;
    }
    let clicked = {
        let mut ui = root_ui();
        ui.button(position(point), text)
    };
    with(|state| {
        state.ui_results.insert(id, clicked as i64);
    });
    clicked
}

pub fn checkbox(id: i64, label: &str) -> bool {
    if buffering() {
        push(UiCommand::Checkbox {
            id,
            label: label.to_string(),
        });
        return with(|state| state.ui_bool(id, false));
    }
    if headless() {
        return false;
    }
    let mut value = with(|state| state.ui_bool(id, false));
    {
        let mut ui = root_ui();
        ui.checkbox(id_of(id), label, &mut value);
    }
    with(|state| {
        state.ui_values.insert(id, UiValue::Bool(value));
    });
    value
}

pub fn slider(id: i64, label: &str, min: f32, max: f32) -> f32 {
    if buffering() {
        push(UiCommand::Slider {
            id,
            label: label.to_string(),
            min,
            max,
        });
        return with(|state| state.ui_float(id, min));
    }
    if headless() {
        return 0.0;
    }
    let mut value = with(|state| state.ui_float(id, min));
    {
        let mut ui = root_ui();
        ui.slider(id_of(id), label, min..max, &mut value);
    }
    with(|state| {
        state.ui_values.insert(id, UiValue::Float(value));
    });
    value
}

pub fn input_text(id: i64, label: &str, password: bool) -> String {
    if buffering() {
        push(UiCommand::InputText {
            id,
            label: label.to_string(),
            password,
        });
        return with(|state| state.ui_text(id));
    }
    if headless() {
        return String::new();
    }
    let mut value = with(|state| state.ui_text(id));
    {
        let mut ui = root_ui();
        if password {
            ui.input_password(id_of(id), label, &mut value);
        } else {
            ui.input_text(id_of(id), label, &mut value);
        }
    }
    with(|state| {
        state.ui_values.insert(id, UiValue::Text(value.clone()));
    });
    value
}

pub fn progress_bar(label: &str, value: f32, min: f32, max: f32) {
    if buffering() {
        push(UiCommand::ProgressBar {
            label: label.to_string(),
            value,
            min,
            max,
        });
        return;
    }
    if headless() {
        return;
    }
    let mut ui = root_ui();
    ui.progress_bar(label, fraction(value, min, max));
}

pub fn combo_box(id: i64, label: &str, options: &[String]) -> i64 {
    with(|state| {
        state.ui_options.insert(id, options.to_vec());
    });
    if buffering() {
        push(UiCommand::ComboBox {
            id,
            label: label.to_string(),
            options: options.to_vec(),
        });
        return with(|state| state.ui_int(id, 0));
    }
    if headless() {
        return 0;
    }
    let variants: Vec<&str> = options.iter().map(|option| option.as_str()).collect();
    let mut selected = with(|state| state.ui_int(id, 0)).max(0) as usize;
    let chosen = {
        let mut ui = root_ui();
        ui.combo_box(id_of(id), label, &variants, Some(&mut selected))
    };
    with(|state| {
        state.ui_values.insert(id, UiValue::Int(chosen as i64));
    });
    chosen as i64
}

pub fn separator() {
    if buffering() {
        push(UiCommand::Separator);
        return;
    }
    if headless() {
        return;
    }
    let mut ui = root_ui();
    ui.separator();
}

pub fn same_line(x: f32) {
    if buffering() {
        push(UiCommand::SameLine(x));
        return;
    }
    if headless() {
        return;
    }
    let mut ui = root_ui();
    ui.same_line(x);
}

pub fn window_begin(id: i64, title: &str, x: f32, y: f32, w: f32, h: f32) -> i64 {
    with(|state| {
        if state.ui_block.is_some() {
            return -1;
        }
        state.ui_buffer.clear();
        state.ui_block = Some(UiBlock::Window {
            id,
            title: title.to_string(),
            x,
            y,
            w,
            h,
        });
        0
    })
}

pub fn window_end() -> i64 {
    let (block, commands) =
        with(|state| (state.ui_block.take(), std::mem::take(&mut state.ui_buffer)));
    let (id, title, x, y, w, h) = match block {
        Some(UiBlock::Window {
            id,
            title,
            x,
            y,
            w,
            h,
        }) => (id, title, x, y, w, h),
        _ => return -1,
    };
    if headless() {
        draw_headless(Some((&title, x, y, w, h)), &commands);
        return 0;
    }
    let mut values = with(|state| state.ui_values.clone());
    let mut results: HashMap<i64, i64> = HashMap::new();
    {
        let mut ui = root_ui();
        Window::new(id_of(id), vec2(x, y), vec2(w, h))
            .label(&title)
            .ui(&mut ui, |ui| {
                for command in &commands {
                    execute(ui, command, &mut values, &mut results);
                }
            });
    }
    with(|state| {
        state.ui_values.extend(values);
        state.ui_results.extend(results);
    });
    0
}

/// One widget resolved for the headless layout pass.
enum HeadlessWidget {
    Label(String),
    Box(String, Color),
    Checkbox(String, bool),
    Bar(String, f32),
    Rule,
}

/// Rasterizes a buffered UI block into the CPU framebuffer. There is no input
/// or hit-testing headless (a button is never "clicked"), and the layout is a
/// simple stacked approximation of macroquad's, not a pixel-exact copy.
fn draw_headless(window: Option<(&str, f32, f32, f32, f32)>, commands: &[UiCommand]) {
    let text_color = Color::new(0.9, 0.9, 0.95, 1.0);
    let box_color = Color::new(0.28, 0.28, 0.34, 1.0);
    let widgets: Vec<HeadlessWidget> = commands
        .iter()
        .map(|command| match command {
            UiCommand::Label(text, _) => HeadlessWidget::Label(text.clone()),
            UiCommand::Button { text, .. } => HeadlessWidget::Box(text.clone(), box_color),
            UiCommand::InputText { id, label, .. } => {
                let value = with(|state| state.ui_text(*id));
                HeadlessWidget::Box(
                    format!("{label}: {value}"),
                    Color::new(0.16, 0.16, 0.2, 1.0),
                )
            }
            UiCommand::ComboBox { id, options, .. } => {
                let index = with(|state| state.ui_int(*id, 0)).max(0) as usize;
                let selected = options.get(index).cloned().unwrap_or_default();
                HeadlessWidget::Box(selected, box_color)
            }
            UiCommand::Checkbox { id, label } => {
                HeadlessWidget::Checkbox(label.clone(), with(|state| state.ui_bool(*id, false)))
            }
            UiCommand::Slider {
                id,
                label,
                min,
                max,
            } => HeadlessWidget::Bar(
                label.clone(),
                fraction(with(|state| state.ui_float(*id, *min)), *min, *max),
            ),
            UiCommand::ProgressBar {
                label,
                value,
                min,
                max,
            } => HeadlessWidget::Bar(label.clone(), fraction(*value, *min, *max)),
            UiCommand::Separator => HeadlessWidget::Rule,
            UiCommand::SameLine(_) => HeadlessWidget::Label(String::new()),
        })
        .collect();
    let font = crate::font::builtin().clone();
    crate::raster::with_framebuffer(|framebuffer| {
        let (x, mut cursor, width) = match window {
            Some((title, x, y, w, h)) => {
                framebuffer.rectangle(x, y, w, h, Color::new(0.12, 0.12, 0.16, 0.95));
                framebuffer.rectangle_lines(x, y, w, h, 1.0, Color::new(0.4, 0.4, 0.5, 1.0));
                framebuffer.rectangle(x, y, w, 24.0, Color::new(0.2, 0.2, 0.26, 1.0));
                font.draw(framebuffer, title, x + 8.0, y + 17.0, 16.0, 0.0, text_color);
                (x + 8.0, y + 34.0, w - 16.0)
            }
            None => (0.0, 8.0, 200.0),
        };
        for widget in &widgets {
            match widget {
                HeadlessWidget::Label(text) => {
                    font.draw(framebuffer, text, x, cursor + 12.0, 16.0, 0.0, text_color);
                    cursor += 20.0;
                }
                HeadlessWidget::Box(text, color) => {
                    framebuffer.rectangle(x, cursor, width, 22.0, *color);
                    font.draw(
                        framebuffer,
                        text,
                        x + 6.0,
                        cursor + 16.0,
                        16.0,
                        0.0,
                        text_color,
                    );
                    cursor += 28.0;
                }
                HeadlessWidget::Checkbox(text, checked) => {
                    let fill = if *checked {
                        Color::new(0.3, 0.7, 0.4, 1.0)
                    } else {
                        Color::new(0.16, 0.16, 0.2, 1.0)
                    };
                    framebuffer.rectangle(x, cursor, 14.0, 14.0, fill);
                    framebuffer.rectangle_lines(x, cursor, 14.0, 14.0, 1.0, text_color);
                    font.draw(
                        framebuffer,
                        text,
                        x + 20.0,
                        cursor + 12.0,
                        16.0,
                        0.0,
                        text_color,
                    );
                    cursor += 22.0;
                }
                HeadlessWidget::Bar(text, value) => {
                    framebuffer.rectangle(x, cursor, width, 16.0, Color::new(0.16, 0.16, 0.2, 1.0));
                    framebuffer.rectangle(
                        x,
                        cursor,
                        width * value.clamp(0.0, 1.0),
                        16.0,
                        Color::new(0.3, 0.55, 0.85, 1.0),
                    );
                    framebuffer.rectangle_lines(x, cursor, width, 16.0, 1.0, text_color);
                    font.draw(
                        framebuffer,
                        text,
                        x + 4.0,
                        cursor + 12.0,
                        14.0,
                        0.0,
                        text_color,
                    );
                    cursor += 24.0;
                }
                HeadlessWidget::Rule => {
                    framebuffer.line(x, cursor + 6.0, x + width, cursor + 6.0, 1.0, text_color);
                    cursor += 16.0;
                }
            }
        }
    });
}

pub fn group_begin(id: i64, w: f32, h: f32) -> i64 {
    with(|state| {
        if state.ui_block.is_some() {
            return -1;
        }
        state.ui_buffer.clear();
        state.ui_block = Some(UiBlock::Group { id, w, h });
        0
    })
}

pub fn group_end() -> i64 {
    let (block, commands) =
        with(|state| (state.ui_block.take(), std::mem::take(&mut state.ui_buffer)));
    let (id, w, h) = match block {
        Some(UiBlock::Group { id, w, h }) => (id, w, h),
        _ => return -1,
    };
    if headless() {
        draw_headless(None, &commands);
        return 0;
    }
    let mut values = with(|state| state.ui_values.clone());
    let mut results: HashMap<i64, i64> = HashMap::new();
    {
        let mut ui = root_ui();
        ui.group(id_of(id), vec2(w, h), |ui| {
            for command in &commands {
                execute(ui, command, &mut values, &mut results);
            }
        });
    }
    with(|state| {
        state.ui_values.extend(values);
        state.ui_results.extend(results);
    });
    0
}

fn execute(
    ui: &mut Ui,
    command: &UiCommand,
    values: &mut HashMap<i64, UiValue>,
    results: &mut HashMap<i64, i64>,
) {
    match command {
        UiCommand::Label(text, point) => ui.label(position(*point), text),
        UiCommand::Button {
            id,
            text,
            position: point,
        } => {
            let clicked = ui.button(position(*point), text.as_str());
            results.insert(*id, clicked as i64);
        }
        UiCommand::Checkbox { id, label } => {
            let mut value = match values.get(id) {
                Some(UiValue::Bool(value)) => *value,
                _ => false,
            };
            ui.checkbox(id_of(*id), label, &mut value);
            values.insert(*id, UiValue::Bool(value));
        }
        UiCommand::Slider {
            id,
            label,
            min,
            max,
        } => {
            let mut value = match values.get(id) {
                Some(UiValue::Float(value)) => *value,
                _ => *min,
            };
            ui.slider(id_of(*id), label, *min..*max, &mut value);
            values.insert(*id, UiValue::Float(value));
        }
        UiCommand::InputText {
            id,
            label,
            password,
        } => {
            let mut value = match values.get(id) {
                Some(UiValue::Text(value)) => value.clone(),
                _ => String::new(),
            };
            if *password {
                ui.input_password(id_of(*id), label, &mut value);
            } else {
                ui.input_text(id_of(*id), label, &mut value);
            }
            values.insert(*id, UiValue::Text(value));
        }
        UiCommand::ProgressBar {
            label,
            value,
            min,
            max,
        } => ui.progress_bar(label, fraction(*value, *min, *max)),
        UiCommand::ComboBox { id, label, options } => {
            let variants: Vec<&str> = options.iter().map(|option| option.as_str()).collect();
            let mut selected = match values.get(id) {
                Some(UiValue::Int(value)) => (*value).max(0) as usize,
                _ => 0,
            };
            let chosen = ui.combo_box(id_of(*id), label, &variants, Some(&mut selected));
            values.insert(*id, UiValue::Int(chosen as i64));
        }
        UiCommand::Separator => ui.separator(),
        UiCommand::SameLine(x) => ui.same_line(*x),
    }
}

pub fn checkbox_value(id: i64) -> bool {
    with(|state| state.ui_bool(id, false))
}

pub fn set_checkbox_value(id: i64, value: bool) {
    with(|state| {
        state.ui_values.insert(id, UiValue::Bool(value));
    });
}

pub fn slider_value(id: i64) -> f32 {
    with(|state| state.ui_float(id, 0.0))
}

pub fn set_slider_value(id: i64, value: f32) {
    with(|state| {
        state.ui_values.insert(id, UiValue::Float(value));
    });
}

pub fn input_text_value(id: i64) -> String {
    with(|state| state.ui_text(id))
}

pub fn set_input_text(id: i64, value: &str) {
    with(|state| {
        state.ui_values.insert(id, UiValue::Text(value.to_string()));
    });
}

pub fn combo_box_value(id: i64) -> String {
    with(|state| state.ui_selected_text(id))
}

pub fn result(id: i64) -> i64 {
    with(|state| state.ui_result(id))
}

/// Reads a JSON string array such as `["one","two"]` into its elements.
pub fn parse_options(text: &str) -> Vec<String> {
    let mut values = Vec::new();
    let mut parts = text.split('"');
    parts.next();
    while let Some(value) = parts.next() {
        values.push(value.to_string());
        if parts.next().is_none() {
            break;
        }
    }
    values
}
