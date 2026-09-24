//! Drawing primitives. All coordinates are bottom-left / +Y up in world space;
//! the helpers convert through the active camera and flip Y for macroquad.
//! Every op is a no-op in headless mode.
//!
//! Argument indices follow the Clynxer wrapper signature. Numbers and strings
//! arrive in separate arrays, so a string argument's index is independent of
//! the numeric ones.

use lynxer_abi::{export_int, export_string};
use macroquad::color::Color;
use macroquad::math::vec2;
use macroquad::shapes::{
    draw_circle, draw_circle_lines, draw_ellipse, draw_ellipse_lines, draw_line, draw_rectangle,
    draw_rectangle_lines, draw_triangle, draw_triangle_lines,
};
use macroquad::text::{draw_text, measure_text};
use macroquad::window::clear_background;

use crate::state::{color_of, parse_coords, with, State};

fn headless() -> bool {
    with(|state| state.headless)
}

fn background() -> Color {
    with(|state| state.background)
}

fn arc_outline(
    x: f32,
    y: f32,
    radius: f32,
    start_degrees: f32,
    end_degrees: f32,
    thickness: f32,
    color: Color,
) {
    let start = start_degrees.to_radians();
    let end = end_degrees.to_radians();
    let steps = (((end - start).abs() / 0.15).ceil() as i32).max(2);
    let mut previous: Option<(f32, f32)> = None;
    for step in 0..=steps {
        let angle = start + (end - start) * step as f32 / steps as f32;
        let vertex = (x + radius * angle.cos(), y - radius * angle.sin());
        if let Some(previous_vertex) = previous {
            draw_line(
                previous_vertex.0,
                previous_vertex.1,
                vertex.0,
                vertex.1,
                thickness,
                color,
            );
        }
        previous = Some(vertex);
    }
}

fn fill_arc(x: f32, y: f32, radius: f32, start_degrees: f32, end_degrees: f32, color: Color) {
    let start = start_degrees.to_radians();
    let end = end_degrees.to_radians();
    let steps = (((end - start).abs() / 0.15).ceil() as i32).max(2);
    let mut previous: Option<(f32, f32)> = None;
    for step in 0..=steps {
        let angle = start + (end - start) * step as f32 / steps as f32;
        let vertex = (x + radius * angle.cos(), y - radius * angle.sin());
        if let Some(previous_vertex) = previous {
            draw_triangle(
                vec2(x, y),
                vec2(previous_vertex.0, previous_vertex.1),
                vec2(vertex.0, vertex.1),
                color,
            );
        }
        previous = Some(vertex);
    }
}

fn draw_anchored_text(
    state: &State,
    text: &str,
    x: f32,
    y: f32,
    color: Color,
    size: f32,
    anchor_x: &str,
    anchor_y: &str,
) {
    let font_size = (size * state.screen_scale()).max(1.0);
    let (sx, sy) = state.screen_pos(x, y);
    let dimensions = measure_text(text, None, font_size as u16, 1.0);
    let offset_x = match anchor_x.to_ascii_lowercase().as_str() {
        "center" => dimensions.width / 2.0,
        "right" => dimensions.width,
        _ => 0.0,
    };
    let offset_y = match anchor_y.to_ascii_lowercase().as_str() {
        "top" => 0.0,
        "center" | "middle" => dimensions.height / 2.0,
        _ => dimensions.height,
    };
    draw_text(text, sx - offset_x, sy - offset_y, font_size, color);
}

export_int!(clynxer_game_begin_draw, args, {
    let _ = args;
    if !headless() {
        clear_background(background());
    }
    0
});

export_int!(clynxer_game_end_draw, args, {
    let _ = args;
    0
});

export_int!(clynxer_game_draw_rect, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let (sx, sy) = state.screen_pos(args.float(0), args.float(1));
        let w = args.float(2) * state.screen_scale();
        let h = args.float(3) * state.screen_scale();
        let color = color_of(args.int(4), args.int(5), args.int(6));
        draw_rectangle(sx - w / 2.0, sy - h / 2.0, w, h, color);
        0
    })
});

