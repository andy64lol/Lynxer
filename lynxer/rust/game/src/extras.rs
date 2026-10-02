//! Scenes, text labels, tilemaps, platformer physics, animated sprites, sound
//! and the window-position/screenshot helpers — the parts of the original
//! `game` module that are not drawing primitives, sprites or input.
//!
//! Everything here except drawing, sound and screenshots is pure data, so it
//! works in headless mode (where `run()` executes a fixed number of
//! deterministic frames). The GPU/audio-backed ops return `-1` (or do nothing)
//! when headless, matching `loadSprite`.

use lynxer_abi::{export_float, export_int};
use macroquad::color::WHITE;
use macroquad::texture::get_screen_data;
use std::path::Path;

use crate::draw::draw_anchored_text;
use crate::sprites::{draw_sprite, load_texture};
use crate::state::{
    color_of_a, with, Animation, PhysicsEngine, PlatformPose, Scene, SoundEntry, Sprite, TextLabel,
    GROUND_SNAP,
};
use rodio::{Decoder, OutputStream, Sink, Source};
use std::fs::File;
use std::io::BufReader;

fn headless() -> bool {
    with(|state| state.headless)
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
    tiles: Vec<u32>,
}

struct TmxTileset {
    first_gid: u32,
    tile_width: f32,
    tile_height: f32,
    columns: i64,
    tile_count: u32,
    margin: f32,
    spacing: f32,
    image_source: Option<String>,
    image_width: Option<f32>,
    image_height: Option<f32>,
}

struct Tmx {
    tile_width: f32,
    tile_height: f32,
    tilesets: Vec<TmxTileset>,
    layers: Vec<TmxLayer>,
}

fn parse_tileset(text: &str, first_gid: u32) -> Option<TmxTileset> {
    let start = text.find("<tileset")?;
    let tag_end = text[start..].find('>')? + start;
    let tag = &text[start..=tag_end];
    let tile_width = attribute(tag, "tilewidth")
        .and_then(|value| value.parse::<f32>().ok())
        .unwrap_or(0.0);
    let tile_height = attribute(tag, "tileheight")
        .and_then(|value| value.parse::<f32>().ok())
        .unwrap_or(0.0);
    if tile_width <= 0.0 || tile_height <= 0.0 {
        return None;
    }
    let content_start = tag_end + 1;
    let content_end = text[content_start..]
        .find("</tileset>")
        .map(|offset| content_start + offset)
        .unwrap_or(text.len());
    let content = &text[content_start..content_end];
    let image_tag = content.find("<image").and_then(|offset| {
        let start = offset + content[offset..].find("<image")?;
        let end = content[start..].find('>')? + start;
        Some(&content[start..=end])
    });
    Some(TmxTileset {
        first_gid,
        tile_width,
        tile_height,
        columns: attribute(tag, "columns")
            .and_then(|value| value.parse::<i64>().ok())
            .filter(|value| *value > 0)
            .unwrap_or(0),
        tile_count: attribute(tag, "tilecount")
            .and_then(|value| value.parse::<u32>().ok())
            .unwrap_or(0),
        margin: attribute(tag, "margin")
            .and_then(|value| value.parse::<f32>().ok())
            .filter(|value| *value >= 0.0)
            .unwrap_or(0.0),
        spacing: attribute(tag, "spacing")
            .and_then(|value| value.parse::<f32>().ok())
            .filter(|value| *value >= 0.0)
            .unwrap_or(0.0),
        image_source: image_tag.and_then(|tag| attribute(tag, "source")),
        image_width: image_tag
            .and_then(|tag| attribute(tag, "width"))
            .and_then(|value| value.parse::<f32>().ok())
            .filter(|value| *value > 0.0),
        image_height: image_tag
            .and_then(|tag| attribute(tag, "height"))
            .and_then(|value| value.parse::<f32>().ok())
            .filter(|value| *value > 0.0),
    })
}

