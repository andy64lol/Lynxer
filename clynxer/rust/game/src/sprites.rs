//! Sprites, sprite lists and textures.
//!
//! Sprites and textures live in `State` registries and are referenced by an
//! integer handle (`-1` means "not available"). Collisions are axis-aligned
//! box tests against the sprite's scaled size.

use macroquad::math::vec2;
use macroquad::shapes::draw_rectangle;
use macroquad::texture::{DrawTextureParams, Texture2D};

use crate::state::{color_of_a, with, Sprite, State};

fn load_texture(path: &str) -> Option<Texture2D> {
    let bytes = std::fs::read(path).ok()?;
    Some(Texture2D::from_file_with_format(&bytes, None))
}

fn draw_sprite(state: &State, index: i64) {
    let sprite = match state.sprite(index) {
        Some(sprite) => *sprite,
        None => return,
    };
    if !sprite.visible {
        return;
    }
    let (cx, cy) = state.screen_pos(sprite.x, sprite.y);
    let scale = state.screen_scale();
    let width = sprite.width() * scale;
    let height = sprite.height() * scale;
    let tint = sprite.tint();
    if sprite.texture >= 0 {
        if let Some(Some(texture)) = state.textures.get(sprite.texture as usize) {
            macroquad::texture::draw_texture_ex(
                texture,
                cx - width / 2.0,
                cy - height / 2.0,
                tint,
                DrawTextureParams {
                    dest_size: Some(vec2(width, height)),
                    rotation: -sprite.angle.to_radians(),
                    flip_x: sprite.flip_x,
                    flip_y: sprite.flip_y,
                    pivot: Some(vec2(width / 2.0, height / 2.0)),
                    ..Default::default()
                },
            );
            return;
        }
    }
    draw_rectangle(cx - width / 2.0, cy - height / 2.0, width, height, tint);
}

fn collides(a: &Sprite, b: &Sprite) -> bool {
    let half_w = (a.width() + b.width()) / 2.0;
    let half_h = (a.height() + b.height()) / 2.0;
    (a.x - b.x).abs() <= half_w && (a.y - b.y).abs() <= half_h
}

// --- creation ---------------------------------------------------------------

game_export_int!(lynxer_game_make_solid_sprite, args, {
    with(|state| {
        let sprite = Sprite::solid(
            args.float(5),
            args.float(6),
            args.float(0),
            args.float(1),
            color_of_a(args.int(2), args.int(3), args.int(4), 255),
        );
        state.sprites.push(Some(sprite));
        (state.sprites.len() - 1) as i64
    })
});

game_export_int!(lynxer_game_load_sprite, args, {
    let path = args.string(0).to_string();
    if with(|state| state.headless) {
        return -1;
    }
    let texture = match load_texture(&path) {
        Some(texture) => texture,
        None => return -1,
    };
    with(|state| {
        let mut sprite = Sprite::solid(
            args.float(1),
            args.float(2),
            texture.width(),
            texture.height(),
            macroquad::color::WHITE,
        );
        sprite.solid = false;
        sprite.scale = if args.float(0) > 0.0 {
            args.float(0)
        } else {
            1.0
        };
        sprite.texture = state.textures.len() as i64;
        state.textures.push(Some(texture));
        state.sprites.push(Some(sprite));
        (state.sprites.len() - 1) as i64
    })
});

game_export_int!(lynxer_game_load_texture, args, {
    let path = args.string(0).to_string();
    if with(|state| state.headless) {
        return -1;
    }
    match load_texture(&path) {
        Some(texture) => with(|state| {
            state.textures.push(Some(texture));
            (state.textures.len() - 1) as i64
        }),
        None => -1,
    }
});

game_export_int!(lynxer_game_set_sprite_texture, args, {
    let index = args.int(0);
    let texture_index = args.int(1);
    with(|state| {
        let size = state
            .textures
            .get(texture_index.max(0) as usize)
            .and_then(|entry| entry.as_ref())
            .map(|texture| (texture.width(), texture.height()));
        if let Some((width, height)) = size {
            if let Some(sprite) = state.sprite_mut(index) {
                sprite.texture = texture_index;
                sprite.solid = false;
                sprite.w = width;
                sprite.h = height;
            }
        }
        0
    })
});

