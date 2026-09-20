//! Clynxer `sqldb` stdlib backend: SQLite database operations.
//!
//! Replaces Python's sqlite3 with Rust `rusqlite`.
//! The Lynxer-facing contract matches `lynxer/stdlib/sqldb.lynx`:
//! structured results are returned as JSON strings, errors as
//! `"ERROR: <message>"`.

use clynxer_abi::{export_int, export_string, lynxer_module};
use rusqlite::{Connection, Result as SqlResult, params};
use std::path::Path;
use std::sync::Mutex;

// --- Handle registry -------------------------------------------------------
// Each entry holds an open Connection. Handles are integer indices.

struct SqlDbState {
    connections: Vec<Option<Connection>>,
}

impl SqlDbState {
    fn new() -> Self {
        SqlDbState {
            connections: Vec::new(),
        }
    }
}

thread_local! {
    static STATE: Mutex<Option<SqlDbState>> = Mutex::new(None);
}

fn with_state<F: FnOnce(&mut SqlDbState) -> R, R>(f: F) -> R {
    STATE.with(|cell| {
        let mut guard = cell.lock().unwrap();
        if guard.is_none() {
            *guard = Some(SqlDbState::new());
        }
        f(guard.as_mut().unwrap())
    })
}

// --- Ops -------------------------------------------------------------------

// Open a database and return a handle, or -1 on error.
export_int!(sqldb_open, args, {
    let path = args.string(0).to_string();
    with_state(|state| {
        match Connection::open(Path::new(&path)) {
            Ok(conn) => {
                let idx = state.connections.len();
                state.connections.push(Some(conn));
                idx as i64
            }
            Err(_) => -1,
        }
    })
});

// Execute one SQL statement and commit. Returns "ok" or "ERROR: <message>".
export_string!(sqldb_execute, args, {
    let idx = args.int(0) as usize;
    let sql = args.string(1).to_string();
    with_state(|state| {
        let conn = match state.connections.get(idx).and_then(|c| c.as_ref()) {
            Some(c) => c,
            None => return "ERROR: invalid handle".to_string(),
        };
        match conn.execute(&sql, []) {
            Ok(_) => "ok".to_string(),
            Err(e) => format!("ERROR: {}", e),
        }
    })
});

// Execute one parameterized SQL statement. paramsJson must be a JSON array.
// Returns "ok" or "ERROR: <message>".
export_string!(sqldb_execute_args, args, {
    let idx = args.int(0) as usize;
    let sql = args.string(1).to_string();
    let params_json = args.string(2);
    with_state(|state| {
        let conn = match state.connections.get(idx).and_then(|c| c.as_ref()) {
            Some(c) => c,
            None => return "ERROR: invalid handle".to_string(),
        };
        let params: Vec<rusqlite::types::Value> = match parse_params_json(params_json) {
            Ok(p) => p,
            Err(e) => return format!("ERROR: {}", e),
        };
        match conn.execute(&sql, rusqlite::params_from_iter(params)) {
            Ok(_) => "ok".to_string(),
            Err(e) => format!("ERROR: {}", e),
        }
    })
});

// Execute multiple SQL statements as one transaction. Returns "ok" or "ERROR: <message>".
export_string!(sqldb_script, args, {
    let idx = args.int(0) as usize;
    let script = args.string(1).to_string();
    with_state(|state| {
        let conn = match state.connections.get(idx).and_then(|c| c.as_ref()) {
            Some(c) => c,
            None => return "ERROR: invalid handle".to_string(),
        };
        match conn.execute_batch(&script) {
            Ok(_) => "ok".to_string(),
            Err(e) => format!("ERROR: {}", e),
        }
    })
});

// Query rows and return a JSON array of objects.
export_string!(sqldb_query, args, {
    let idx = args.int(0) as usize;
    let sql = args.string(1).to_string();
    with_state(|state| {
        let conn = match state.connections.get(idx).and_then(|c| c.as_ref()) {
            Some(c) => c,
            None => return "ERROR: invalid handle".to_string(),
        };
        let mut stmt = match conn.prepare(&sql) {
            Ok(s) => s,
            Err(e) => return format!("ERROR: {}", e),
        };
        let columns: Vec<String> = stmt
            .column_names()
            .iter()
            .map(|name| name.to_string())
            .collect();
        let rows = match stmt.query_map([], |row| {
            let mut map = serde_json::Map::new();
            for (i, col) in columns.iter().enumerate() {
                let value = match row.get::<usize, rusqlite::types::Value>(i) {
                    Ok(v) => value_to_json(v),
                    Err(_) => serde_json::Value::Null,
                };
                map.insert(col.to_string(), value);
            }
            Ok(serde_json::Value::Object(map))
        }) {
            Ok(r) => r,
            Err(e) => return format!("ERROR: {}", e),
        };
        let mut result = Vec::new();
        for row in rows {
            match row {
                Ok(v) => result.push(v),
                Err(e) => return format!("ERROR: {}", e),
            }
        }
        serde_json::to_string(&result).unwrap_or_else(|_| "[]".to_string())
    })
});

