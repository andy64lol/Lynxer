//! Immediate-mode UI widgets.
//!
//! Widgets are addressed by an integer id. Values (checkbox state, slider
//! position, input text, combo selection) live in the module's own registry so
//! Lynxer can read and write them directly; button clicks land in the result
//! registry and are read with `uiResult(id)`.
//!
//! macroquad's window and group take a closure, so the flat op API buffers
//! commands on a stack and replays each completed block in its parent.

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
    with(|state| {
        if let Some((_, commands)) = state.ui_stack.last_mut() {
            commands.push(command);
        } else {
            state.ui_buffer.push(command);
        }
    });
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
        if state.ui_stack.is_empty() {
            state.ui_buffer.clear();
        }
        state.ui_stack.push((
            UiBlock::Window {
                id,
                title: title.to_string(),
                x,
                y,
                w,
                h,
            },
            Vec::new(),
        ));
        0
    })
}

pub fn window_end() -> i64 {
    finish_block(true)
}

/// One widget resolved for the headless layout pass.
enum HeadlessWidget {
    Label(String),
    Box(String, Color),
    Checkbox(String, bool),
    Bar(String, f32),
    Rule,
    Nested(UiBlock, Vec<HeadlessWidget>),
}

pub fn group_begin(id: i64, w: f32, h: f32) -> i64 {
    with(|state| {
        if state.ui_stack.is_empty() {
            state.ui_buffer.clear();
        }
        state
            .ui_stack
            .push((UiBlock::Group { id, w, h }, Vec::new()));
        0
    })
}

pub fn group_end() -> i64 {
    finish_block(false)
}

