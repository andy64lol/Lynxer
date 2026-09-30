//! Scenes, text labels, tilemaps, platformer physics, animated sprites, sound
//! and the window-position/screenshot helpers — the parts of the original
//! `game` module that are not drawing primitives, sprites or input.
//!
//! Everything here except drawing, sound and screenshots is pure data, so it
//! works in headless mode (where `run()` executes a fixed number of
//! deterministic frames). The GPU/audio-backed ops return `-1` (or do nothing)
//! when headless, matching `loadSprite`.

use lynxer_abi::{export_float, export_int};
use macroquad::audio::{load_sound, play_sound, set_sound_volume, stop_sound, PlaySoundParams};
use macroquad::color::WHITE;
use macroquad::texture::get_screen_data;
use rodio::{Decoder, Source};
use std::fs::File;
use std::io::BufReader;
use std::path::Path;
use std::time::{Duration, Instant};

use crate::draw::draw_anchored_text;
use crate::sprites::{draw_sprite, load_texture};
use crate::state::{
    color_of_a, with, Animation, PhysicsEngine, Scene, SoundEntry, Sprite, TextLabel, GROUND_SNAP,
};

fn headless() -> bool {
    with(|state| state.headless)
}

/// Runs a future that is ready on its first poll. Macroquad's audio loaders
/// decode synchronously and never yield, so this cannot spin for long.
fn block_on<F: std::future::Future>(future: F) -> F::Output {
    use std::task::{Context, Poll, RawWaker, RawWakerVTable, Waker};
    fn raw() -> RawWaker {
        RawWaker::new(std::ptr::null(), &VTABLE)
    }
    static VTABLE: RawWakerVTable = RawWakerVTable::new(|_| raw(), |_| {}, |_| {}, |_| {});
    let waker = unsafe { Waker::from_raw(raw()) };
    let mut context = Context::from_waker(&waker);
    let mut pinned = Box::pin(future);
    loop {
        match pinned.as_mut().poll(&mut context) {
            Poll::Ready(value) => return value,
            Poll::Pending => std::thread::yield_now(),
        }
    }
}