export_int!(clynxer_game_draw_rect_outline, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let (sx, sy) = state.screen_pos(args.float(0), args.float(1));
        let scale = state.screen_scale();
        let w = args.float(2) * scale;
        let h = args.float(3) * scale;
        let color = color_of(args.int(4), args.int(5), args.int(6));
        draw_rectangle_lines(
            sx - w / 2.0,
            sy - h / 2.0,
            w,
            h,
            args.float(7) * scale,
            color,
        );
        0
    })
});

export_int!(clynxer_game_draw_circle, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let (sx, sy) = state.screen_pos(args.float(0), args.float(1));
        draw_circle(
            sx,
            sy,
            args.float(2) * state.screen_scale(),
            color_of(args.int(3), args.int(4), args.int(5)),
        );
        0
    })
});

export_int!(clynxer_game_draw_circle_outline, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let (sx, sy) = state.screen_pos(args.float(0), args.float(1));
        let scale = state.screen_scale();
        draw_circle_lines(
            sx,
            sy,
            args.float(2) * scale,
            args.float(6) * scale,
            color_of(args.int(3), args.int(4), args.int(5)),
        );
        0
    })
});

export_int!(clynxer_game_draw_ellipse, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let (sx, sy) = state.screen_pos(args.float(0), args.float(1));
        let scale = state.screen_scale();
        draw_ellipse(
            sx,
            sy,
            args.float(2) * scale / 2.0,
            args.float(3) * scale / 2.0,
            0.0,
            color_of(args.int(4), args.int(5), args.int(6)),
        );
        0
    })
});

export_int!(clynxer_game_draw_ellipse_outline, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let (sx, sy) = state.screen_pos(args.float(0), args.float(1));
        let scale = state.screen_scale();
        draw_ellipse_lines(
            sx,
            sy,
            args.float(2) * scale / 2.0,
            args.float(3) * scale / 2.0,
            0.0,
            args.float(7) * scale,
            color_of(args.int(4), args.int(5), args.int(6)),
        );
        0
    })
});

export_int!(clynxer_game_draw_line, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let (x1, y1) = state.screen_pos(args.float(0), args.float(1));
        let (x2, y2) = state.screen_pos(args.float(2), args.float(3));
        draw_line(
            x1,
            y1,
            x2,
            y2,
            args.float(7) * state.screen_scale(),
            color_of(args.int(4), args.int(5), args.int(6)),
        );
        0
    })
});

export_int!(clynxer_game_draw_triangle, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let (x1, y1) = state.screen_pos(args.float(0), args.float(1));
        let (x2, y2) = state.screen_pos(args.float(2), args.float(3));
        let (x3, y3) = state.screen_pos(args.float(4), args.float(5));
        draw_triangle(
            vec2(x1, y1),
            vec2(x2, y2),
            vec2(x3, y3),
            color_of(args.int(6), args.int(7), args.int(8)),
        );
        0
    })
});

export_int!(clynxer_game_draw_triangle_outline, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let (x1, y1) = state.screen_pos(args.float(0), args.float(1));
        let (x2, y2) = state.screen_pos(args.float(2), args.float(3));
        let (x3, y3) = state.screen_pos(args.float(4), args.float(5));
        draw_triangle_lines(
            vec2(x1, y1),
            vec2(x2, y2),
            vec2(x3, y3),
            args.float(9) * state.screen_scale(),
            color_of(args.int(6), args.int(7), args.int(8)),
        );
        0
    })
});

export_int!(clynxer_game_draw_point, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let (sx, sy) = state.screen_pos(args.float(0), args.float(1));
        let size = args.float(5) * state.screen_scale();
        draw_rectangle(
            sx - size / 2.0,
            sy - size / 2.0,
            size,
            size,
            color_of(args.int(2), args.int(3), args.int(4)),
        );
        0
    })
});