// --- getters ----------------------------------------------------------------

game_export_float!(lynxer_game_get_sprite_x, args, {
    with(|state| state.sprite(args.int(0)).map(|s| s.x as f64).unwrap_or(0.0))
});

game_export_float!(lynxer_game_get_sprite_y, args, {
    with(|state| state.sprite(args.int(0)).map(|s| s.y as f64).unwrap_or(0.0))
});

game_export_float!(lynxer_game_get_sprite_angle, args, {
    with(|state| {
        state
            .sprite(args.int(0))
            .map(|s| s.angle as f64)
            .unwrap_or(0.0)
    })
});

game_export_float!(lynxer_game_get_sprite_scale, args, {
    with(|state| {
        state
            .sprite(args.int(0))
            .map(|s| s.scale as f64)
            .unwrap_or(0.0)
    })
});

game_export_float!(lynxer_game_get_sprite_width, args, {
    with(|state| {
        state
            .sprite(args.int(0))
            .map(|s| s.width() as f64)
            .unwrap_or(0.0)
    })
});

game_export_float!(lynxer_game_get_sprite_height, args, {
    with(|state| {
        state
            .sprite(args.int(0))
            .map(|s| s.height() as f64)
            .unwrap_or(0.0)
    })
});

game_export_float!(lynxer_game_get_sprite_vx, args, {
    with(|state| {
        state
            .sprite(args.int(0))
            .map(|s| s.vx as f64)
            .unwrap_or(0.0)
    })
});

game_export_float!(lynxer_game_get_sprite_vy, args, {
    with(|state| {
        state
            .sprite(args.int(0))
            .map(|s| s.vy as f64)
            .unwrap_or(0.0)
    })
});

game_export_float!(lynxer_game_get_sprite_angular_velocity, args, {
    with(|state| {
        state
            .sprite(args.int(0))
            .map(|s| s.angular_velocity as f64)
            .unwrap_or(0.0)
    })
});

game_export_int!(lynxer_game_get_sprite_alpha, args, {
    with(|state| {
        state
            .sprite(args.int(0))
            .map(|s| s.alpha as i64)
            .unwrap_or(0)
    })
});

game_export_int!(lynxer_game_get_sprite_visible, args, {
    with(|state| {
        state
            .sprite(args.int(0))
            .map(|s| s.visible as i64)
            .unwrap_or(0)
    })
});

game_export_int!(lynxer_game_sprite_exists, args, {
    with(|state| (state.sprite(args.int(0)).is_some()) as i64)
});

game_export_string!(lynxer_game_get_sprite_position, args, {
    with(|state| match state.sprite(args.int(0)) {
        Some(sprite) => format!("{},{}", sprite.x, sprite.y),
        None => "0,0".to_string(),
    })
});

// --- setters ----------------------------------------------------------------

game_export_int!(lynxer_game_set_sprite_pos, args, {
    let index = args.int(0);
    let x = args.float(1);
    let y = args.float(2);
    with(|state| {
        if let Some(sprite) = state.sprite_mut(index) {
            sprite.x = x;
            sprite.y = y;
        }
        0
    })
});

game_export_int!(lynxer_game_set_sprite_angle, args, {
    let index = args.int(0);
    let angle = args.float(1);
    with(|state| {
        if let Some(sprite) = state.sprite_mut(index) {
            sprite.angle = angle;
        }
        0
    })
});

game_export_int!(lynxer_game_set_sprite_scale, args, {
    let index = args.int(0);
    let scale = args.float(1);
    with(|state| {
        if let Some(sprite) = state.sprite_mut(index) {
            sprite.scale = scale;
        }
        0
    })
});