fn decode_layer_data(data_tag: &str, payload: &str) -> Option<Vec<u32>> {
    let encoding = attribute(data_tag, "encoding");
    if encoding.as_deref() == Some("csv") || encoding.is_none() {
        if encoding.is_none() {
            let mut tiles = Vec::new();
            let mut cursor = 0;
            while let Some(offset) = payload[cursor..].find("<tile") {
                let start = cursor + offset;
                let end = payload[start..].find('>')? + start;
                let tag = &payload[start..=end];
                if let Some(gid) = attribute(tag, "gid").and_then(|value| value.parse().ok()) {
                    tiles.push(gid);
                }
                cursor = end + 1;
            }
            return Some(tiles);
        }
        return Some(
            payload
                .split(|character: char| character == ',' || character.is_whitespace())
                .filter(|part| !part.is_empty())
                .map(str::parse::<u32>)
                .collect::<Result<Vec<_>, _>>()
                .ok()?,
        );
    }
    if encoding.as_deref() != Some("base64") {
        return None;
    }
    use base64::Engine;
    use std::io::Read;
    let encoded = base64::engine::general_purpose::STANDARD
        .decode(payload.split_whitespace().collect::<String>())
        .ok()?;
    let decoded = match attribute(data_tag, "compression").as_deref() {
        None => encoded,
        Some("gzip") => {
            let mut bytes = Vec::new();
            flate2::read::GzDecoder::new(encoded.as_slice())
                .read_to_end(&mut bytes)
                .ok()?;
            bytes
        }
        Some("zlib") => {
            let mut bytes = Vec::new();
            flate2::read::ZlibDecoder::new(encoded.as_slice())
                .read_to_end(&mut bytes)
                .ok()?;
            bytes
        }
        Some(_) => return None,
    };
    if decoded.len() % 4 != 0 {
        return None;
    }
    Some(
        decoded
            .chunks_exact(4)
            .map(|chunk| u32::from_le_bytes([chunk[0], chunk[1], chunk[2], chunk[3]]))
            .collect(),
    )
}

fn expand_external_tilesets(text: &str, map_path: &Path) -> Option<String> {
    let base = map_path
        .parent()
        .filter(|path| !path.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    let mut expanded = String::with_capacity(text.len());
    let mut cursor = 0;
    while let Some(offset) = text[cursor..].find("<tileset") {
        let start = cursor + offset;
        expanded.push_str(&text[cursor..start]);
        let end = text[start..].find('>')? + start;
        let tag = &text[start..=end];
        let Some(source) = attribute(tag, "source") else {
            expanded.push_str(tag);
            cursor = end + 1;
            continue;
        };
        let first_gid = attribute(tag, "firstgid").unwrap_or_else(|| "1".to_string());
        let tsx_path = base.join(source);
        let tsx = std::fs::read_to_string(&tsx_path).ok()?;
        let root = tsx.find("<tileset")?;
        let root_end = tsx[root..].find('>')? + root;
        let root_tag = &tsx[root..=root_end];
        let tsx_content_end = tsx[root_end + 1..]
            .find("</tileset>")
            .map(|offset| root_end + 1 + offset)
            .unwrap_or(tsx.len());
        let attributes = root_tag
            .strip_prefix("<tileset")?
            .trim_end_matches('>')
            .trim()
            .to_string();
        expanded.push_str(&format!("<tileset firstgid=\"{first_gid}\" {attributes}>"));
        let mut content = tsx[root_end + 1..tsx_content_end].to_string();
        if let Some(image) = attribute(
            content.find("<image").and_then(|offset| {
                let start = offset + content[offset..].find("<image")?;
                let end = content[start..].find('>')? + start;
                Some(&content[start..=end])
            })?,
            "source",
        ) {
            let image_path = tsx_path.parent().unwrap_or(base).join(&image);
            let image_path = if image_path.is_absolute() {
                image_path
            } else {
                std::env::current_dir().ok()?.join(image_path)
            };
            if let Some(image_path) = image_path.to_str() {
                content = content.replace(
                    &format!("source=\"{image}\""),
                    &format!("source=\"{image_path}\""),
                );
            }
        }
        expanded.push_str(&content);
        expanded.push_str("</tileset>");
        cursor = end + 1;
    }
    expanded.push_str(&text[cursor..]);
    Some(expanded)
}

/// Reads inline Tiled tilesets and CSV, XML-tile or base64 layer data.
fn parse_tilemap(text: &str) -> Option<Tmx> {
    let map_tag = text.find("<map").and_then(|start| {
        let end = text[start..].find('>')? + start;
        Some(&text[start..=end])
    })?;
    let tile_width = attribute(map_tag, "tilewidth")?.parse().ok()?;
    let tile_height = attribute(map_tag, "tileheight")?.parse().ok()?;
    let mut tilesets = Vec::new();
    let mut cursor = 0usize;
    while let Some(offset) = text[cursor..].find("<tileset") {
        let start = cursor + offset;
        let tag_end = text[start..].find('>')? + start;
        let first_gid = attribute(&text[start..=tag_end], "firstgid")
            .and_then(|value| value.parse().ok())
            .unwrap_or(1);
        let end = text[tag_end + 1..]
            .find("</tileset>")
            .map(|offset| tag_end + 1 + offset + "</tileset>".len())
            .unwrap_or(tag_end + 1);
        if let Some(tileset) = parse_tileset(&text[start..end], first_gid) {
            tilesets.push(tileset);
        }
        cursor = end.max(tag_end + 1);
    }
    tilesets.sort_by_key(|tileset| tileset.first_gid);
    if tilesets.is_empty() {
        return None;
    }

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

        let data_tag = &text[data_open..data_open + data_gt];
        let Some(tiles) = decode_layer_data(data_tag, payload) else {
            return None;
        };
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
        tilesets,
        layers,
    })
}