export_int!(clynxer_game_draw_rect_rounded_filled, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let (sx, sy) = state.screen_pos(args.float(0), args.float(1));
        let scale = state.screen_scale();
        let w = args.float(2) * scale;
        let h = args.float(3) * scale;
        let radius = args.float(7) * scale;
        let color = color_of(args.int(4), args.int(5), args.int(6));
        draw_rectangle(
            sx - w / 2.0,
            sy - h / 2.0 + radius,
            w,
            (h - radius * 2.0).max(0.0),
            color,
        );
        draw_rectangle(
            sx - w / 2.0 + radius,
            sy - h / 2.0,
            (w - radius * 2.0).max(0.0),
            radius,
            color,
        );
        draw_rectangle(
            sx - w / 2.0 + radius,
            sy + h / 2.0 - radius,
            (w - radius * 2.0).max(0.0),
            radius,
            color,
        );
        for (ox, oy) in [(-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0)] {
            draw_circle(
                sx + ox * (w / 2.0 - radius),
                sy + oy * (h / 2.0 - radius),
                radius,
                color,
            );
        }
        0
    })
});

export_int!(clynxer_game_draw_rect_rounded_outline, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let (sx, sy) = state.screen_pos(args.float(0), args.float(1));
        let scale = state.screen_scale();
        let w = args.float(2) * scale;
        let h = args.float(3) * scale;
        let radius = args.float(7) * scale;
        let thickness = args.float(8) * scale;
        let color = color_of(args.int(4), args.int(5), args.int(6));
        let inner_w = (w - radius * 2.0).max(0.0);
        let inner_h = (h - radius * 2.0).max(0.0);
        draw_line(
            sx - w / 2.0 + radius,
            sy + h / 2.0,
            sx + w / 2.0 - radius,
            sy + h / 2.0,
            thickness,
            color,
        );
        draw_line(
            sx - w / 2.0 + radius,
            sy - h / 2.0,
            sx + w / 2.0 - radius,
            sy - h / 2.0,
            thickness,
            color,
        );
        draw_line(
            sx - w / 2.0,
            sy - h / 2.0 + radius,
            sx - w / 2.0,
            sy + h / 2.0 - radius,
            thickness,
            color,
        );
        draw_line(
            sx + w / 2.0,
            sy - h / 2.0 + radius,
            sx + w / 2.0,
            sy + h / 2.0 - radius,
            thickness,
            color,
        );
        let _ = (inner_w, inner_h);
        arc_outline(
            sx - w / 2.0 + radius,
            sy + h / 2.0 - radius,
            radius,
            90.0,
            180.0,
            thickness,
            color,
        );
        arc_outline(
            sx + w / 2.0 - radius,
            sy + h / 2.0 - radius,
            radius,
            0.0,
            90.0,
            thickness,
            color,
        );
        arc_outline(
            sx - w / 2.0 + radius,
            sy - h / 2.0 + radius,
            radius,
            180.0,
            270.0,
            thickness,
            color,
        );
        arc_outline(
            sx + w / 2.0 - radius,
            sy - h / 2.0 + radius,
            radius,
            270.0,
            360.0,
            thickness,
            color,
        );
        0
    })
});

export_int!(clynxer_game_draw_star, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let (cx, cy) = state.screen_pos(args.float(0), args.float(1));
        let scale = state.screen_scale();
        let outer = args.float(2) * scale;
        let inner = args.float(3) * scale;
        let points = args.int(4).max(2);
        let color = color_of(args.int(5), args.int(6), args.int(7));
        let step = std::f32::consts::PI / points as f32;
        let mut previous: Option<(f32, f32)> = None;
        let mut first: Option<(f32, f32)> = None;
        for index in 0..(points * 2) {
            let angle = index as f32 * step - std::f32::consts::PI / 2.0;
            let radius = if index % 2 == 0 { outer } else { inner };
            let vertex = (cx + radius * angle.cos(), cy - radius * angle.sin());
            if let Some(previous_vertex) = previous {
                draw_triangle(
                    vec2(cx, cy),
                    vec2(previous_vertex.0, previous_vertex.1),
                    vec2(vertex.0, vertex.1),
                    color,
                );
            } else {
                first = Some(vertex);
            }
            previous = Some(vertex);
        }
        if let (Some(previous_vertex), Some(first_vertex)) = (previous, first) {
            draw_triangle(
                vec2(cx, cy),
                vec2(previous_vertex.0, previous_vertex.1),
                vec2(first_vertex.0, first_vertex.1),
                color,
            );
        }
        0
    })
});