game_export_int!(lynxer_game_set_sprite_velocity, args, {
    let index = args.int(0);
    let vx = args.float(1);
    let vy = args.float(2);
    with(|state| {
        if let Some(sprite) = state.sprite_mut(index) {
            sprite.vx = vx;
            sprite.vy = vy;
        }
        0
    })
});

game_export_int!(lynxer_game_set_sprite_angular_velocity, args, {
    let index = args.int(0);
    let value = args.float(1);
    with(|state| {
        if let Some(sprite) = state.sprite_mut(index) {
            sprite.angular_velocity = value;
        }
        0
    })
});

game_export_int!(lynxer_game_stop_sprite, args, {
    let index = args.int(0);
    with(|state| {
        if let Some(sprite) = state.sprite_mut(index) {
            sprite.vx = 0.0;
            sprite.vy = 0.0;
        }
        0
    })
});

game_export_int!(lynxer_game_move_sprite_toward, args, {
    let index = args.int(0);
    let target_x = args.float(1);
    let target_y = args.float(2);
    let speed = args.float(3);
    with(|state| {
        if let Some(sprite) = state.sprite_mut(index) {
            let dx = target_x - sprite.x;
            let dy = target_y - sprite.y;
            let distance = (dx * dx + dy * dy).sqrt();
            if distance > 0.0 {
                sprite.vx = dx / distance * speed;
                sprite.vy = dy / distance * speed;
            } else {
                sprite.vx = 0.0;
                sprite.vy = 0.0;
            }
        }
        0
    })
});

game_export_int!(lynxer_game_face_sprite_to, args, {
    let index = args.int(0);
    let target_x = args.float(1);
    let target_y = args.float(2);
    with(|state| {
        if let Some(sprite) = state.sprite_mut(index) {
            let dx = target_x - sprite.x;
            let dy = target_y - sprite.y;
            sprite.angle = dy.atan2(dx).to_degrees();
        }
        0
    })
});

game_export_int!(lynxer_game_set_sprite_alpha, args, {
    let index = args.int(0);
    let alpha = args.int(1).clamp(0, 255) as u8;
    with(|state| {
        if let Some(sprite) = state.sprite_mut(index) {
            sprite.alpha = alpha;
        }
        0
    })
});

game_export_int!(lynxer_game_set_sprite_color, args, {
    let index = args.int(0);
    let color = color_of_a(args.int(1), args.int(2), args.int(3), args.int(4));
    with(|state| {
        if let Some(sprite) = state.sprite_mut(index) {
            sprite.color = color;
        }
        0
    })
});

game_export_int!(lynxer_game_set_sprite_visible, args, {
    let index = args.int(0);
    let visible = args.int(1) != 0;
    with(|state| {
        if let Some(sprite) = state.sprite_mut(index) {
            sprite.visible = visible;
        }
        0
    })
});

game_export_int!(lynxer_game_flip_sprite_h, args, {
    let index = args.int(0);
    with(|state| {
        if let Some(sprite) = state.sprite_mut(index) {
            sprite.flip_x = !sprite.flip_x;
        }
        0
    })
});

game_export_int!(lynxer_game_flip_sprite_v, args, {
    let index = args.int(0);
    with(|state| {
        if let Some(sprite) = state.sprite_mut(index) {
            sprite.flip_y = !sprite.flip_y;
        }
        0
    })
});

game_export_int!(lynxer_game_destroy_sprite, args, {
    let index = args.int(0);
    with(|state| {
        if index >= 0 && (index as usize) < state.sprites.len() {
            state.sprites[index as usize] = None;
            for list in &mut state.lists {
                list.retain(|entry| *entry != index);
            }
        }
        0
    })
});

// --- update / draw ----------------------------------------------------------

game_export_int!(lynxer_game_update_sprite, args, {
    let index = args.int(0);
    with(|state| {
        let dt = state.dt as f32;
        if let Some(sprite) = state.sprite_mut(index) {
            sprite.x += sprite.vx * dt;
            sprite.y += sprite.vy * dt;
            sprite.angle += sprite.angular_velocity * dt;
        }
        0
    })
});