// Parameterized form of query().
export_string!(sqldb_query_args, args, {
    let idx = args.int(0) as usize;
    let sql = args.string(1).to_string();
    let params_json = args.string(2);
    with_state(|state| {
        let conn = match state.connections.get(idx).and_then(|c| c.as_ref()) {
            Some(c) => c,
            None => return "ERROR: invalid handle".to_string(),
        };
        let params: Vec<rusqlite::types::Value> = match parse_params_json(params_json) {
            Ok(p) => p,
            Err(e) => return format!("ERROR: {}", e),
        };
        let mut stmt = match conn.prepare(&sql) {
            Ok(s) => s,
            Err(e) => return format!("ERROR: {}", e),
        };
        let columns: Vec<String> = stmt
            .column_names()
            .iter()
            .map(|name| name.to_string())
            .collect();
        let rows = match stmt.query_map(rusqlite::params_from_iter(params), |row| {
            let mut map = serde_json::Map::new();
            for (i, col) in columns.iter().enumerate() {
                let value = match row.get::<usize, rusqlite::types::Value>(i) {
                    Ok(v) => value_to_json(v),
                    Err(_) => serde_json::Value::Null,
                };
                map.insert(col.to_string(), value);
            }
            Ok(serde_json::Value::Object(map))
        }) {
            Ok(r) => r,
            Err(e) => return format!("ERROR: {}", e),
        };
        let mut result = Vec::new();
        for row in rows {
            match row {
                Ok(v) => result.push(v),
                Err(e) => return format!("ERROR: {}", e),
            }
        }
        serde_json::to_string(&result).unwrap_or_else(|_| "[]".to_string())
    })
});

// Return the first column of the first row as a string, or "" when absent.
export_string!(sqldb_scalar, args, {
    let idx = args.int(0) as usize;
    let sql = args.string(1).to_string();
    with_state(|state| {
        let conn = match state.connections.get(idx).and_then(|c| c.as_ref()) {
            Some(c) => c,
            None => return "ERROR: invalid handle".to_string(),
        };
        match conn.query_row(&sql, [], |row| row.get::<_, String>(0)) {
            Ok(v) => v,
            Err(_) => "".to_string(),
        }
    })
});

// Parameterized form of scalar().
export_string!(sqldb_scalar_args, args, {
    let idx = args.int(0) as usize;
    let sql = args.string(1).to_string();
    let params_json = args.string(2);
    with_state(|state| {
        let conn = match state.connections.get(idx).and_then(|c| c.as_ref()) {
            Some(c) => c,
            None => return "ERROR: invalid handle".to_string(),
        };
        let params: Vec<rusqlite::types::Value> = match parse_params_json(params_json) {
            Ok(p) => p,
            Err(e) => return format!("ERROR: {}", e),
        };
        match conn.query_row(&sql, rusqlite::params_from_iter(params), |row| row.get::<_, String>(0)) {
            Ok(v) => v,
            Err(_) => "".to_string(),
        }
    })
});

// Execute an insert/update and return SQLite's lastrowid as an integer.
// Returns -1 on error.
export_int!(sqldb_last_insert_id, args, {
    let idx = args.int(0) as usize;
    let sql = args.string(1).to_string();
    let params_json = args.string(2);
    with_state(|state| {
        let conn = match state.connections.get(idx).and_then(|c| c.as_ref()) {
            Some(c) => c,
            None => return -1,
        };
        let params: Vec<rusqlite::types::Value> = match parse_params_json(params_json) {
            Ok(p) => p,
            Err(_) => return -1,
        };
        match conn.execute(&sql, rusqlite::params_from_iter(params)) {
            Ok(_) => {
                match conn.last_insert_rowid() {
                    id if id > 0 => id,
                    _ => -1,
                }
            }
            Err(_) => -1,
        }
    })
});