export_int!(clynxer_game_draw_dashed_line, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let (x1, y1) = state.screen_pos(args.float(0), args.float(1));
        let (x2, y2) = state.screen_pos(args.float(2), args.float(3));
        let scale = state.screen_scale();
        let thickness = args.float(7) * scale;
        let dash = args.float(8) * scale;
        let color = color_of(args.int(4), args.int(5), args.int(6));
        let (dx, dy) = (x2 - x1, y2 - y1);
        let distance = (dx * dx + dy * dy).sqrt();
        if distance <= 0.0 || dash <= 0.0 {
            return 0;
        }
        let (nx, ny) = (dx / distance, dy / distance);
        let mut position = 0.0;
        let mut draw_segment = true;
        while position < distance {
            let end = (position + dash).min(distance);
            if draw_segment {
                draw_line(
                    x1 + nx * position,
                    y1 + ny * position,
                    x1 + nx * end,
                    y1 + ny * end,
                    thickness,
                    color,
                );
            }
            position = end;
            draw_segment = !draw_segment;
        }
        0
    })
});

export_int!(clynxer_game_draw_cross, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let (cx, cy) = state.screen_pos(args.float(0), args.float(1));
        let size = args.float(2) * state.screen_scale();
        let thickness = args.float(6) * state.screen_scale();
        let color = color_of(args.int(3), args.int(4), args.int(5));
        draw_line(cx - size, cy, cx + size, cy, thickness, color);
        draw_line(cx, cy - size, cx, cy + size, thickness, color);
        0
    })
});

export_int!(clynxer_game_draw_gradient_rect, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let (sx, sy) = state.screen_pos(args.float(0), args.float(1));
        let scale = state.screen_scale();
        let w = args.float(2) * scale;
        let h = args.float(3) * scale;
        draw_rectangle(
            sx - w / 2.0,
            sy,
            w,
            h / 2.0,
            color_of(args.int(7), args.int(8), args.int(9)),
        );
        draw_rectangle(
            sx - w / 2.0,
            sy - h / 2.0,
            w,
            h / 2.0,
            color_of(args.int(4), args.int(5), args.int(6)),
        );
        0
    })
});

export_int!(clynxer_game_draw_arc, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let (sx, sy) = state.screen_pos(args.float(0), args.float(1));
        let radius = args.float(2) * state.screen_scale() / 2.0;
        arc_outline(
            sx,
            sy,
            radius,
            args.float(7),
            args.float(8),
            args.float(9) * state.screen_scale(),
            color_of(args.int(4), args.int(5), args.int(6)),
        );
        0
    })
});

export_int!(clynxer_game_draw_arc_filled, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let (sx, sy) = state.screen_pos(args.float(0), args.float(1));
        let radius = args.float(2) * state.screen_scale() / 2.0;
        fill_arc(
            sx,
            sy,
            radius,
            args.float(7),
            args.float(8),
            color_of(args.int(4), args.int(5), args.int(6)),
        );
        0
    })
});

export_int!(clynxer_game_draw_text, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let text = args.string(0).to_string();
        draw_anchored_text(
            state,
            &text,
            args.float(0),
            args.float(1),
            color_of(args.int(2), args.int(3), args.int(4)),
            args.float(5),
            "left",
            "bottom",
        );
        0
    })
});

export_int!(clynxer_game_draw_text_styled, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let text = args.string(0).to_string();
        draw_anchored_text(
            state,
            &text,
            args.float(0),
            args.float(1),
            color_of(args.int(2), args.int(3), args.int(4)),
            args.float(5),
            args.string(2),
            "bottom",
        );
        0
    })
});

