//! Turtle graphics: a position, a heading and a pen over the drawing ops.
//!
//! A turtle is a handle. Handle 0 is an implicit default turtle created on
//! first use, which the pure-Lynxer `turtle` module wraps into the classic
//! no-handle API; `create` returns handles from 1 up. The heading is in degrees,
//! `0` points along `+x` (east) and it increases clockwise, matching the
//! screen's y-down coordinate system. Moving with the pen down draws a line
//! through the ordinary shape ops, so it works on the headless rasterizer too.

use macroquad::color::Color;

use crate::state::{with, Turtle};

fn default_turtle() -> Turtle {
    Turtle {
        x: 0.0,
        y: 0.0,
        heading: 0.0,
        pen_down: true,
        color: Color::new(0.0, 0.0, 0.0, 1.0),
        width: 1.0,
    }
}

/// Runs `body` on the turtle for `handle`. Handle `< 1` selects the implicit
/// default turtle (created on demand); a missing explicit handle returns `None`.
fn with_turtle<R>(handle: i64, body: impl FnOnce(&mut Turtle) -> R) -> Option<R> {
    with(|state| {
        let index = if handle < 1 { 0usize } else { handle as usize };
        if state.turtles.len() <= index {
            state.turtles.resize_with(index + 1, || None);
        }
        if state.turtles[index].is_none() {
            if handle >= 1 {
                return None;
            }
            state.turtles[index] = Some(default_turtle());
        }
        state.turtles[index].as_mut().map(body)
    })
}

pub fn create(x: f32, y: f32, heading: f32) -> i64 {
    with(|state| {
        state.turtles.push(Some(Turtle {
            x,
            y,
            heading,
            ..default_turtle()
        }));
        (state.turtles.len() - 1) as i64
    })
}

/// The new position after moving `distance` along the current heading.
fn step(turtle: &Turtle, distance: f32) -> (f32, f32) {
    let radians = turtle.heading.to_radians();
    (
        turtle.x + distance * radians.cos(),
        turtle.y + distance * radians.sin(),
    )
}

/// A pen stroke: from, to, width and colour. Produced while the turtle state is
/// borrowed and drawn afterwards, because drawing re-borrows the module state.
type Stroke = (f32, f32, f32, f32, f32, Color);

fn draw(stroke: Option<Stroke>) -> i64 {
    if let Some((x1, y1, x2, y2, width, color)) = stroke {
        crate::shapes::line(x1, y1, x2, y2, width, color);
    }
    0
}

pub fn move_by(handle: i64, distance: f32) -> i64 {
    let stroke = with_turtle(handle, |turtle| {
        let (next_x, next_y) = step(turtle, distance);
        let stroke = if turtle.pen_down {
            Some((
                turtle.x,
                turtle.y,
                next_x,
                next_y,
                turtle.width,
                turtle.color,
            ))
        } else {
            None
        };
        turtle.x = next_x;
        turtle.y = next_y;
        stroke
    });
    match stroke {
        Some(stroke) => draw(stroke),
        None => -1,
    }
}

fn move_to(turtle: &mut Turtle, x: f32, y: f32) -> Option<Stroke> {
    let stroke = if turtle.pen_down {
        Some((turtle.x, turtle.y, x, y, turtle.width, turtle.color))
    } else {
        None
    };
    turtle.x = x;
    turtle.y = y;
    stroke
}

pub fn goto(handle: i64, x: f32, y: f32) -> i64 {
    match with_turtle(handle, |turtle| move_to(turtle, x, y)) {
        Some(stroke) => draw(stroke),
        None => -1,
    }
}

pub fn turn(handle: i64, degrees: f32) -> i64 {
    match with_turtle(handle, |turtle| {
        turtle.heading += degrees;
        0
    }) {
        Some(status) => status,
        None => -1,
    }
}

pub fn home(handle: i64) -> i64 {
    let stroke = with_turtle(handle, |turtle| {
        let stroke = move_to(turtle, 0.0, 0.0);
        turtle.heading = 0.0;
        stroke
    });
    match stroke {
        Some(stroke) => draw(stroke),
        None => -1,
    }
}

pub fn set_pen(handle: i64, down: bool) -> i64 {
    match with_turtle(handle, |turtle| {
        turtle.pen_down = down;
        0
    }) {
        Some(status) => status,
        None => -1,
    }
}

pub fn set_color(handle: i64, color: Color) -> i64 {
    match with_turtle(handle, |turtle| {
        turtle.color = color;
        0
    }) {
        Some(status) => status,
        None => -1,
    }
}

pub fn set_width(handle: i64, width: f32) -> i64 {
    match with_turtle(handle, |turtle| {
        turtle.width = width.max(0.0);
        0
    }) {
        Some(status) => status,
        None => -1,
    }
}

pub fn position(handle: i64) -> Option<(f32, f32)> {
    with_turtle(handle, |turtle| (turtle.x, turtle.y))
}

pub fn heading(handle: i64) -> f32 {
    with_turtle(handle, |turtle| turtle.heading).unwrap_or(0.0)
}

pub fn pen_is_down(handle: i64) -> bool {
    with_turtle(handle, |turtle| turtle.pen_down).unwrap_or(false)
}

pub fn remove(handle: i64) -> bool {
    if handle < 1 {
        return false;
    }
    with(|state| match state.turtles.get_mut(handle as usize) {
        Some(slot) => slot.take().is_some(),
        None => false,
    })
}
