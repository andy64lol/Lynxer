//! Lynxer `sqldb` stdlib backend: SQLite database operations.
//!
//! Replaces Python's sqlite3 with Rust `rusqlite`. The Lynxer-facing contract
//! matches `lynxer/stdlib/sqldb.lynx`: an operation either names a database
//! **path** — a connection is opened for that call and closed after it — or a
//! **handle** from `open()`, which reuses one live connection until `close()`.
//! Structured results are JSON strings, errors are `"ERROR: <message>"`.

use base64::engine::general_purpose::STANDARD;
use base64::Engine as _;
use lynxer_abi::{export_int, export_string, lynxer_module};
use rusqlite::Connection;
use std::collections::HashMap;
use std::path::Path;
use std::sync::atomic::{AtomicI64, Ordering};
use std::sync::{Mutex, OnceLock};

// --- Connection handling ---------------------------------------------------

/// Open connections, keyed by the handle `open()` returned.
///
/// A handle stays valid for the whole run (the registries in `graphics`,
/// `image` and `watch` work the same way) and is released by `close()`. Every
/// handle op takes the registry lock for the duration of its call, so two
/// Lynxer threads cannot interleave statements on one connection.
fn registry() -> &'static Mutex<HashMap<i64, Connection>> {
    static REGISTRY: OnceLock<Mutex<HashMap<i64, Connection>>> = OnceLock::new();
    REGISTRY.get_or_init(|| Mutex::new(HashMap::new()))
}

/// Handles are monotonic, so a stale handle can never name a newer connection.
fn next_handle() -> i64 {
    static NEXT: AtomicI64 = AtomicI64::new(1);
    NEXT.fetch_add(1, Ordering::Relaxed)
}

/// Opens `path` for the duration of `f` and closes it (on drop) afterwards.
/// Everything a connection can fail with becomes an `"ERROR: …"` message.
fn with_path<T, F>(path: &str, f: F) -> Result<T, String>
where
    F: FnOnce(&Connection) -> Result<T, String>,
{
    let conn = Connection::open(Path::new(path)).map_err(|e| e.to_string())?;
    f(&conn)
}

/// Runs `f` on the connection behind `handle`, or reports an unknown handle.
fn with_handle<T, F>(handle: i64, f: F) -> Result<T, String>
where
    F: FnOnce(&Connection) -> Result<T, String>,
{
    let guard = registry()
        .lock()
        .map_err(|_| "the connection registry is poisoned".to_string())?;
    match guard.get(&handle) {
        Some(conn) => f(conn),
        None => Err(format!("unknown connection handle {}", handle)),
    }
}

/// Renders a failed call the way every op does.
fn error_text(message: String) -> String {
    format!("ERROR: {}", message)
}

// --- Operations over one connection ----------------------------------------

fn op_execute(conn: &Connection, sql: &str) -> Result<String, String> {
    conn.execute(sql, []).map_err(|e| e.to_string())?;
    Ok("ok".to_string())
}

fn op_execute_args(conn: &Connection, sql: &str, params_json: &str) -> Result<String, String> {
    let params = parse_params_json(params_json)?;
    conn.execute(sql, rusqlite::params_from_iter(params))
        .map_err(|e| e.to_string())?;
    Ok("ok".to_string())
}

fn op_script(conn: &Connection, script: &str) -> Result<String, String> {
    conn.execute_batch(script).map_err(|e| e.to_string())?;
    Ok("ok".to_string())
}

fn op_query(
    conn: &Connection,
    sql: &str,
    params_json: Option<&str>,
) -> Result<String, String> {
    let params = match params_json {
        Some(json) => parse_params_json(json)?,
        None => Vec::new(),
    };
    query_rows(conn, sql, params).map_err(|e| e.to_string())
}