export_int!(clynxer_game_draw_text_anchored, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let text = args.string(0).to_string();
        draw_anchored_text(
            state,
            &text,
            args.float(0),
            args.float(1),
            color_of(args.int(2), args.int(3), args.int(4)),
            args.float(5),
            args.string(1),
            args.string(2),
        );
        0
    })
});

export_int!(clynxer_game_draw_polygon, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let values = parse_coords(args.string(0));
        let color = color_of(args.int(0), args.int(1), args.int(2));
        if values.len() < 6 {
            return 0;
        }
        let screen: Vec<(f32, f32)> = values
            .chunks_exact(2)
            .map(|pair| state.screen_pos(pair[0], pair[1]))
            .collect();
        for index in 1..screen.len().saturating_sub(1) {
            draw_triangle(
                vec2(screen[0].0, screen[0].1),
                vec2(screen[index].0, screen[index].1),
                vec2(screen[index + 1].0, screen[index + 1].1),
                color,
            );
        }
        0
    })
});

export_int!(clynxer_game_draw_polygon_outline, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let values = parse_coords(args.string(0));
        let color = color_of(args.int(0), args.int(1), args.int(2));
        let thickness = args.float(3) * state.screen_scale();
        let screen: Vec<(f32, f32)> = values
            .chunks_exact(2)
            .map(|pair| state.screen_pos(pair[0], pair[1]))
            .collect();
        if screen.len() >= 2 {
            for index in 0..screen.len() {
                let next = (index + 1) % screen.len();
                draw_line(
                    screen[index].0,
                    screen[index].1,
                    screen[next].0,
                    screen[next].1,
                    thickness,
                    color,
                );
            }
        }
        0
    })
});

export_int!(clynxer_game_draw_polyline, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let values = parse_coords(args.string(0));
        let color = color_of(args.int(0), args.int(1), args.int(2));
        let thickness = args.float(3) * state.screen_scale();
        let screen: Vec<(f32, f32)> = values
            .chunks_exact(2)
            .map(|pair| state.screen_pos(pair[0], pair[1]))
            .collect();
        for index in 0..screen.len().saturating_sub(1) {
            draw_line(
                screen[index].0,
                screen[index].1,
                screen[index + 1].0,
                screen[index + 1].1,
                thickness,
                color,
            );
        }
        0
    })
});

export_int!(clynxer_game_draw_points, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let values = parse_coords(args.string(0));
        let color = color_of(args.int(0), args.int(1), args.int(2));
        let size = args.float(3) * state.screen_scale();
        for pair in values.chunks_exact(2) {
            let (sx, sy) = state.screen_pos(pair[0], pair[1]);
            draw_rectangle(sx - size / 2.0, sy - size / 2.0, size, size, color);
        }
        0
    })
});

export_int!(clynxer_game_draw_lines, args, {
    if headless() {
        return 0;
    }
    with(|state| {
        let values = parse_coords(args.string(0));
        let color = color_of(args.int(0), args.int(1), args.int(2));
        let thickness = args.float(3) * state.screen_scale();
        for segment in values.chunks_exact(4) {
            let (x1, y1) = state.screen_pos(segment[0], segment[1]);
            let (x2, y2) = state.screen_pos(segment[2], segment[3]);
            draw_line(x1, y1, x2, y2, thickness, color);
        }
        0
    })
});

export_string!(clynxer_game_screen_to_tile, args, {
    let size = args.float(2);
    if size <= 0.0 {
        "0,0".to_string()
    } else {
        format!(
            "{},{}",
            (args.float(0) / size).floor() as i64,
            (args.float(1) / size).floor() as i64
        )
    }
});

export_string!(clynxer_game_tile_to_screen, args, {
    let size = args.float(2);
    let center = size / 2.0;
    format!(
        "{},{}",
        args.float(0) * size + center,
        args.float(1) * size + center
    )
});
