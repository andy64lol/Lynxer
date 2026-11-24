//! Project configuration (wynxer.config).

#[derive(Debug, Clone)]
pub struct Project {
    pub name: String,
    pub version: String,
}

impl Project {
    pub fn default() -> Self {
        Self { name: "wynxer-project".into(), version: "0.1.0".into() }
    }
}
