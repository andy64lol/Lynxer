//! Project configuration (wynxer.config).

#[derive(Debug, Clone)]
pub struct Project {
    pub name: String,
    pub version: String,
}

impl Default for Project {
    fn default() -> Self {
        Self {
            name: "wynxer-project".into(),
            version: "0.1.0".into(),
        }
    }
}