game_export_int!(lynxer_game_draw_sprite, args, {
    let index = args.int(0);
    with(|state| {
        if !state.headless {
            draw_sprite(state, index);
        }
        0
    })
});

game_export_int!(lynxer_game_draw_texture, args, {
    if with(|state| state.headless) {
        return 0;
    }
    let texture_index = args.int(0);
    with(|state| {
        let parameters = state
            .textures
            .get(texture_index.max(0) as usize)
            .and_then(|entry| entry.as_ref())
            .map(|texture| (texture.width(), texture.height()));
        if let (Some((texture_width, texture_height)), Some(Some(texture))) = (
            parameters,
            state.textures.get(texture_index.max(0) as usize),
        ) {
            let (cx, cy) = state.screen_pos(args.float(1), args.float(2));
            let scale = state.screen_scale();
            let width = args.float(3) * scale;
            let height = args.float(4) * scale;
            macroquad::texture::draw_texture_ex(
                texture,
                cx - width / 2.0,
                cy - height / 2.0,
                macroquad::color::WHITE,
                DrawTextureParams {
                    dest_size: Some(vec2(width, height)),
                    rotation: -args.float(5).to_radians(),
                    pivot: Some(vec2(width / 2.0, height / 2.0)),
                    ..Default::default()
                },
            );
            let _ = (texture_width, texture_height);
        }
        0
    })
});

game_export_int!(lynxer_game_draw_texture_at, args, {
    let path = args.string(0).to_string();
    if with(|state| state.headless) {
        return 0;
    }
    let texture = match load_texture(&path) {
        Some(texture) => texture,
        None => return 0,
    };
    with(|state| {
        let (cx, cy) = state.screen_pos(args.float(0), args.float(1));
        let scale = args.float(2) * state.screen_scale();
        let width = texture.width() * scale;
        let height = texture.height() * scale;
        macroquad::texture::draw_texture_ex(
            &texture,
            cx - width / 2.0,
            cy - height / 2.0,
            macroquad::color::WHITE,
            DrawTextureParams {
                dest_size: Some(vec2(width, height)),
                pivot: Some(vec2(width / 2.0, height / 2.0)),
                ..Default::default()
            },
        );
        0
    })
});

game_export_int!(lynxer_game_draw_texture_rect, args, {
    if with(|state| state.headless) {
        return 0;
    }
    let texture_index = args.int(0);
    with(|state| {
        if let Some(Some(texture)) = state.textures.get(texture_index.max(0) as usize) {
            let (cx, cy) = state.screen_pos(
                args.float(1) + args.float(3) / 2.0,
                args.float(2) + args.float(4) / 2.0,
            );
            let scale = state.screen_scale();
            let width = args.float(3) * scale;
            let height = args.float(4) * scale;
            macroquad::texture::draw_texture_ex(
                texture,
                cx - width / 2.0,
                cy - height / 2.0,
                macroquad::color::WHITE,
                DrawTextureParams {
                    dest_size: Some(vec2(width, height)),
                    ..Default::default()
                },
            );
        }
        0
    })
});

// --- collisions and queries -------------------------------------------------

game_export_int!(lynxer_game_sprite_collides, args, {
    let first = args.int(0);
    let second = args.int(1);
    with(|state| match (state.sprite(first), state.sprite(second)) {
        (Some(a), Some(b)) => collides(a, b) as i64,
        _ => 0,
    })
});

game_export_int!(lynxer_game_sprite_collides_with_list, args, {
    let index = args.int(0);
    let list_index = args.int(1);
    with(|state| {
        let sprite = match state.sprite(index) {
            Some(sprite) => *sprite,
            None => return 0,
        };
        let entries = match state.lists.get(list_index.max(0) as usize) {
            Some(entries) => entries.clone(),
            None => return 0,
        };
        for entry in entries {
            if let Some(other) = state.sprite(entry) {
                if collides(&sprite, other) {
                    return 1;
                }
            }
        }
        0
    })
});

