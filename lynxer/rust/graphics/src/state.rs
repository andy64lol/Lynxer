//! Global state for the Lynxer `graphics` module.
//!
//! Every op arrives on the thread that runs the macroquad loop, so a
//! `thread_local` cell is the simplest sound container. Each op borrows the
//! state exactly once.
//!
//! Handles (`textures`, `images`, `fonts`, `materials`, `targets`) are indices
//! into these vectors; `-1` means "not available", which is also what every
//! load returns in headless mode.

use std::cell::RefCell;
use std::collections::HashMap;

use macroquad::color::{Color, BLACK};
use macroquad::material::Material;
use macroquad::text::Font;
use macroquad::texture::{Image, RenderTarget, Texture2D};

/// Frames a headless `run()` executes: enough for the callbacks to run
/// deterministically without a display.
pub const HEADLESS_FRAMES: u32 = 3;
pub const HEADLESS_DT: f64 = 1.0 / 60.0;

/// The 2D camera the module last set, kept so `screenToWorld`/`worldToScreen`
/// can be answered without a macroquad context.
#[derive(Clone, Copy)]
pub struct CameraState {
    pub target_x: f32,
    pub target_y: f32,
    pub zoom: f32,
    pub rotation: f32,
    pub offset_x: f32,
    pub offset_y: f32,
}

impl CameraState {
    fn new() -> Self {
        CameraState {
            target_x: 0.0,
            target_y: 0.0,
            zoom: 1.0,
            rotation: 0.0,
            offset_x: 0.0,
            offset_y: 0.0,
        }
    }
}

/// A widget value that survives across frames, keyed by the id the Lynxer
/// program passes to the `ui*` ops. macroquad keeps its own copy inside the
/// `Ui`; this is the copy Lynxer can read and write.
#[derive(Clone)]
pub enum UiValue {
    Bool(bool),
    Float(f32),
    Int(i64),
    Text(String),
}

/// One widget issued between `uiWindowBegin`/`uiGroupBegin` and its `*End`.
/// macroquad's window and group take a closure, so the flat ops buffer the
/// block here and replay it inside that closure.
pub enum UiCommand {
    Label(String, Option<(f32, f32)>),
    Button {
        id: i64,
        text: String,
        position: Option<(f32, f32)>,
    },
    Checkbox {
        id: i64,
        label: String,
    },
    Slider {
        id: i64,
        label: String,
        min: f32,
        max: f32,
    },
    InputText {
        id: i64,
        label: String,
        password: bool,
    },
    ProgressBar {
        label: String,
        value: f32,
        min: f32,
        max: f32,
    },
    ComboBox {
        id: i64,
        label: String,
        options: Vec<String>,
    },
    Separator,
    SameLine(f32),
}

/// The open `uiWindowBegin`/`uiGroupBegin` block, if any.
pub enum UiBlock {
    Window {
        id: i64,
        title: String,
        x: f32,
        y: f32,
        w: f32,
        h: f32,
    },
    Group {
        id: i64,
        w: f32,
        h: f32,
    },
}

pub struct State {
    pub title: String,
    pub width: f32,
    pub height: f32,
    pub background: Color,
    pub resizable: bool,
    pub fullscreen: bool,
    pub high_dpi: bool,
    pub sample_count: i32,
    pub nearest_filter: bool,
    pub fps_cap: f32,
    pub initialized: bool,
    pub running: bool,
    pub quit_requested: bool,
    pub headless: bool,
    pub dt: f64,
    pub sim_time: f64,
    pub frames: u64,
    pub start_callback: String,
    pub update_callback: String,
    pub draw_callback: String,
    pub textures: Vec<Option<Texture2D>>,
    pub images: Vec<Option<Image>>,
    pub fonts: Vec<Font>,
    pub materials: Vec<Option<Material>>,
    pub targets: Vec<Option<RenderTarget>>,
    pub camera2d: CameraState,
    pub ui_values: HashMap<i64, UiValue>,
    pub ui_results: HashMap<i64, i64>,
    /// The options of the last `uiComboBox` call per id, so the selected text
    /// can be resolved without the caller re-supplying the list.
    pub ui_options: HashMap<i64, Vec<String>>,
    pub ui_block: Option<UiBlock>,
    pub ui_buffer: Vec<UiCommand>,
    pub mouse_x: f32,
    pub mouse_y: f32,
    pub scroll_x: f32,
    pub scroll_y: f32,
}

impl State {
    fn new() -> Self {
        State {
            title: "Lynxer".to_string(),
            width: 800.0,
            height: 600.0,
            background: BLACK,
            resizable: true,
            fullscreen: false,
            high_dpi: false,
            sample_count: 1,
            nearest_filter: false,
            fps_cap: 0.0,
            initialized: false,
            running: false,
            quit_requested: false,
            headless: headless_requested(),
            dt: HEADLESS_DT,
            sim_time: 0.0,
            frames: 0,
            start_callback: String::new(),
            update_callback: String::new(),
            draw_callback: String::new(),
            textures: Vec::new(),
            images: Vec::new(),
            fonts: Vec::new(),
            materials: Vec::new(),
            targets: Vec::new(),
            camera2d: CameraState::new(),
            ui_values: HashMap::new(),
            ui_results: HashMap::new(),
            ui_options: HashMap::new(),
            ui_block: None,
            ui_buffer: Vec::new(),
            mouse_x: 0.0,
            mouse_y: 0.0,
            scroll_x: 0.0,
            scroll_y: 0.0,
        }
    }