// Return whether a table exists in the database.
export_int!(sqldb_table_exists, args, {
    let idx = args.int(0) as usize;
    let table_name = args.string(1);
    with_state(|state| {
        let conn = match state.connections.get(idx).and_then(|c| c.as_ref()) {
            Some(c) => c,
            None => return 0,
        };
        match conn.query_row(
            "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?",
            rusqlite::params![table_name],
            |_| Ok(()),
        ) {
            Ok(_) => 1,
            Err(_) => 0,
        }
    })
});

// Return table names as a JSON array.
export_string!(sqldb_tables, args, {
    let idx = args.int(0) as usize;
    with_state(|state| {
        let conn = match state.connections.get(idx).and_then(|c| c.as_ref()) {
            Some(c) => c,
            None => return "ERROR: invalid handle".to_string(),
        };
        let mut stmt = match conn.prepare(
            "SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' ORDER BY name"
        ) {
            Ok(s) => s,
            Err(e) => return format!("ERROR: {}", e),
        };
        let rows = match stmt.query_map([], |row| row.get::<_, String>(0)) {
            Ok(r) => r,
            Err(e) => return format!("ERROR: {}", e),
        };
        let mut names = Vec::new();
        for row in rows {
            match row {
                Ok(name) => names.push(serde_json::Value::String(name)),
                Err(e) => return format!("ERROR: {}", e),
            }
        }
        serde_json::to_string(&names).unwrap_or_else(|_| "[]".to_string())
    })
});

// --- Helpers ---------------------------------------------------------------

fn parse_params_json(json: &str) -> Result<Vec<rusqlite::types::Value>, String> {
    let parsed: Vec<serde_json::Value> = serde_json::from_str(json)
        .map_err(|e| format!("paramsJson must contain a JSON array: {}", e))?;
    let mut params = Vec::new();
    for value in parsed {
        params.push(match value {
            serde_json::Value::Null => rusqlite::types::Value::Null,
            serde_json::Value::Bool(b) => {
                rusqlite::types::Value::Integer(b as i64)
            }
            serde_json::Value::Number(n) => {
                if let Some(i) = n.as_i64() {
                    rusqlite::types::Value::Integer(i)
                } else if let Some(f) = n.as_f64() {
                    rusqlite::types::Value::Real(f)
                } else {
                    return Err("Invalid number".to_string());
                }
            }
            serde_json::Value::String(s) => {
                rusqlite::types::Value::Text(s)
            }
            _ => return Err("Unsupported parameter type".to_string()),
        });
    }
    Ok(params)
}

fn value_to_json(value: rusqlite::types::Value) -> serde_json::Value {
    match value {
        rusqlite::types::Value::Null => serde_json::Value::Null,
        rusqlite::types::Value::Integer(i) => serde_json::Value::Number(
            serde_json::Number::from(i),
        ),
        rusqlite::types::Value::Real(f) => {
            serde_json::Number::from_f64(f).map_or(serde_json::Value::Null, |n| {
                serde_json::Value::Number(n)
            })
        }
        rusqlite::types::Value::Text(s) => serde_json::Value::String(s),
        rusqlite::types::Value::Blob(b) => {
            // Encode blobs as base64 strings for JSON compatibility.
            let b64 = base64::encode(&b);
            serde_json::Value::String(b64)
        }
    }
}

const OPS: &[(&str, &str, &str)] = &[
    ("open", "sqldb_open", "cdecl:int64(...)"),
    ("execute", "sqldb_execute", "cdecl:cstring(...)"),
    ("executeArgs", "sqldb_execute_args", "cdecl:cstring(...)"),
    ("script", "sqldb_script", "cdecl:cstring(...)"),
    ("query", "sqldb_query", "cdecl:cstring(...)"),
    ("queryArgs", "sqldb_query_args", "cdecl:cstring(...)"),
    ("scalar", "sqldb_scalar", "cdecl:cstring(...)"),
    ("scalarArgs", "sqldb_scalar_args", "cdecl:cstring(...)"),
    ("lastInsertId", "sqldb_last_insert_id", "cdecl:int64(...)"),
    ("tableExists", "sqldb_table_exists", "cdecl:int64(...)"),
    ("tables", "sqldb_tables", "cdecl:cstring(...)"),
];

lynxer_module!(OPS);
