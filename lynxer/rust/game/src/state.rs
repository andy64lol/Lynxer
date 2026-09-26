//! Global window/game state.
//!
//! All interpreter calls arrive on the same thread that runs the macroquad
//! loop, so a `thread_local` cell is the simplest sound container. Each op
//! borrows the state exactly once.

use std::cell::RefCell;
use std::collections::HashMap;

use macroquad::audio::Sound;
use macroquad::color::{Color, BLACK};
use macroquad::texture::Texture2D;

/// Number of frames a headless `run()` executes. Enough for callbacks to be
/// exercised deterministically without a display.
pub const HEADLESS_FRAMES: u32 = 3;
pub const HEADLESS_DT: f64 = 1.0 / 60.0;

#[derive(Clone, Copy)]
pub struct Camera {
    pub x: f32,
    pub y: f32,
    pub zoom: f32,
}

impl Camera {
    pub fn new() -> Self {
        Camera {
            x: 0.0,
            y: 0.0,
            zoom: 1.0,
        }
    }
}

/// A scene: named sprite lists that are drawn and updated together.
pub struct Scene {
    pub lists: Vec<(String, i64)>,
}

/// A HUD text label. `size` is a font size and `anchor` is `left` / `center` /
/// `right`, matching the drawing helper.
pub struct TextLabel {
    pub text: String,
    pub x: f32,
    pub y: f32,
    pub color: Color,
    pub size: f32,
    pub anchor: String,
}

/// A platformer physics engine over one player sprite and an optional wall
/// list. Ground state is recomputed every step.
pub struct PhysicsEngine {
    pub gravity: f32,
    pub walls: i64,
    pub player: i64,
    pub on_ground: bool,
}

/// A loaded sound plus the module's own playback state. A finished one-shot is
/// not detected, so `isSoundPlaying` reports what the module was last told.
pub struct SoundEntry {
    pub sound: Sound,
    pub volume: f32,
    pub playing: bool,
}

/// An animated sprite: the texture indices it cycles through, its rate and the
/// time spent on the current frame.
pub struct Animation {
    pub textures: Vec<i64>,
    pub fps: f32,
    pub elapsed: f32,
    pub frame: usize,
}

/// A sprite. Solid sprites carry an explicit rectangle; textured sprites take
/// their size from the texture and keep `w`/`h` updated for collisions.
#[derive(Clone, Copy)]
pub struct Sprite {
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
    pub angle: f32,
    pub angular_velocity: f32,
    pub scale: f32,
    pub vx: f32,
    pub vy: f32,
    pub alpha: u8,
    pub color: Color,
    pub visible: bool,
    pub texture: i64,
    pub solid: bool,
    pub flip_x: bool,
    pub flip_y: bool,
}

impl Sprite {
    pub fn solid(x: f32, y: f32, w: f32, h: f32, color: Color) -> Self {
        Sprite {
            x,
            y,
            w,
            h,
            angle: 0.0,
            angular_velocity: 0.0,
            scale: 1.0,
            vx: 0.0,
            vy: 0.0,
            alpha: 255,
            color,
            visible: true,
            texture: -1,
            solid: true,
            flip_x: false,
            flip_y: false,
        }
    }

    pub fn width(&self) -> f32 {
        self.w * self.scale
    }

    pub fn height(&self) -> f32 {
        self.h * self.scale
    }

    pub fn tint(&self) -> Color {
        Color {
            r: self.color.r,
            g: self.color.g,
            b: self.color.b,
            a: self.color.a * (self.alpha as f32 / 255.0),
        }
    }
}

pub struct State {
    pub title: String,
    pub width: f32,
    pub height: f32,
    pub background: Color,
    pub initialized: bool,
    pub running: bool,
    pub quit_requested: bool,
    pub headless: bool,
    pub fps_cap: f32,
    pub dt: f64,
    pub sim_time: f64,
    pub sprites: Vec<Option<Sprite>>,
    pub lists: Vec<Vec<i64>>,
    pub textures: Vec<Option<Texture2D>>,
    pub cameras: Vec<Camera>,
    pub active_camera: Option<usize>,
    pub scenes: Vec<Scene>,
    pub labels: Vec<Option<TextLabel>>,
    pub engines: Vec<Option<PhysicsEngine>>,
    pub sounds: Vec<Option<SoundEntry>>,
    pub animations: HashMap<i64, Animation>,
    pub mouse_query_x: f32,
    pub mouse_query_y: f32,
    pub scroll_x: f32,
    pub scroll_y: f32,
    /// Lynxer function names registered with setDrawCallback/setUpdateCallback.
    pub draw_callback: String,
    pub update_callback: String,
}