fn op_scalar(
    conn: &Connection,
    sql: &str,
    params_json: Option<&str>,
) -> Result<String, String> {
    let params = match params_json {
        Some(json) => parse_params_json(json)?,
        None => Vec::new(),
    };
    match conn.query_row(sql, rusqlite::params_from_iter(params), |row| {
        row.get::<usize, rusqlite::types::Value>(0)
    }) {
        Ok(value) => Ok(value_to_scalar(value)),
        // An absent row is "" rather than an error, matching the reference.
        Err(rusqlite::Error::QueryReturnedNoRows) => Ok(String::new()),
        Err(e) => Err(e.to_string()),
    }
}

fn op_last_insert_id(
    conn: &Connection,
    sql: &str,
    params_json: &str,
) -> Result<i64, String> {
    let params = parse_params_json(params_json)?;
    conn.execute(sql, rusqlite::params_from_iter(params))
        .map_err(|e| e.to_string())?;
    let id = conn.last_insert_rowid();
    Ok(if id > 0 { id } else { -1 })
}

fn op_table_exists(conn: &Connection, table_name: &str) -> Result<bool, String> {
    let found = conn.query_row(
        "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?",
        rusqlite::params![table_name],
        |_| Ok(()),
    );
    Ok(found.is_ok())
}

// --- Path-based ops --------------------------------------------------------

export_string!(sqldb_execute, args, {
    match with_path(args.string(0), |conn| op_execute(conn, args.string(1))) {
        Ok(value) => value,
        Err(e) => error_text(e),
    }
});

export_string!(sqldb_execute_args, args, {
    match with_path(args.string(0), |conn| {
        op_execute_args(conn, args.string(1), args.string(2))
    }) {
        Ok(value) => value,
        Err(e) => error_text(e),
    }
});

export_string!(sqldb_script, args, {
    match with_path(args.string(0), |conn| op_script(conn, args.string(1))) {
        Ok(value) => value,
        Err(e) => error_text(e),
    }
});

export_string!(sqldb_query, args, {
    match with_path(args.string(0), |conn| op_query(conn, args.string(1), None)) {
        Ok(value) => value,
        Err(e) => error_text(e),
    }
});

export_string!(sqldb_query_args, args, {
    match with_path(args.string(0), |conn| {
        op_query(conn, args.string(1), Some(args.string(2)))
    }) {
        Ok(value) => value,
        Err(e) => error_text(e),
    }
});

export_string!(sqldb_scalar, args, {
    match with_path(args.string(0), |conn| op_scalar(conn, args.string(1), None)) {
        Ok(value) => value,
        Err(e) => error_text(e),
    }
});

export_string!(sqldb_scalar_args, args, {
    match with_path(args.string(0), |conn| {
        op_scalar(conn, args.string(1), Some(args.string(2)))
    }) {
        Ok(value) => value,
        Err(e) => error_text(e),
    }
});

export_int!(sqldb_last_insert_id, args, {
    match with_path(args.string(0), |conn| {
        op_last_insert_id(conn, args.string(1), args.string(2))
    }) {
        Ok(value) => value,
        Err(_) => -1,
    }
});

export_int!(sqldb_table_exists, args, {
    match with_path(args.string(0), |conn| op_table_exists(conn, args.string(1))) {
        Ok(value) => value as i64,
        Err(_) => 0,
    }
});

export_string!(sqldb_tables, args, {
    match with_path(args.string(0), |conn| list_tables(conn).map_err(|e| e.to_string())) {
        Ok(value) => value,
        Err(e) => error_text(e),
    }
});

// --- Handle ops ------------------------------------------------------------
//
// The packed ABI delivers numbers and strings in two independent lists, so the
// handle is `args.int(0)` and the strings keep their own indices.

export_int!(sqldb_open, args, {
    match Connection::open(Path::new(args.string(0))) {
        Ok(conn) => match registry().lock() {
            Ok(mut entries) => {
                let handle = next_handle();
                entries.insert(handle, conn);
                handle
            }
            Err(_) => -1,
        },
        Err(_) => -1,
    }
});

export_int!(sqldb_close, args, {
    match registry().lock() {
        Ok(mut entries) => entries.remove(&args.int(0)).is_some() as i64,
        Err(_) => 0,
    }
});

export_string!(sqldb_execute_on, args, {
    match with_handle(args.int(0), |conn| op_execute(conn, args.string(0))) {
        Ok(value) => value,
        Err(e) => error_text(e),
    }
});