game_export_string!(lynxer_game_get_colliding_sprites, args, {
    let index = args.int(0);
    let list_index = args.int(1);
    with(|state| {
        let sprite = match state.sprite(index) {
            Some(sprite) => *sprite,
            None => return "[]".to_string(),
        };
        let entries = match state.lists.get(list_index.max(0) as usize) {
            Some(entries) => entries.clone(),
            None => return "[]".to_string(),
        };
        let mut hits = Vec::new();
        for entry in entries {
            if let Some(other) = state.sprite(entry) {
                if collides(&sprite, other) {
                    hits.push(entry.to_string());
                }
            }
        }
        format!("[{}]", hits.join(","))
    })
});

game_export_float!(lynxer_game_sprite_distance, args, {
    let first = args.int(0);
    let second = args.int(1);
    with(|state| match (state.sprite(first), state.sprite(second)) {
        (Some(a), Some(b)) => {
            let dx = a.x - b.x;
            let dy = a.y - b.y;
            (dx * dx + dy * dy).sqrt() as f64
        }
        _ => 0.0,
    })
});

game_export_int!(lynxer_game_sprite_near, args, {
    let index = args.int(0);
    let target_x = args.float(1);
    let target_y = args.float(2);
    let range = args.float(3);
    with(|state| match state.sprite(index) {
        Some(sprite) => {
            let dx = sprite.x - target_x;
            let dy = sprite.y - target_y;
            ((dx * dx + dy * dy).sqrt() <= range) as i64
        }
        None => 0,
    })
});

// --- sprite lists -----------------------------------------------------------

game_export_int!(lynxer_game_make_sprite_list, args, {
    let _ = args;
    with(|state| {
        state.lists.push(Vec::new());
        (state.lists.len() - 1) as i64
    })
});

game_export_int!(lynxer_game_add_to_list, args, {
    let list_index = args.int(0);
    let sprite_index = args.int(1);
    with(|state| {
        if let Some(list) = state.lists.get_mut(list_index.max(0) as usize) {
            if !list.contains(&sprite_index) {
                list.push(sprite_index);
            }
        }
        0
    })
});

game_export_int!(lynxer_game_remove_sprite_from_list, args, {
    let list_index = args.int(0);
    let sprite_index = args.int(1);
    with(|state| {
        if let Some(list) = state.lists.get_mut(list_index.max(0) as usize) {
            list.retain(|entry| *entry != sprite_index);
        }
        0
    })
});

game_export_int!(lynxer_game_clear_sprite_list, args, {
    let list_index = args.int(0);
    with(|state| {
        if let Some(list) = state.lists.get_mut(list_index.max(0) as usize) {
            list.clear();
        }
        0
    })
});

game_export_int!(lynxer_game_get_sprite_list_count, args, {
    let list_index = args.int(0);
    with(|state| {
        state
            .lists
            .get(list_index.max(0) as usize)
            .map(|list| list.len() as i64)
            .unwrap_or(0)
    })
});

game_export_int!(lynxer_game_draw_sprite_list, args, {
    let list_index = args.int(0);
    with(|state| {
        if state.headless {
            return 0;
        }
        let entries = match state.lists.get(list_index.max(0) as usize) {
            Some(entries) => entries.clone(),
            None => return 0,
        };
        for entry in entries {
            draw_sprite(state, entry);
        }
        0
    })
});

game_export_int!(lynxer_game_update_sprite_list, args, {
    let list_index = args.int(0);
    with(|state| {
        let dt = state.dt as f32;
        let entries = match state.lists.get(list_index.max(0) as usize) {
            Some(entries) => entries.clone(),
            None => return 0,
        };
        for entry in entries {
            if let Some(sprite) = state.sprite_mut(entry) {
                sprite.x += sprite.vx * dt;
                sprite.y += sprite.vy * dt;
                sprite.angle += sprite.angular_velocity * dt;
            }
        }
        0
    })
});