fn headless_widgets(commands: &[UiCommand]) -> Vec<HeadlessWidget> {
    commands
        .iter()
        .map(|command| match command {
            UiCommand::Block(block, children) => {
                HeadlessWidget::Nested(block.clone(), headless_widgets(children))
            }
            UiCommand::Label(text, _) => HeadlessWidget::Label(text.clone()),
            UiCommand::Button { text, .. } => {
                HeadlessWidget::Box(text.clone(), Color::new(0.28, 0.28, 0.34, 1.0))
            }
            UiCommand::InputText { id, label, .. } => HeadlessWidget::Box(
                format!("{label}: {}", with(|state| state.ui_text(*id))),
                Color::new(0.16, 0.16, 0.2, 1.0),
            ),
            UiCommand::ComboBox { id, options, .. } => {
                let index = with(|state| state.ui_int(*id, 0)).max(0) as usize;
                HeadlessWidget::Box(
                    options.get(index).cloned().unwrap_or_default(),
                    Color::new(0.28, 0.28, 0.34, 1.0),
                )
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
        .collect()
}

fn draw_headless_children(
    framebuffer: &mut crate::raster::Framebuffer,
    font: &crate::font::RasterFont,
    widgets: &[HeadlessWidget],
    x: f32,
    y: f32,
    width: f32,
    text_color: Color,
    box_color: Color,
) {
    let mut cursor = y;
    for widget in widgets {
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
                framebuffer.rectangle(
                    x,
                    cursor,
                    14.0,
                    14.0,
                    if *checked {
                        Color::new(0.3, 0.7, 0.4, 1.0)
                    } else {
                        box_color
                    },
                );
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
                framebuffer.rectangle(
                    x,
                    cursor,
                    width * value.clamp(0.0, 1.0),
                    16.0,
                    Color::new(0.3, 0.55, 0.85, 1.0),
                );
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
            HeadlessWidget::Nested(block, children) => {
                let (title, requested_width, requested_height) = match block {
                    UiBlock::Window { title, w, h, .. } => (title.clone(), *w, *h),
                    UiBlock::Group { id, w, h } => (format!("Group {id}"), *w, *h),
                };
                let nested_width = if requested_width > 0.0 {
                    requested_width.min(width)
                } else {
                    width
                };
                let height = if requested_height > 0.0 {
                    requested_height
                } else {
                    (children.len() as f32 * 28.0 + 34.0).max(52.0)
                };
                framebuffer.rectangle(x, cursor, nested_width, height, box_color);
                framebuffer.rectangle_lines(x, cursor, nested_width, height, 1.0, text_color);
                font.draw(
                    framebuffer,
                    &title,
                    x + 6.0,
                    cursor + 16.0,
                    15.0,
                    0.0,
                    text_color,
                );
                draw_headless_children(
                    framebuffer,
                    font,
                    children,
                    x + 8.0,
                    cursor + 24.0,
                    nested_width - 16.0,
                    text_color,
                    box_color,
                );
                cursor += height + 6.0;
            }
        }
    }
}

fn finish_block(expect_window: bool) -> i64 {
    let (block, commands, parent_open, valid) = with(|state| {
        let Some((current, _)) = state.ui_stack.last() else {
            return (None, Vec::new(), false, false);
        };
        if matches!(current, UiBlock::Window { .. }) != expect_window {
            return (None, Vec::new(), false, false);
        }
        let (block, commands) = state.ui_stack.pop().expect("block checked above");
        let parent_open = !state.ui_stack.is_empty();
        if let Some((_, parent_commands)) = state.ui_stack.last_mut() {
            parent_commands.push(UiCommand::Block(block, commands));
            return (None, Vec::new(), true, true);
        }
        (Some(block), commands, parent_open, true)
    });
    if !valid {
        return -1;
    }
    if parent_open {
        return 0;
    }
    let Some(block) = block else { return -1 };
    if headless() {
        draw_headless(Some(&block), &commands);
        return 0;
    }
    let mut values = with(|state| state.ui_values.clone());
    let mut results = HashMap::new();
    {
        let mut ui = root_ui();
        execute_block(&mut ui, &block, &commands, &mut values, &mut results);
    }
    with(|state| {
        state.ui_values.extend(values);
        state.ui_results.extend(results);
    });
    0
}

fn execute_block(
    ui: &mut Ui,
    block: &UiBlock,
    commands: &[UiCommand],
    values: &mut HashMap<i64, UiValue>,
    results: &mut HashMap<i64, i64>,
) {
    match block {
        UiBlock::Window {
            id,
            title,
            x,
            y,
            w,
            h,
        } => {
            Window::new(id_of(*id), vec2(*x, *y), vec2(*w, *h))
                .label(title)
                .ui(ui, |ui| execute_all(ui, commands, values, results));
        }
        UiBlock::Group { id, w, h } => {
            ui.group(id_of(*id), vec2(*w, *h), |ui| {
                execute_all(ui, commands, values, results)
            });
        }
    }
}

fn execute_all(
    ui: &mut Ui,
    commands: &[UiCommand],
    values: &mut HashMap<i64, UiValue>,
    results: &mut HashMap<i64, i64>,
) {
    for command in commands {
        if let UiCommand::Block(block, children) = command {
            execute_block(ui, block, children, values, results);
        } else {
            execute(ui, command, values, results);
        }
    }
}

fn draw_headless(block: Option<&UiBlock>, commands: &[UiCommand]) {
    let text_color = Color::new(0.9, 0.9, 0.95, 1.0);
    let box_color = Color::new(0.28, 0.28, 0.34, 1.0);
    let widgets = headless_widgets(commands);
    let font = crate::font::builtin().clone();
    crate::raster::with_framebuffer(|framebuffer| {
        let (x, y, width) = match block {
            Some(UiBlock::Window {
                title, x, y, w, h, ..
            }) => {
                framebuffer.rectangle(*x, *y, *w, *h, Color::new(0.12, 0.12, 0.16, 0.95));
                framebuffer.rectangle_lines(*x, *y, *w, *h, 1.0, Color::new(0.4, 0.4, 0.5, 1.0));
                framebuffer.rectangle(*x, *y, *w, 24.0, Color::new(0.2, 0.2, 0.26, 1.0));
                font.draw(
                    framebuffer,
                    title,
                    *x + 8.0,
                    *y + 17.0,
                    16.0,
                    0.0,
                    text_color,
                );
                (*x + 8.0, *y + 34.0, *w - 16.0)
            }
            Some(UiBlock::Group { w, .. }) => (8.0, 8.0, *w),
            None => (0.0, 8.0, 200.0),
        };
        draw_headless_children(
            framebuffer,
            &font,
            &widgets,
            x,
            y,
            width,
            text_color,
            box_color,
        );
    });
}

fn execute(
    ui: &mut Ui,
    command: &UiCommand,
    values: &mut HashMap<i64, UiValue>,
    results: &mut HashMap<i64, i64>,
) {
    match command {
        UiCommand::Block(block, commands) => execute_block(ui, block, commands, values, results),
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

#[cfg(test)]
mod tests {
    use super::{group_begin, group_end, label, window_begin};
    use crate::state::with;

    #[test]
    fn nested_ui_blocks_keep_children_in_the_parent_buffer() {
        with(|state| {
            state.headless = true;
            state.ui_stack.clear();
            state.ui_buffer.clear();
        });
        assert_eq!(window_begin(1, "outer", 0.0, 0.0, 100.0, 80.0), 0);
        assert_eq!(group_begin(2, 50.0, 40.0), 0);
        label("inside", None);
        assert_eq!(group_end(), 0);
        with(|state| {
            assert_eq!(state.ui_stack.len(), 1);
            assert!(matches!(
                state.ui_stack[0].1.first(),
                Some(crate::state::UiCommand::Block(crate::state::UiBlock::Group { id: 2, .. }, commands))
                    if matches!(commands.first(), Some(crate::state::UiCommand::Label(text, _)) if text == "inside")
            ));
            state.ui_stack.clear();
            state.ui_buffer.clear();
        });
    }
}