export_string!(sqldb_execute_args_on, args, {
    match with_handle(args.int(0), |conn| {
        op_execute_args(conn, args.string(0), args.string(1))
    }) {
        Ok(value) => value,
        Err(e) => error_text(e),
    }
});

export_string!(sqldb_script_on, args, {
    match with_handle(args.int(0), |conn| op_script(conn, args.string(0))) {
        Ok(value) => value,
        Err(e) => error_text(e),
    }
});

export_string!(sqldb_query_on, args, {
    match with_handle(args.int(0), |conn| op_query(conn, args.string(0), None)) {
        Ok(value) => value,
        Err(e) => error_text(e),
    }
});

export_string!(sqldb_query_args_on, args, {
    match with_handle(args.int(0), |conn| {
        op_query(conn, args.string(0), Some(args.string(1)))
    }) {
        Ok(value) => value,
        Err(e) => error_text(e),
    }
});

export_string!(sqldb_scalar_on, args, {
    match with_handle(args.int(0), |conn| op_scalar(conn, args.string(0), None)) {
        Ok(value) => value,
        Err(e) => error_text(e),
    }
});

export_string!(sqldb_scalar_args_on, args, {
    match with_handle(args.int(0), |conn| {
        op_scalar(conn, args.string(0), Some(args.string(1)))
    }) {
        Ok(value) => value,
        Err(e) => error_text(e),
    }
});

export_int!(sqldb_last_insert_id_on, args, {
    match with_handle(args.int(0), |conn| {
        op_last_insert_id(conn, args.string(0), args.string(1))
    }) {
        Ok(value) => value,
        Err(_) => -1,
    }
});

export_int!(sqldb_table_exists_on, args, {
    match with_handle(args.int(0), |conn| op_table_exists(conn, args.string(0))) {
        Ok(value) => value as i64,
        Err(_) => 0,
    }
});

export_string!(sqldb_tables_on, args, {
    match with_handle(args.int(0), |conn| list_tables(conn).map_err(|e| e.to_string())) {
        Ok(value) => value,
        Err(e) => error_text(e),
    }
});

// --- Helpers ---------------------------------------------------------------

/// Serialises `value` the way Python's `json.dumps` does by default: compact,
/// but with one space after every `:` and `,` outside a string. `sqldb`'s
/// reference output goes through `json.dumps`, so byte-identical output needs
/// the separators to match. `rust/json` duplicates this helper for the same
/// reason; each `.so` stays self-contained.
fn json_dumps(value: &serde_json::Value) -> String {
    let raw = serde_json::to_string(value).unwrap_or_else(|_| "null".to_string());
    let mut output = String::with_capacity(raw.len() + raw.len() / 8);
    let mut in_string = false;
    let mut escaped = false;
    for character in raw.chars() {
        if in_string {
            output.push(character);
            if escaped {
                escaped = false;
            } else if character == '\\' {
                escaped = true;
            } else if character == '"' {
                in_string = false;
            }
            continue;
        }
        match character {
            '"' => {
                in_string = true;
                output.push(character);
            }
            ':' | ',' => {
                output.push(character);
                output.push(' ');
            }
            _ => output.push(character),
        }
    }
    output
}

// Runs `sql` and renders every row as a JSON object keyed by column name.
fn query_rows(
    conn: &Connection,
    sql: &str,
    params: Vec<rusqlite::types::Value>,
) -> rusqlite::Result<String> {
    let mut stmt = conn.prepare(sql)?;
    let columns: Vec<String> = stmt
        .column_names()
        .iter()
        .map(|name| name.to_string())
        .collect();
    let rows = stmt.query_map(rusqlite::params_from_iter(params), |row| {
        let mut map = serde_json::Map::new();
        for (index, column) in columns.iter().enumerate() {
            let value = row
                .get::<usize, rusqlite::types::Value>(index)
                .map_or(serde_json::Value::Null, value_to_json);
            map.insert(column.clone(), value);
        }
        Ok(serde_json::Value::Object(map))
    })?;
    let mut result = Vec::new();
    for row in rows {
        result.push(row?);
    }
    Ok(json_dumps(&serde_json::Value::Array(result)))
}

