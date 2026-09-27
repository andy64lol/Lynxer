//! Custom materials (GLSL shaders) and their uniforms.
//!
//! `load_material` compiles on the GPU, so it only works with a live context and
//! returns `None` in headless mode.

use macroquad::material::{
    gl_use_default_material, gl_use_material, load_material, Material, MaterialParams,
};
use macroquad::miniquad::ShaderSource;
use macroquad::texture::Texture2D;

use crate::state::with;

fn headless() -> bool {
    with(|state| state.headless)
}

pub fn load(vertex: &str, fragment: &str) -> Option<Material> {
    if headless() || vertex.is_empty() || fragment.is_empty() {
        return None;
    }
    load_material(
        ShaderSource::Glsl { vertex, fragment },
        MaterialParams {
            pipeline_params: Default::default(),
            uniforms: Vec::new(),
            textures: Vec::new(),
        },
    )
    .ok()
}

pub fn load_from_files(vertex_path: &str, fragment_path: &str) -> Option<Material> {
    if headless() {
        return None;
    }
    let vertex = std::fs::read_to_string(vertex_path).ok()?;
    let fragment = std::fs::read_to_string(fragment_path).ok()?;
    load(&vertex, &fragment)
}

pub fn use_material(material: &Material) {
    if headless() {
        return;
    }
    gl_use_material(material);
}

pub fn use_default() {
    if headless() {
        return;
    }
    gl_use_default_material();
}

pub fn set_uniform(material: &Material, name: &str, value: f32) {
    if headless() {
        return;
    }
    material.set_uniform(name, value);
}

pub fn set_uniform_array(material: &Material, name: &str, values: &[f32]) {
    if headless() || values.is_empty() {
        return;
    }
    material.set_uniform_array(name, values);
}

pub fn set_texture(material: &Material, name: &str, texture: Texture2D) {
    if headless() {
        return;
    }
    material.set_texture(name, texture);
}