/// The string values of a JSON array such as `["a.png","b.png"]`.
fn json_string_array(text: &str) -> Vec<String> {
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

fn attribute(tag: &str, name: &str) -> Option<String> {
    let needle = format!("{name}=\"");
    let start = tag.find(&needle)? + needle.len();
    let rest = &tag[start..];
    let end = rest.find('"')?;
    Some(rest[..end].to_string())
}

struct TmxLayer {
    name: String,
    /// Tiles per row, from the layer's `width` attribute.
    width: i64,
    tiles: Vec<i64>,
}

struct Tmx {
    tile_width: f32,
    tile_height: f32,
    first_gid: i64,
    columns: i64,
    margin: f32,
    spacing: f32,
    image_source: Option<String>,
    image_width: Option<f32>,
    image_height: Option<f32>,
    layers: Vec<TmxLayer>,
}

/// A deliberately small Tiled `.tmx` reader: the first `<tileset>` gives the
/// tile size, and each `<layer>` contributes its comma-separated `<data>`.
/// XML is not otherwise validated, and CSV is the only payload format read.
fn parse_tilemap(text: &str) -> Option<Tmx> {
    let tileset_start = text.find("<tileset")?;
    let tileset_end = text[tileset_start..].find('>')? + tileset_start;
    let tileset = &text[tileset_start..tileset_end];
    let tile_width: f32 = attribute(tileset, "tilewidth")?.parse().ok()?;
    let tile_height: f32 = attribute(tileset, "tileheight")?.parse().ok()?;
    if tile_width <= 0.0 || tile_height <= 0.0 {
        return None;
    }
    let first_gid = attribute(tileset, "firstgid")
        .and_then(|value| value.parse::<i64>().ok())
        .filter(|value| *value > 0)
        .unwrap_or(1);
    let columns = attribute(tileset, "columns")
        .and_then(|value| value.parse::<i64>().ok())
        .filter(|value| *value > 0)
        .unwrap_or(0);
    let margin = attribute(tileset, "margin")
        .and_then(|value| value.parse::<f32>().ok())
        .filter(|value| *value >= 0.0)
        .unwrap_or(0.0);
    let spacing = attribute(tileset, "spacing")
        .and_then(|value| value.parse::<f32>().ok())
        .filter(|value| *value >= 0.0)
        .unwrap_or(0.0);
    let tileset_content_start = tileset_end + 1;
    let tileset_content_end = text[tileset_content_start..]
        .find("</tileset>")
        .map(|offset| tileset_content_start + offset)
        .unwrap_or(text.len());
    let image_tag = text[tileset_content_start..tileset_content_end]
        .find("<image")
        .and_then(|offset| {
            let start = tileset_content_start + offset;
            let end = text[start..].find('>')? + start;
            Some(&text[start..end])
        });
    let image_source = image_tag.and_then(|tag| attribute(tag, "source"));
    let image_width = image_tag
        .and_then(|tag| attribute(tag, "width"))
        .and_then(|value| value.parse::<f32>().ok())
        .filter(|value| *value > 0.0);
    let image_height = image_tag
        .and_then(|tag| attribute(tag, "height"))
        .and_then(|value| value.parse::<f32>().ok())
        .filter(|value| *value > 0.0);

    let mut layers = Vec::new();
    let mut cursor = 0usize;
    while let Some(open) = text[cursor..].find("<layer") {
        let open = cursor + open;
        let Some(close) = text[open..].find('>') else {
            break;
        };
        let tag = &text[open..open + close];
        let name = attribute(tag, "name").unwrap_or_default();
        let declared_width = attribute(tag, "width")
            .and_then(|value| value.parse::<i64>().ok())
            .filter(|value| *value > 0)
            .unwrap_or(0);

        let Some(data_open) = text[open..].find("<data") else {
            break;
        };
        let data_open = open + data_open;
        let Some(data_gt) = text[data_open..].find('>') else {
            break;
        };
        let payload_start = data_open + data_gt + 1;
        let Some(payload_end) = text[payload_start..].find("</data>") else {
            break;
        };
        let payload = &text[payload_start..payload_start + payload_end];
        cursor = payload_start + payload_end + "</data>".len();

        let tiles: Vec<i64> = payload
            .split(|character: char| character == ',' || character.is_whitespace())
            .filter(|part| !part.is_empty())
            .filter_map(|part| part.parse::<i64>().ok())
            .collect();
        // A layer without a usable `width` is treated as a single row.
        let width = if declared_width > 0 {
            declared_width
        } else {
            tiles.len().max(1) as i64
        };
        layers.push(TmxLayer { name, width, tiles });
    }
    if layers.is_empty() {
        return None;
    }
    Some(Tmx {
        tile_width,
        tile_height,
        first_gid,
        columns,
        margin,
        spacing,
        image_source,
        image_width,
        image_height,
        layers,
    })
}

/// Map a Tiled global tile ID to its source rectangle in the first tileset
/// atlas. This is pure geometry and can be tested without a graphics context.
fn tile_source_rect(
    tmx: &Tmx,
    gid: i64,
    atlas_width: f32,
    atlas_height: f32,
) -> Option<macroquad::math::Rect> {
    let tile_id = gid.checked_sub(tmx.first_gid)?;
    if tile_id < 0 {
        return None;
    }
    let atlas_width = if atlas_width > 0.0 {
        atlas_width
    } else {
        tmx.image_width?
    };
    let atlas_height = if atlas_height > 0.0 {
        atlas_height
    } else {
        tmx.image_height?
    };
    let columns = if tmx.columns > 0 {
        tmx.columns
    } else {
        let step = tmx.tile_width + tmx.spacing;
        if step <= 0.0 {
            return None;
        }
        ((atlas_width - 2.0 * tmx.margin + tmx.spacing) / step).floor() as i64
    };
    if columns <= 0 {
        return None;
    }
    let x = tmx.margin + (tile_id % columns) as f32 * (tmx.tile_width + tmx.spacing);
    let y = tmx.margin + (tile_id / columns) as f32 * (tmx.tile_height + tmx.spacing);
    if x + tmx.tile_width > atlas_width || y + tmx.tile_height > atlas_height {
        return None;
    }
    Some(macroquad::math::Rect::new(
        x,
        y,
        tmx.tile_width,
        tmx.tile_height,
    ))
}

// --- scenes -----------------------------------------------------------------

export_int!(lynxer_game_make_scene, args, {
    let _ = args;
    with(|state| {
        state.scenes.push(Scene { lists: Vec::new() });
        (state.scenes.len() - 1) as i64
    })
});

export_int!(lynxer_game_add_list_to_scene, args, {
    let scene_index = args.int(0);
    let list_index = args.int(1);
    let name = args.string(0).to_string();
    with(|state| {
        if scene_index < 0
            || list_index < 0
            || (scene_index as usize) >= state.scenes.len()
            || (list_index as usize) >= state.lists.len()
        {
            return -1;
        }
        let scene = &mut state.scenes[scene_index as usize];
        scene.lists.retain(|(existing, _)| *existing != name);
        scene.lists.push((name, list_index));
        0
    })
});

fn scene_lists(state: &crate::state::State, scene_index: i64) -> Option<Vec<i64>> {
    if scene_index < 0 {
        return None;
    }
    state
        .scenes
        .get(scene_index as usize)
        .map(|scene| scene.lists.iter().map(|(_, index)| *index).collect())
}

export_int!(lynxer_game_draw_scene, args, {
    let scene_index = args.int(0);
    with(|state| {
        if state.headless {
            return 0;
        }
        let Some(lists) = scene_lists(state, scene_index) else {
            return -1;
        };
        for list in lists {
            let sprites = state.lists.get(list as usize).cloned().unwrap_or_default();
            for sprite in sprites {
                draw_sprite(state, sprite);
            }
        }
        0
    })
});

export_int!(lynxer_game_update_scene, args, {
    let scene_index = args.int(0);
    with(|state| {
        let dt = state.dt as f32;
        let Some(lists) = scene_lists(state, scene_index) else {
            return -1;
        };
        for list in lists {
            let sprites = state.lists.get(list as usize).cloned().unwrap_or_default();
            for index in sprites {
                if let Some(sprite) = state.sprite_mut(index) {
                    sprite.x += sprite.vx * dt;
                    sprite.y += sprite.vy * dt;
                    sprite.angle += sprite.angular_velocity * dt;
                }
            }
        }
        0
    })
});

// --- text labels ------------------------------------------------------------

export_int!(lynxer_game_make_text_label, args, {
    let label = TextLabel {
        text: args.string(0).to_string(),
        x: args.float(0),
        y: args.float(1),
        color: color_of_a(args.int(2), args.int(3), args.int(4), 255),
        size: args.float(5),
        anchor: args.string(1).to_string(),
    };
    with(|state| {
        state.labels.push(Some(label));
        (state.labels.len() - 1) as i64
    })
});

fn update_label(index: i64, body: impl FnOnce(&mut TextLabel)) -> i64 {
    if index < 0 {
        return -1;
    }
    with(|state| {
        match state
            .labels
            .get_mut(index as usize)
            .and_then(Option::as_mut)
        {
            Some(label) => {
                body(label);
                0
            }
            None => -1,
        }
    })
}

export_int!(lynxer_game_set_text_label, args, {
    let text = args.string(0).to_string();
    update_label(args.int(0), move |label| label.text = text)
});

export_int!(lynxer_game_set_text_label_pos, args, {
    // The leading handle shifts x/y to numeric indices 1 and 2.
    let (x, y) = (args.float(1), args.float(2));
    update_label(args.int(0), move |label| {
        label.x = x;
        label.y = y;
    })
});

export_int!(lynxer_game_set_text_label_color, args, {
    let color = color_of_a(args.int(1), args.int(2), args.int(3), args.int(4));
    update_label(args.int(0), move |label| label.color = color)
});

export_int!(lynxer_game_draw_text_label, args, {
    let index = args.int(0);
    with(|state| {
        if state.headless {
            return 0;
        }
        let Some(Some(label)) = state.labels.get(index as usize) else {
            return -1;
        };
        draw_anchored_text(
            state,
            &label.text,
            label.x,
            label.y,
            label.color,
            label.size,
            &label.anchor,
            "bottom",
        );
        0
    })
});

export_int!(lynxer_game_destroy_text_label, args, {
    let index = args.int(0);
    if index < 0 {
        return -1;
    }
    with(|state| match state.labels.get_mut(index as usize) {
        Some(slot) if slot.is_some() => {
            *slot = None;
            0
        }
        _ => -1,
    })
});

// --- tilemap ----------------------------------------------------------------

export_int!(lynxer_game_load_tilemap, args, {
    let path = args.string(0).to_string();
    let scaling = if args.float(0) > 0.0 {
        args.float(0)
    } else {
        1.0
    };
    let text = match std::fs::read_to_string(&path) {
        Ok(text) => text,
        Err(_) => return -1,
    };
    let Some(tilemap) = parse_tilemap(&text) else {
        return -1;
    };
    let is_headless = headless();
    let atlas = if is_headless {
        None
    } else {
        tilemap.image_source.as_deref().and_then(|source| {
            let map_path = Path::new(&path);
            let base = map_path
                .parent()
                .filter(|parent| !parent.as_os_str().is_empty())
                .unwrap_or_else(|| Path::new("."));
            let image_path = base.join(source);
            image_path.to_str().and_then(load_texture)
        })
    };
    let (texture_index, atlas_width, atlas_height) = match atlas {
        Some(texture) => {
            let width = texture.width();
            let height = texture.height();
            let index = with(|state| {
                state.textures.push(Some(texture));
                (state.textures.len() - 1) as i64
            });
            (index, width, height)
        }
        None => (
            -1,
            tilemap.image_width.unwrap_or(0.0),
            tilemap.image_height.unwrap_or(0.0),
        ),
    };
    with(|state| {
        let mut scene = Scene { lists: Vec::new() };
        // Tiled rows run top-down and the world is bottom-up, so a tile's row
        // index is flipped when placing it.
        for layer in &tilemap.layers {
            let row_width = layer.width.max(1);
            let height_tiles = ((layer.tiles.len() as i64 + row_width - 1) / row_width).max(1);
            let mut list = Vec::new();
            for (position, tile) in layer.tiles.iter().enumerate() {
                if *tile == 0 {
                    continue;
                }
                let column = position as i64 % row_width;
                let row = position as i64 / row_width;
                let x = (column as f32 + 0.5) * tilemap.tile_width * scaling;
                let y = ((height_tiles - row - 1) as f32 + 0.5) * tilemap.tile_height * scaling;
                let shade = 96 + ((*tile * 37) % 96) as i64;
                let source = if texture_index >= 0 {
                    tile_source_rect(&tilemap, *tile, atlas_width, atlas_height)
                } else {
                    None
                };
                let color = if source.is_some() {
                    WHITE
                } else {
                    color_of_a(shade, shade, shade, 255)
                };
                let mut sprite = Sprite::solid(
                    x,
                    y,
                    tilemap.tile_width * scaling,
                    tilemap.tile_height * scaling,
                    color,
                );
                if let Some(source) = source {
                    sprite.texture = texture_index;
                    sprite.source = Some(source);
                }
                state.sprites.push(Some(sprite));
                list.push((state.sprites.len() - 1) as i64);
            }
            state.lists.push(list);
            scene
                .lists
                .push((layer.name.clone(), (state.lists.len() - 1) as i64));
        }
        state.scenes.push(scene);
        (state.scenes.len() - 1) as i64
    })
});

export_int!(lynxer_game_get_tilemap_layer, args, {
    let scene_index = args.int(0);
    let name = args.string(0).to_string();
    with(|state| {
        if scene_index < 0 {
            return -1;
        }
        match state.scenes.get(scene_index as usize) {
            Some(scene) => scene
                .lists
                .iter()
                .find(|(layer, _)| *layer == name)
                .map(|(_, index)| *index)
                .unwrap_or(-1),
            None => -1,
        }
    })
});

// --- physics ----------------------------------------------------------------

export_int!(lynxer_game_make_physics_engine, args, {
    let engine = PhysicsEngine {
        // Numbers are packed per kind, so `gravity` is float(0) and the list
        // indices are int(1) and int(2) — all three read the same `nums` list.
        gravity: args.float(0),
        walls: args.int(1),
        one_way: args.int(2),
        player: -1,
        on_ground: false,
    };
    with(|state| {
        state.engines.push(Some(engine));
        (state.engines.len() - 1) as i64
    })
});

export_int!(lynxer_game_set_physics_player, args, {
    let engine_index = args.int(0);
    let player = args.int(1);
    with(|state| {
        if player < 0 || state.sprite(player).is_none() {
            return -1;
        }
        match state
            .engines
            .get_mut(engine_index as usize)
            .and_then(Option::as_mut)
        {
            Some(engine) => {
                engine.player = player;
                0
            }
            None => -1,
        }
    })
});

// Wall geometry: `(x, y, width, height, angle)`. A non-zero angle marks a
// slope, and a wall in the one-way list is a platform that is only solid from
// above.
type WallBox = (f32, f32, f32, f32, f32);

fn wall_boxes(state: &crate::state::State, list: i64) -> Vec<WallBox> {
    if list < 0 {
        return Vec::new();
    }
    state
        .lists
        .get(list as usize)
        .map(|entries| {
            entries
                .iter()
                .filter_map(|index| state.sprite(*index))
                .map(|wall| (wall.x, wall.y, wall.width(), wall.height(), wall.angle))
                .collect()
        })
        .unwrap_or_default()
}

/// The surface height a slope presents at horizontal position `x`, measured
/// from the box's bottom-left corner and capped at its top.
fn slope_surface(wall: &WallBox, x: f32) -> f32 {
    let (wx, wy, ww, wh, angle) = *wall;
    if ww <= 0.0 {
        return wy + wh / 2.0;
    }
    let left = wx - ww / 2.0;
    let along = ((x - left) / ww).clamp(0.0, 1.0);
    let base = wy - wh / 2.0;
    (base + angle.to_radians().tan() * ww * along).min(wy + wh / 2.0)
}

export_int!(lynxer_game_update_physics, args, {
    let engine_index = args.int(0);
    with(|state| {
        let Some(engine) = state
            .engines
            .get(engine_index as usize)
            .and_then(Option::as_ref)
        else {
            return -1;
        };
        let (gravity, walls, one_way, player) =
            (engine.gravity, engine.walls, engine.one_way, engine.player);
        let Some(player_sprite) = state.sprite(player) else {
            return -1;
        };
        let (mut vx, mut vy, mut x, mut y) = (
            player_sprite.vx,
            player_sprite.vy,
            player_sprite.x,
            player_sprite.y,
        );
        let width = player_sprite.width();
        let height = player_sprite.height();
        let dt = state.dt as f32;

        let solid = wall_boxes(state, walls);
        let platforms = wall_boxes(state, one_way);
        // Where the feet were before this step, which is what tells a one-way
        // platform whether the player came from above.
        let feet_before = y - height / 2.0;

        // --- Horizontal: move, then push out of the side of a solid wall.
        // Slopes never block sideways, and one-way platforms never block
        // horizontally at all.
        x += vx * dt;
        for wall in &solid {
            let (wx, wy, ww, wh, angle) = *wall;
            if angle != 0.0 {
                continue;
            }
            let half_w = (width + ww) / 2.0;
            let half_h = (height + wh) / 2.0;
            if (x - wx).abs() > half_w || (y - wy).abs() > half_h {
                continue;
            }
            if vx > 0.0 {
                x = wx - ww / 2.0 - width / 2.0;
            } else if vx < 0.0 {
                x = wx + ww / 2.0 + width / 2.0;
            }
            vx = 0.0;
        }

        // --- Vertical: gravity, then land, bump a ceiling, or ride a slope.
        vy -= gravity * dt;
        y += vy * dt;

        let mut on_ground = false;
        for wall in &solid {
            let (wx, wy, ww, wh, angle) = *wall;
            if angle != 0.0 {
                continue;
            }
            let half_w = (width + ww) / 2.0;
            let half_h = (height + wh) / 2.0;
            if (x - wx).abs() > half_w || (y - wy).abs() > half_h {
                continue;
            }
            if vy <= 0.0 && y >= wy {
                // Falling onto the top of the wall: rest on it.
                y = wy + wh / 2.0 + height / 2.0;
                vy = 0.0;
                on_ground = true;
            } else if vy > 0.0 && y < wy {
                // Rising into the underside: stop.
                y = wy - wh / 2.0 - height / 2.0;
                vy = 0.0;
            }
        }

        // --- Slopes: land on the ramp surface under the player's centre.
        for wall in &solid {
            let (wx, _, ww, _, angle) = *wall;
            if angle == 0.0 {
                continue;
            }
            if (x - wx).abs() > (width + ww) / 2.0 {
                continue;
            }
            let surface = slope_surface(wall, x);
            if vy <= 0.0 && y - height / 2.0 <= surface && feet_before >= surface - GROUND_SNAP {
                y = surface + height / 2.0;
                vy = 0.0;
                on_ground = true;
            }
        }

        // --- One-way platforms: solid only for a player falling from above.
        for wall in &platforms {
            let (wx, wy, ww, wh, _) = *wall;
            let half_w = (width + ww) / 2.0;
            let half_h = (height + wh) / 2.0;
            if (x - wx).abs() > half_w || (y - wy).abs() > half_h {
                continue;
            }
            let top = wy + wh / 2.0;
            if vy <= 0.0 && feet_before >= top - GROUND_SNAP {
                y = top + height / 2.0;
                vy = 0.0;
                on_ground = true;
            }
        }

        if let Some(sprite) = state.sprite_mut(player) {
            sprite.x = x;
            sprite.y = y;
            sprite.vx = vx;
            sprite.vy = vy;
        }
        if let Some(Some(engine)) = state.engines.get_mut(engine_index as usize) {
            engine.on_ground = on_ground;
        }
        0
    })
});

export_int!(lynxer_game_can_jump, args, {
    let engine_index = args.int(0);
    with(|state| {
        match state
            .engines
            .get(engine_index as usize)
            .and_then(Option::as_ref)
        {
            Some(engine) => engine.on_ground as i64,
            None => 0,
        }
    })
});

export_int!(lynxer_game_jump_player, args, {
    let engine_index = args.int(0);
    let jump_speed = args.float(1);
    with(|state| {
        let Some(engine) = state
            .engines
            .get(engine_index as usize)
            .and_then(Option::as_ref)
        else {
            return -1;
        };
        if !engine.on_ground {
            return 0;
        }
        let player = engine.player;
        if let Some(sprite) = state.sprite_mut(player) {
            sprite.vy = jump_speed;
        }
        if let Some(Some(engine)) = state.engines.get_mut(engine_index as usize) {
            engine.on_ground = false;
        }
        0
    })
});

export_float!(lynxer_game_get_player_vy, args, {
    let engine_index = args.int(0);
    with(|state| {
        let player = state
            .engines
            .get(engine_index as usize)
            .and_then(Option::as_ref)
            .map(|engine| engine.player)
            .unwrap_or(-1);
        state
            .sprite(player)
            .map(|sprite| sprite.vy as f64)
            .unwrap_or(0.0)
    })
});

// --- animated sprites -------------------------------------------------------

export_int!(lynxer_game_make_animated_sprite, args, {
    let paths = json_string_array(args.string(0));
    let fps = args.float(0);
    let (x, y) = (args.float(1), args.float(2));
    if paths.is_empty() || fps <= 0.0 {
        return -1;
    }
    if with(|state| state.headless) {
        return -1;
    }
    with(|state| {
        let mut textures = Vec::with_capacity(paths.len());
        for path in &paths {
            match load_texture(path) {
                Some(texture) => {
                    state.textures.push(Some(texture));
                    textures.push((state.textures.len() - 1) as i64);
                }
                None => return -1,
            }
        }
        let first = textures[0];
        let (width, height) = state
            .textures
            .get(first as usize)
            .and_then(|slot| slot.as_ref())
            .map(|texture| (texture.width(), texture.height()))
            .unwrap_or((1.0, 1.0));
        let mut sprite = Sprite::solid(x, y, width, height, WHITE);
        sprite.solid = false;
        sprite.texture = first;
        state.sprites.push(Some(sprite));
        let index = (state.sprites.len() - 1) as i64;
        state.animations.insert(
            index,
            Animation {
                textures,
                fps,
                elapsed: 0.0,
                frame: 0,
            },
        );
        index
    })
});

export_int!(lynxer_game_update_animation, args, {
    let index = args.int(0);
    let dt = args.float(1);
    with(|state| {
        let Some(animation) = state.animations.get_mut(&index) else {
            return -1;
        };
        if animation.textures.len() < 2 || animation.fps <= 0.0 {
            return 0;
        }
        let step = 1.0 / animation.fps;
        animation.elapsed += dt.max(0.0);
        while animation.elapsed >= step {
            animation.elapsed -= step;
            animation.frame = (animation.frame + 1) % animation.textures.len();
        }
        let texture = animation.textures[animation.frame];
        if let Some(sprite) = state.sprite_mut(index) {
            sprite.texture = texture;
        }
        0
    })
});

// --- sound ------------------------------------------------------------------

/// Probe media duration without opening an audio output device. Rodio's decoder
/// uses Symphonia for the supported compressed formats, and falls back to
/// counting decoded samples if the container has no duration metadata.
fn probe_sound_duration(path: &str) -> Option<Duration> {
    let file = File::open(Path::new(path)).ok()?;
    let decoder = Decoder::new(BufReader::new(file)).ok()?;
    if let Some(duration) = decoder.total_duration() {
        return Some(duration);
    }
    let sample_rate = decoder.sample_rate() as u64;
    let channels = decoder.channels() as u64;
    if sample_rate == 0 || channels == 0 {
        return None;
    }
    let sample_count = decoder.count() as u64;
    Some(Duration::from_secs_f64(
        sample_count as f64 / (sample_rate * channels) as f64,
    ))
}

fn playback_finished(
    started_at: Option<Instant>,
    duration: Option<Duration>,
    looping: bool,
    now: Instant,
) -> bool {
    if looping {
        return false;
    }
    match (started_at, duration) {
        (Some(started_at), Some(duration)) => now.saturating_duration_since(started_at) >= duration,
        _ => false,
    }
}

export_int!(lynxer_game_load_sound, args, {
    let path = args.string(0).to_string();
    if with(|state| state.headless) {
        return -1;
    }
    let duration = probe_sound_duration(&path);
    match block_on(load_sound(&path)) {
        Ok(sound) => with(|state| {
            state.sounds.push(Some(SoundEntry {
                sound,
                volume: 1.0,
                playing: false,
                looping: false,
                started_at: None,
                duration,
            }));
            (state.sounds.len() - 1) as i64
        }),
        Err(_) => -1,
    }
});

fn update_sound(index: i64, body: impl FnOnce(&mut SoundEntry)) -> i64 {
    if index < 0 {
        return -1;
    }
    with(|state| {
        match state
            .sounds
            .get_mut(index as usize)
            .and_then(Option::as_mut)
        {
            Some(entry) => {
                body(entry);
                0
            }
            None => -1,
        }
    })
}

export_int!(lynxer_game_play_sound, args, {
    update_sound(args.int(0), |entry| {
        play_sound(
            &entry.sound,
            PlaySoundParams {
                looped: false,
                volume: entry.volume,
            },
        );
        entry.playing = true;
        entry.looping = false;
        entry.started_at = Some(Instant::now());
    })
});

export_int!(lynxer_game_loop_sound, args, {
    update_sound(args.int(0), |entry| {
        play_sound(
            &entry.sound,
            PlaySoundParams {
                looped: true,
                volume: entry.volume,
            },
        );
        entry.playing = true;
        entry.looping = true;
        entry.started_at = None;
    })
});

export_int!(lynxer_game_stop_sound, args, {
    update_sound(args.int(0), |entry| {
        stop_sound(&entry.sound);
        entry.playing = false;
        entry.looping = false;
        entry.started_at = None;
    })
});

export_int!(lynxer_game_set_sound_volume, args, {
    let volume = args.float(1).clamp(0.0, 1.0);
    update_sound(args.int(0), move |entry| {
        entry.volume = volume;
        set_sound_volume(&entry.sound, volume);
    })
});

export_int!(lynxer_game_is_sound_playing, args, {
    let index = args.int(0);
    if index < 0 {
        return 0;
    }
    with(|state| {
        match state
            .sounds
            .get_mut(index as usize)
            .and_then(Option::as_mut)
        {
            Some(entry) => {
                if entry.playing
                    && playback_finished(
                        entry.started_at,
                        entry.duration,
                        entry.looping,
                        Instant::now(),
                    )
                {
                    entry.playing = false;
                }
                entry.playing as i64
            }
            None => 0,
        }
    })
});

// --- window -----------------------------------------------------------------

export_int!(lynxer_game_set_window_pos, args, {
    let (x, y) = (args.int(0).max(0) as u32, args.int(1).max(0) as u32);
    if !headless() {
        macroquad::miniquad::window::set_window_position(x, y);
    }
    0
});

export_int!(lynxer_game_screenshot, args, {
    let path = args.string(0).to_string();
    if headless() {
        return -1;
    }
    get_screen_data().export_png(&path);
    0
});

#[cfg(test)]
mod tests {
    use super::{parse_tilemap, playback_finished, probe_sound_duration, tile_source_rect};
    use std::fs;
    use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

    #[test]
    fn tilemap_atlas_coordinates_respect_first_gid_margin_and_spacing() {
        let text = r#"<map><tileset firstgid="5" tilewidth="16" tileheight="8" columns="3" margin="1" spacing="2"><image source="tiles.png" width="55" height="21"/></tileset><layer name="ground" width="4" height="1"><data encoding="csv">5,6,7,8</data></layer></map>"#;
        let tilemap = parse_tilemap(text).expect("valid inline tileset and layer");

        let first = tile_source_rect(&tilemap, 5, 55.0, 21.0).unwrap();
        assert_eq!((first.x, first.y, first.w, first.h), (1.0, 1.0, 16.0, 8.0));

        let next_row = tile_source_rect(&tilemap, 8, 55.0, 21.0).unwrap();
        assert_eq!(
            (next_row.x, next_row.y, next_row.w, next_row.h),
            (1.0, 11.0, 16.0, 8.0)
        );
        assert!(tile_source_rect(&tilemap, 4, 55.0, 21.0).is_none());
        assert!(tile_source_rect(&tilemap, 11, 55.0, 21.0).is_none());
    }

    #[test]
    fn one_shot_playback_expires_but_loops_do_not() {
        let start = Instant::now();
        let duration = Some(Duration::from_secs(2));
        assert!(!playback_finished(
            Some(start),
            duration,
            false,
            start + Duration::from_secs(1)
        ));
        assert!(playback_finished(
            Some(start),
            duration,
            false,
            start + Duration::from_secs(2)
        ));
        assert!(!playback_finished(
            Some(start),
            duration,
            true,
            start + Duration::from_secs(10)
        ));
    }

    #[test]
    fn probes_wav_duration_without_an_audio_device() {
        let sample_rate = 8_000u32;
        let sample_frames = 1_600u32;
        let data_bytes = sample_frames * 2;
        let mut wav = Vec::with_capacity(44 + data_bytes as usize);
        wav.extend_from_slice(b"RIFF");
        wav.extend_from_slice(&(36 + data_bytes).to_le_bytes());
        wav.extend_from_slice(b"WAVEfmt ");
        wav.extend_from_slice(&16u32.to_le_bytes());
        wav.extend_from_slice(&1u16.to_le_bytes());
        wav.extend_from_slice(&1u16.to_le_bytes());
        wav.extend_from_slice(&sample_rate.to_le_bytes());
        wav.extend_from_slice(&(sample_rate * 2).to_le_bytes());
        wav.extend_from_slice(&2u16.to_le_bytes());
        wav.extend_from_slice(&16u16.to_le_bytes());
        wav.extend_from_slice(b"data");
        wav.extend_from_slice(&data_bytes.to_le_bytes());
        wav.resize(44 + data_bytes as usize, 0);

        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let path = std::env::temp_dir().join(format!("lynxer-game-{nonce}.wav"));
        fs::write(&path, wav).unwrap();
        let duration = probe_sound_duration(path.to_str().unwrap());
        fs::remove_file(path).unwrap();

        assert_eq!(duration, Some(Duration::from_millis(200)));
    }
}