fn list_tables(conn: &Connection) -> rusqlite::Result<String> {
    let mut stmt = conn.prepare(
        "SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' ORDER BY name",
    )?;
    let rows = stmt.query_map([], |row| row.get::<_, String>(0))?;
    let mut names = Vec::new();
    for row in rows {
        names.push(serde_json::Value::String(row?));
    }
    Ok(json_dumps(&serde_json::Value::Array(names)))
}

fn parse_params_json(json: &str) -> Result<Vec<rusqlite::types::Value>, String> {
    let parsed: Vec<serde_json::Value> = serde_json::from_str(json)
        .map_err(|e| format!("paramsJson must contain a JSON array: {}", e))?;
    let mut params = Vec::new();
    for value in parsed {
        params.push(match value {
            serde_json::Value::Null => rusqlite::types::Value::Null,
            serde_json::Value::Bool(b) => rusqlite::types::Value::Integer(b as i64),
            serde_json::Value::Number(n) => {
                if let Some(i) = n.as_i64() {
                    rusqlite::types::Value::Integer(i)
                } else if let Some(f) = n.as_f64() {
                    rusqlite::types::Value::Real(f)
                } else {
                    return Err("Invalid number".to_string());
                }
            }
            serde_json::Value::String(s) => rusqlite::types::Value::Text(s),
            _ => return Err("Unsupported parameter type".to_string()),
        });
    }
    Ok(params)
}

fn value_to_json(value: rusqlite::types::Value) -> serde_json::Value {
    match value {
        rusqlite::types::Value::Null => serde_json::Value::Null,
        rusqlite::types::Value::Integer(i) => {
            serde_json::Value::Number(serde_json::Number::from(i))
        }
        rusqlite::types::Value::Real(f) => serde_json::Number::from_f64(f)
            .map_or(serde_json::Value::Null, |n| serde_json::Value::Number(n)),
        rusqlite::types::Value::Text(s) => serde_json::Value::String(s),
        rusqlite::types::Value::Blob(b) => {
            // Encode blobs as base64 strings for JSON compatibility.
            let b64 = STANDARD.encode(&b);
            serde_json::Value::String(b64)
        }
    }
}

// Renders one value the way the reference's `str()` does for `scalar()`.
fn value_to_scalar(value: rusqlite::types::Value) -> String {
    match value {
        rusqlite::types::Value::Null => String::new(),
        rusqlite::types::Value::Integer(i) => i.to_string(),
        rusqlite::types::Value::Real(f) => f.to_string(),
        rusqlite::types::Value::Text(s) => s,
        rusqlite::types::Value::Blob(b) => STANDARD.encode(&b),
    }
}

const OPS: &[(&str, &str, &str)] = &[
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
    ("open", "sqldb_open", "cdecl:int64(...)"),
    ("close", "sqldb_close", "cdecl:int64(...)"),
    ("executeOn", "sqldb_execute_on", "cdecl:cstring(...)"),
    (
        "executeArgsOn",
        "sqldb_execute_args_on",
        "cdecl:cstring(...)",
    ),
    ("scriptOn", "sqldb_script_on", "cdecl:cstring(...)"),
    ("queryOn", "sqldb_query_on", "cdecl:cstring(...)"),
    ("queryArgsOn", "sqldb_query_args_on", "cdecl:cstring(...)"),
    ("scalarOn", "sqldb_scalar_on", "cdecl:cstring(...)"),
    ("scalarArgsOn", "sqldb_scalar_args_on", "cdecl:cstring(...)"),
    (
        "lastInsertIdOn",
        "sqldb_last_insert_id_on",
        "cdecl:int64(...)",
    ),
    ("tableExistsOn", "sqldb_table_exists_on", "cdecl:int64(...)"),
    ("tablesOn", "sqldb_tables_on", "cdecl:cstring(...)"),
];

lynxer_module!(OPS);