impl State {
    fn new() -> Self {
        State {
            title: "Lynxer".to_string(),
            width: 800.0,
            height: 600.0,
            background: BLACK,
            initialized: false,
            running: false,
            quit_requested: false,
            headless: headless_requested(),
            fps_cap: 0.0,
            dt: HEADLESS_DT,
            sim_time: 0.0,
            sprites: Vec::new(),
            lists: Vec::new(),
            textures: Vec::new(),
            cameras: Vec::new(),
            active_camera: None,
            scenes: Vec::new(),
            labels: Vec::new(),
            engines: Vec::new(),
            sounds: Vec::new(),
            animations: HashMap::new(),
            mouse_query_x: 0.0,
            mouse_query_y: 0.0,
            scroll_x: 0.0,
            scroll_y: 0.0,
            draw_callback: String::new(),
            update_callback: String::new(),
        }
    }

    /// Resets per-run registries. Called by `init` so repeated runs in one
    /// process start clean, mirroring the reference implementation.
    pub fn reset(&mut self) {
        self.sprites.clear();
        self.lists.clear();
        self.textures.clear();
        self.cameras.clear();
        self.active_camera = None;
        self.scenes.clear();
        self.labels.clear();
        self.engines.clear();
        self.sounds.clear();
        self.animations.clear();
        self.quit_requested = false;
        self.sim_time = 0.0;
    }

    pub fn camera(&self) -> Option<&Camera> {
        self.active_camera.and_then(|index| self.cameras.get(index))
    }

    /// World (bottom-left, +Y up) to macroquad screen (top-left, +Y down),
    /// applying the active camera.
    pub fn screen_pos(&self, x: f32, y: f32) -> (f32, f32) {
        let (world_x, world_y) = match self.camera() {
            Some(camera) => (
                (x - camera.x) * camera.zoom + self.width / 2.0,
                (y - camera.y) * camera.zoom + self.height / 2.0,
            ),
            None => (x, y),
        };
        (world_x, self.height - world_y)
    }

    pub fn screen_scale(&self) -> f32 {
        self.camera().map(|camera| camera.zoom).unwrap_or(1.0)
    }

    pub fn sprite(&self, index: i64) -> Option<&Sprite> {
        if index < 0 {
            return None;
        }
        self.sprites.get(index as usize).and_then(|s| s.as_ref())
    }

    pub fn sprite_mut(&mut self, index: i64) -> Option<&mut Sprite> {
        if index < 0 {
            return None;
        }
        self.sprites
            .get_mut(index as usize)
            .and_then(|s| s.as_mut())
    }
}

thread_local! {
    static STATE: RefCell<State> = RefCell::new(State::new());
}

pub fn with<R>(body: impl FnOnce(&mut State) -> R) -> R {
    STATE.with(|cell| body(&mut cell.borrow_mut()))
}

pub fn headless_requested() -> bool {
    match std::env::var("LYNXER_GAME_HEADLESS") {
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

/// Parses a flat coordinate list ("1,2 3,4" or JSON-ish "[1,2,3,4]") into
/// numbers. Any character that cannot continue a number separates tokens.
pub fn parse_coords(text: &str) -> Vec<f32> {
    let mut values = Vec::new();
    let mut current = String::new();
    for character in text.chars() {
        let is_digit = character.is_ascii_digit();
        let is_sign = character == '-' || character == '+';
        let is_dot = character == '.';
        if current.is_empty() {
            if is_digit || is_sign || is_dot {
                current.push(character);
            }
        } else if is_digit || is_dot {
            current.push(character);
        } else {
            if let Ok(value) = current.parse::<f32>() {
                values.push(value);
            }
            current.clear();
            if is_sign || is_dot {
                current.push(character);
            }
        }
    }
    if !current.is_empty() {
        if let Ok(value) = current.parse::<f32>() {
            values.push(value);
        }
    }
    values
}