/// Map a Tiled global tile ID to its source rectangle in the first tileset
/// atlas. This is pure geometry and can be tested without a graphics context.
fn tile_source_rect(
    tileset: &TmxTileset,
    tile_id: u32,
    atlas_width: f32,
    atlas_height: f32,
) -> Option<macroquad::math::Rect> {
    let atlas_width = if atlas_width > 0.0 {
        atlas_width
    } else {
        tileset.image_width?
    };
    let atlas_height = if atlas_height > 0.0 {
        atlas_height
    } else {
        tileset.image_height?
    };
    let columns = if tileset.columns > 0 {
        tileset.columns
    } else {
        let step = tileset.tile_width + tileset.spacing;
        if step <= 0.0 {
            return None;
        }
        ((atlas_width - 2.0 * tileset.margin + tileset.spacing) / step).floor() as i64
    };
    if columns <= 0 || (tileset.tile_count > 0 && tile_id >= tileset.tile_count) {
        return None;
    }
    let x =
        tileset.margin + (tile_id as i64 % columns) as f32 * (tileset.tile_width + tileset.spacing);
    let y = tileset.margin
        + (tile_id as i64 / columns) as f32 * (tileset.tile_height + tileset.spacing);
    if x + tileset.tile_width > atlas_width || y + tileset.tile_height > atlas_height {
        return None;
    }
    Some(macroquad::math::Rect::new(
        x,
        y,
        tileset.tile_width,
        tileset.tile_height,
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
    let Some(text) = expand_external_tilesets(&text, Path::new(&path)) else {
        return -1;
    };
    let Some(tilemap) = parse_tilemap(&text) else {
        return -1;
    };
    let is_headless = headless();
    let map_path = Path::new(&path);
    let base = map_path
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."));
    let atlases: Vec<(i64, f32, f32)> = tilemap
        .tilesets
        .iter()
        .map(|tileset| {
            let loaded = if is_headless {
                None
            } else {
                tileset
                    .image_source
                    .as_deref()
                    .and_then(|source| base.join(source).to_str().and_then(load_texture))
            };
            match loaded {
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
                    tileset.image_width.unwrap_or(0.0),
                    tileset.image_height.unwrap_or(0.0),
                ),
            }
        })
        .collect();
    with(|state| {
        let mut scene = Scene { lists: Vec::new() };
        // Tiled rows run top-down and the world is bottom-up, so a tile's row
        // index is flipped when placing it.
        for layer in &tilemap.layers {
            let row_width = layer.width.max(1);
            let height_tiles = ((layer.tiles.len() as i64 + row_width - 1) / row_width).max(1);
            let mut list = Vec::new();
            for (position, encoded_gid) in layer.tiles.iter().enumerate() {
                let gid = encoded_gid & 0x0fff_ffff;
                if gid == 0 {
                    continue;
                }
                let column = position as i64 % row_width;
                let row = position as i64 / row_width;
                let x = (column as f32 + 0.5) * tilemap.tile_width * scaling;
                let y = ((height_tiles - row - 1) as f32 + 0.5) * tilemap.tile_height * scaling;
                let selected = tilemap
                    .tilesets
                    .iter()
                    .enumerate()
                    .rev()
                    .find(|(_, tileset)| tileset.first_gid <= gid);
                let (texture_index, source) = selected
                    .map(|(index, tileset)| {
                        let (texture, atlas_width, atlas_height) = atlases[index];
                        (
                            texture,
                            tile_source_rect(
                                tileset,
                                gid - tileset.first_gid,
                                atlas_width,
                                atlas_height,
                            ),
                        )
                    })
                    .unwrap_or((-1, None));
                let shade = 96 + ((gid * 37) % 96) as i64;
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
                    sprite.flip_x = encoded_gid & 0x8000_0000 != 0;
                    sprite.flip_y = encoded_gid & 0x4000_0000 != 0;
                    if encoded_gid & 0x2000_0000 != 0 {
                        sprite.angle = 90.0;
                    }
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
        standing_on: None,
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
type WallBox = (i64, f32, f32, f32, f32, f32);

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
                .filter_map(|index| {
                    state.sprite(*index).map(|wall| {
                        (
                            *index,
                            wall.x,
                            wall.y,
                            wall.width(),
                            wall.height(),
                            wall.angle,
                        )
                    })
                })
                .collect()
        })
        .unwrap_or_default()
}

/// The surface height a slope presents at horizontal position `x`, measured
/// from the box's bottom-left corner and capped at its top.
fn slope_surface(wall: &WallBox, x: f32) -> f32 {
    let (_, wx, wy, ww, wh, angle) = *wall;
    if ww <= 0.0 {
        return wy + wh / 2.0;
    }
    let left = wx - ww / 2.0;
    let along = ((x - left) / ww).clamp(0.0, 1.0);
    let base = wy - wh / 2.0;
    (base + angle.to_radians().tan() * ww * along).min(wy + wh / 2.0)
}

fn rotated_half_extents(width: f32, height: f32, angle: f32) -> (f32, f32) {
    let (sin, cos) = angle.to_radians().sin_cos();
    (
        (cos.abs() * width + sin.abs() * height) / 2.0,
        (sin.abs() * width + cos.abs() * height) / 2.0,
    )
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
        let (gravity, walls, one_way, player, was_on_ground, standing_on) = (
            engine.gravity,
            engine.walls,
            engine.one_way,
            engine.player,
            engine.on_ground,
            engine.standing_on,
        );
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
        let (half_width, half_height) = rotated_half_extents(width, height, player_sprite.angle);
        let dt = state.dt as f32;

        let solid = wall_boxes(state, walls);
        let platforms = wall_boxes(state, one_way);
        if was_on_ground {
            if let Some((platform_id, previous_pose)) = standing_on {
                if let Some((_, px, py, _, _, angle)) = solid
                    .iter()
                    .chain(platforms.iter())
                    .find(|(index, ..)| *index == platform_id)
                {
                    let rotation = (*angle - previous_pose.angle).to_radians();
                    let (sin, cos) = rotation.sin_cos();
                    let relative_x = x - previous_pose.x;
                    let relative_y = y - previous_pose.y;
                    x = *px + relative_x * cos - relative_y * sin;
                    y = *py + relative_x * sin + relative_y * cos;
                }
            }
        }
        // Where the feet were before this step, which is what tells a one-way
        // platform whether the player came from above.
        let feet_before = y - half_height;

        // --- Horizontal: move, then push out of the side of a solid wall.
        // Slopes never block sideways, and one-way platforms never block
        // horizontally at all.
        x += vx * dt;
        for wall in &solid {
            let (_, wx, wy, ww, wh, angle) = *wall;
            if angle != 0.0 {
                continue;
            }
            let half_w = half_width + ww / 2.0;
            let half_h = half_height + wh / 2.0;
            if (x - wx).abs() > half_w || (y - wy).abs() > half_h {
                continue;
            }
            if vx > 0.0 {
                x = wx - ww / 2.0 - half_width;
            } else if vx < 0.0 {
                x = wx + ww / 2.0 + half_width;
            }
            vx = 0.0;
        }

        // --- Vertical: gravity, then land, bump a ceiling, or ride a slope.
        vy -= gravity * dt;
        y += vy * dt;

        let mut on_ground = false;
        let mut landed_on = None;
        for wall in &solid {
            let (wall_id, wx, wy, ww, wh, angle) = *wall;
            if angle != 0.0 {
                continue;
            }
            let half_w = half_width + ww / 2.0;
            let half_h = half_height + wh / 2.0;
            if (x - wx).abs() > half_w || (y - wy).abs() > half_h {
                continue;
            }
            if vy <= 0.0 && y >= wy {
                // Falling onto the top of the wall: rest on it.
                y = wy + wh / 2.0 + half_height;
                vy = 0.0;
                on_ground = true;
                landed_on = Some((
                    wall_id,
                    PlatformPose {
                        x: wx,
                        y: wy,
                        angle,
                    },
                ));
            } else if vy > 0.0 && y < wy {
                // Rising into the underside: stop.
                y = wy - wh / 2.0 - half_height;
                vy = 0.0;
            }
        }

        // --- Slopes: land on the ramp surface under the player's centre.
        for wall in &solid {
            let (wall_id, wx, wy, ww, _wh, angle) = *wall;
            if angle == 0.0 {
                continue;
            }
            if (x - wx).abs() > half_width + ww / 2.0 {
                continue;
            }
            let surface = slope_surface(wall, x);
            if vy <= 0.0 && y - half_height <= surface && feet_before >= surface - GROUND_SNAP {
                y = surface + half_height;
                vy = 0.0;
                on_ground = true;
                landed_on = Some((
                    wall_id,
                    PlatformPose {
                        x: wx,
                        y: wy,
                        angle,
                    },
                ));
            }
        }

        // --- One-way platforms: solid only for a player falling from above.
        for wall in &platforms {
            let (wall_id, wx, wy, ww, wh, angle) = *wall;
            let half_w = half_width + ww / 2.0;
            let half_h = half_height + wh / 2.0;
            if (x - wx).abs() > half_w || (y - wy).abs() > half_h {
                continue;
            }
            let top = wy + wh / 2.0;
            if vy <= 0.0 && feet_before >= top - GROUND_SNAP {
                y = top + half_height;
                vy = 0.0;
                on_ground = true;
                landed_on = Some((
                    wall_id,
                    PlatformPose {
                        x: wx,
                        y: wy,
                        angle,
                    },
                ));
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
            engine.standing_on = if on_ground { landed_on } else { None };
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

export_int!(lynxer_game_load_sound, args, {
    let path = args.string(0).to_string();
    if with(|state| state.headless) {
        return -1;
    }
    if File::open(&path)
        .ok()
        .and_then(|file| Decoder::new(BufReader::new(file)).ok())
        .is_none()
    {
        return -1;
    }
    let Ok((output, handle)) = OutputStream::try_default() else {
        return -1;
    };
    let Ok(sink) = Sink::try_new(&handle) else {
        return -1;
    };
    sink.pause();
    with(|state| {
        state.sounds.push(Some(SoundEntry {
            _output: output,
            sink,
            path,
            volume: 1.0,
            looping: false,
        }));
        (state.sounds.len() - 1) as i64
    })
});

fn update_sound(index: i64, body: impl FnOnce(&mut SoundEntry) -> i64) -> i64 {
    if index < 0 {
        return -1;
    }
    with(|state| {
        match state
            .sounds
            .get_mut(index as usize)
            .and_then(Option::as_mut)
        {
            Some(entry) => body(entry),
            None => -1,
        }
    })
}

export_int!(lynxer_game_play_sound, args, {
    start_sound(args.int(0), false)
});

export_int!(lynxer_game_loop_sound, args, {
    start_sound(args.int(0), true)
});

export_int!(lynxer_game_stop_sound, args, {
    update_sound(args.int(0), |entry| {
        entry.sink.pause();
        entry.sink.clear();
        entry.looping = false;
        0
    })
});

export_int!(lynxer_game_set_sound_volume, args, {
    let volume = args.float(1).clamp(0.0, 1.0);
    update_sound(args.int(0), move |entry| {
        entry.volume = volume;
        entry.sink.set_volume(volume);
        0
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
            Some(entry) => sound_sink_is_playing(&entry.sink) as i64,
            None => 0,
        }
    })
});

fn sound_sink_is_playing(sink: &Sink) -> bool {
    !sink.empty() && !sink.is_paused()
}

fn start_sound(index: i64, looping: bool) -> i64 {
    let Some((path, volume)) = with(|state| {
        state
            .sounds
            .get(index.max(0) as usize)
            .and_then(Option::as_ref)
            .map(|entry| (entry.path.clone(), entry.volume))
    }) else {
        return -1;
    };
    if index < 0 {
        return -1;
    }
    let Ok(file) = File::open(path) else {
        return -1;
    };
    let Ok(source) = Decoder::new(BufReader::new(file)) else {
        return -1;
    };
    with(|state| {
        let Some(entry) = state
            .sounds
            .get_mut(index as usize)
            .and_then(Option::as_mut)
        else {
            return -1;
        };
        entry.sink.pause();
        entry.sink.clear();
        entry.sink.set_volume(volume);
        if looping {
            entry.sink.append(source.repeat_infinite());
        } else {
            entry.sink.append(source);
        }
        entry.sink.play();
        entry.looping = looping;
        0
    })
}

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
    use super::{decode_layer_data, expand_external_tilesets, parse_tilemap, tile_source_rect};
    use std::io::Write;

    #[test]
    fn tilemap_atlas_coordinates_respect_first_gid_margin_and_spacing() {
        let text = r#"<map tilewidth="16" tileheight="8"><tileset firstgid="5" tilewidth="16" tileheight="8" columns="3" margin="1" spacing="2"><image source="tiles.png" width="55" height="21"/></tileset><layer name="ground" width="4" height="1"><data encoding="csv">5,6,7,8</data></layer></map>"#;
        let tilemap = parse_tilemap(text).expect("valid inline tileset and layer");
        let tileset = &tilemap.tilesets[0];

        let first = tile_source_rect(tileset, 0, 55.0, 21.0).unwrap();
        assert_eq!((first.x, first.y, first.w, first.h), (1.0, 1.0, 16.0, 8.0));

        let next_row = tile_source_rect(tileset, 3, 55.0, 21.0).unwrap();
        assert_eq!(
            (next_row.x, next_row.y, next_row.w, next_row.h),
            (1.0, 11.0, 16.0, 8.0)
        );
        assert!(tile_source_rect(tileset, 6, 55.0, 21.0).is_none());
        assert!(tile_source_rect(tileset, 3, 30.0, 10.0).is_none());
    }

    #[test]
    fn tilemap_parses_multiple_tilesets_and_common_layer_data() {
        let text = r#"<map tilewidth="16" tileheight="16"><tileset firstgid="1" tilewidth="16" tileheight="16" columns="1" tilecount="2"><image source="a.png" width="16" height="32"/></tileset><tileset firstgid="10" tilewidth="8" tileheight="8" columns="1" tilecount="1"><image source="b.png" width="8" height="8"/></tileset><layer name="csv" width="2"><data encoding="csv">1,10</data></layer><layer name="xml" width="2"><data><tile gid="2"/><tile gid="10"/></data></layer><layer name="base64" width="1"><data encoding="base64">BwAAAA==</data></layer></map>"#;
        let tilemap = parse_tilemap(text).expect("valid TMX with multiple tilesets");
        assert_eq!(tilemap.tilesets.len(), 2);
        assert_eq!(tilemap.tilesets[1].first_gid, 10);
        assert_eq!(tilemap.layers[0].tiles, vec![1, 10]);
        assert_eq!(tilemap.layers[1].tiles, vec![2, 10]);
        assert_eq!(tilemap.layers[2].tiles, vec![7]);
        assert_eq!(
            tile_source_rect(&tilemap.tilesets[1], 0, 8.0, 8.0).map(|rect| (rect.w, rect.h)),
            Some((8.0, 8.0))
        );
    }

    #[test]
    fn tilemap_decodes_compressed_base64_payloads() {
        use base64::Engine;
        use flate2::write::{GzEncoder, ZlibEncoder};
        use flate2::Compression;

        let gzip = {
            let mut encoder = GzEncoder::new(Vec::new(), Compression::default());
            encoder.write_all(&42u32.to_le_bytes()).unwrap();
            encoder.finish().unwrap()
        };
        let zlib = {
            let mut encoder = ZlibEncoder::new(Vec::new(), Compression::default());
            encoder.write_all(&42u32.to_le_bytes()).unwrap();
            encoder.finish().unwrap()
        };
        for (compression, bytes) in [("gzip", gzip), ("zlib", zlib)] {
            let payload = base64::engine::general_purpose::STANDARD.encode(bytes);
            let tag = format!("<data encoding=\"base64\" compression=\"{compression}\">");
            assert_eq!(decode_layer_data(&tag, &payload), Some(vec![42]));
        }
    }

    #[test]
    fn external_tsx_and_its_image_resolve_from_their_own_directory() {
        use std::time::{SystemTime, UNIX_EPOCH};

        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let directory = std::env::temp_dir().join(format!("lynxer-tsx-{nonce}"));
        std::fs::create_dir_all(&directory).unwrap();
        std::fs::write(directory.join("tiles.png"), []).unwrap();
        std::fs::write(
            directory.join("tiles.tsx"),
            r#"<tileset name="external" tilewidth="8" tileheight="8" columns="1" tilecount="1"><image source="tiles.png" width="8" height="8"/></tileset>"#,
        )
        .unwrap();
        let text = r#"<map tilewidth="8" tileheight="8"><tileset firstgid="9" source="tiles.tsx"/><layer name="ground" width="1"><data encoding="csv">9</data></layer></map>"#;
        let expanded = expand_external_tilesets(text, &directory.join("map.tmx")).unwrap();
        let tilemap = parse_tilemap(&expanded).unwrap();
        assert_eq!(tilemap.tilesets[0].first_gid, 9);
        assert_eq!(
            tilemap.tilesets[0].image_source.as_deref(),
            std::fs::canonicalize(directory.join("tiles.png"))
                .unwrap()
                .to_str()
        );
        std::fs::remove_dir_all(directory).unwrap();
    }

    #[test]
    fn audio_sink_state_tracks_completion_looping_and_stop_when_available() {
        use super::sound_sink_is_playing;
        use rodio::{OutputStream, Sink, Source};

        let Ok((_output, handle)) = OutputStream::try_default() else {
            return;
        };
        let sink = Sink::try_new(&handle).unwrap();
        sink.append(rodio::buffer::SamplesBuffer::new(
            1,
            1_000,
            vec![0.0f32; 10],
        ));
        sink.play();
        assert!(sound_sink_is_playing(&sink));
        std::thread::sleep(std::time::Duration::from_millis(100));
        assert!(!sound_sink_is_playing(&sink));

        let repeated =
            rodio::buffer::SamplesBuffer::new(1, 1_000, vec![0.0f32; 10]).repeat_infinite();
        sink.append(repeated);
        sink.play();
        assert!(sound_sink_is_playing(&sink));
        sink.pause();
        assert!(!sound_sink_is_playing(&sink));
        sink.clear();
        assert!(!sound_sink_is_playing(&sink));
    }
}