    /// Clears per-run registries so a second `init` in the same process starts
    /// clean.
    pub fn reset(&mut self) {
        self.textures.clear();
        self.images.clear();
        self.fonts.clear();
        self.materials.clear();
        self.targets.clear();
        self.camera2d = CameraState::new();
        self.ui_values.clear();
        self.ui_results.clear();
        self.ui_options.clear();
        self.ui_block = None;
        self.ui_buffer.clear();
        self.quit_requested = false;
        self.sim_time = 0.0;
        self.frames = 0;
    }

    pub fn texture(&self, handle: i64) -> Option<&Texture2D> {
        if handle < 0 {
            return None;
        }
        self.textures.get(handle as usize).and_then(|t| t.as_ref())
    }

    pub fn image(&self, handle: i64) -> Option<&Image> {
        if handle < 0 {
            return None;
        }
        self.images.get(handle as usize).and_then(|i| i.as_ref())
    }

    pub fn image_mut(&mut self, handle: i64) -> Option<&mut Image> {
        if handle < 0 {
            return None;
        }
        self.images
            .get_mut(handle as usize)
            .and_then(|i| i.as_mut())
    }

    pub fn font(&self, handle: i64) -> Option<&Font> {
        if handle < 0 {
            return None;
        }
        self.fonts.get(handle as usize)
    }

    pub fn target(&self, handle: i64) -> Option<&RenderTarget> {
        if handle < 0 {
            return None;
        }
        self.targets.get(handle as usize).and_then(|t| t.as_ref())
    }

    pub fn material(&self, handle: i64) -> Option<&Material> {
        if handle < 0 {
            return None;
        }
        self.materials.get(handle as usize).and_then(|m| m.as_ref())
    }

    pub fn ui_bool(&self, id: i64, fallback: bool) -> bool {
        match self.ui_values.get(&id) {
            Some(UiValue::Bool(value)) => *value,
            _ => fallback,
        }
    }

    pub fn ui_float(&self, id: i64, fallback: f32) -> f32 {
        match self.ui_values.get(&id) {
            Some(UiValue::Float(value)) => *value,
            _ => fallback,
        }
    }

    pub fn ui_text(&self, id: i64) -> String {
        match self.ui_values.get(&id) {
            Some(UiValue::Text(value)) => value.clone(),
            _ => String::new(),
        }
    }

    pub fn ui_result(&self, id: i64) -> i64 {
        self.ui_results.get(&id).copied().unwrap_or(0)
    }

    pub fn ui_int(&self, id: i64, fallback: i64) -> i64 {
        match self.ui_values.get(&id) {
            Some(UiValue::Int(value)) => *value,
            _ => fallback,
        }
    }

    /// The selected text of the last `uiComboBox` call for `id`.
    pub fn ui_selected_text(&self, id: i64) -> String {
        let index = self.ui_int(id, 0).max(0) as usize;
        self.ui_options
            .get(&id)
            .and_then(|options| options.get(index))
            .cloned()
            .unwrap_or_default()
    }

    /// True while a window/group block is open, so widget ops buffer instead of
    /// drawing.
    pub fn buffering(&self) -> bool {
        self.ui_block.is_some()
    }
}

thread_local! {
    static STATE: RefCell<State> = RefCell::new(State::new());
}

pub fn with<R>(body: impl FnOnce(&mut State) -> R) -> R {
    STATE.with(|cell| body(&mut cell.borrow_mut()))
}

pub fn headless_requested() -> bool {
    match std::env::var("LYNXER_GRAPHICS_HEADLESS") {
        Ok(value) => !value.is_empty() && value != "0",
        Err(_) => false,
    }
}

pub fn clamp_channel(value: f64) -> u8 {
    value.round().clamp(0.0, 255.0) as u8
}

pub fn color_of(r: i64, g: i64, b: i64) -> Color {
    Color::from_rgba(
        clamp_channel(r as f64),
        clamp_channel(g as f64),
        clamp_channel(b as f64),
        255,
    )
}

pub fn color_of_a(r: i64, g: i64, b: i64, a: i64) -> Color {
    Color::from_rgba(
        clamp_channel(r as f64),
        clamp_channel(g as f64),
        clamp_channel(b as f64),
        clamp_channel(a as f64),
    )
}

/// Renders a float the way the docs and golden files expect: no trailing `.0`
/// for integral values.
pub fn number(value: f32) -> String {
    if !value.is_finite() {
        return "0".to_string();
    }
    if (value - value.round()).abs() < f32::EPSILON {
        format!("{}", value.round() as i64)
    } else {
        let mut text = format!("{value}");
        if text.contains('.') {
            while text.ends_with('0') {
                text.pop();
            }
            if text.ends_with('.') {
                text.pop();
            }
        }
        text
    }
}
