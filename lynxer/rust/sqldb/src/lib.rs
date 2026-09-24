//! Lynxer `sqldb` stdlib backend: SQLite database operations.
//!
//! Replaces Python's sqlite3 with Rust `rusqlite`.
//! The Clynxer-facing contract matches `clynxer/stdlib/sqldb.lynx`:
//! every operation names a database **path**, opens a connection, does its
//! work and closes it again. Structured results are returned as JSON strings,
//! errors as `"ERROR: <message>"`.

use base64::engine::general_purpose::STANDARD;
use base64::Engine as _;
use lynxer_abi::{export_int, export_string, clynxer_module};
use rusqlite::{Connection, Result as SqlResult};
use std::path::Path;

// --- Connection handling ---------------------------------------------------
//
// There is no handle registry: the reference opens and closes a connection per
// call, so `path` is the only state an operation needs.

// Opens `path` for the duration of `f` and closes it (on drop) afterwards.
// Everything a connection can fail with surfaces as a `rusqlite::Error`, which
// each op renders into its own sentinel.
fn with_conn<T, F>(path: &str, f: F) -> SqlResult<T>
where
    F: FnOnce(&Connection) -> SqlResult<T>,
{
    let conn = Connection::open(Path::new(path))?;
    f(&conn)
}

// --- Ops -------------------------------------------------------------------

// Execute one SQL statement and commit. Returns "ok" or "ERROR: <message>".
export_string!(sqldb_execute, args, {
    let path = args.string(0);
    let sql = args.string(1);
    match with_conn(path, |conn| conn.execute(sql, [])) {
        Ok(_) => "ok".to_string(),
        Err(e) => format!("ERROR: {}", e),
    }
});

// Execute one parameterized SQL statement. paramsJson must be a JSON array.
// Returns "ok" or "ERROR: <message>".
export_string!(sqldb_execute_args, args, {
    let path = args.string(0);
    let sql = args.string(1);
    let params = match parse_params_json(args.string(2)) {
        Ok(params) => params,
        Err(e) => return format!("ERROR: {}", e),
    };
    match with_conn(path, |conn| {
        conn.execute(sql, rusqlite::params_from_iter(params))
    }) {
        Ok(_) => "ok".to_string(),
        Err(e) => format!("ERROR: {}", e),
    }
});

// Execute multiple SQL statements as one transaction. Returns "ok" or "ERROR: <message>".
export_string!(sqldb_script, args, {
    let path = args.string(0);
    let script = args.string(1);
    match with_conn(path, |conn| conn.execute_batch(script)) {
        Ok(_) => "ok".to_string(),
        Err(e) => format!("ERROR: {}", e),
    }
});

// Query rows and return a JSON array of objects.
export_string!(sqldb_query, args, {
    let path = args.string(0);
    let sql = args.string(1);
    match with_conn(path, |conn| query_rows(conn, sql, Vec::new())) {
        Ok(json) => json,
        Err(e) => format!("ERROR: {}", e),
    }
});

// Parameterized form of query().
export_string!(sqldb_query_args, args, {
    let path = args.string(0);
    let sql = args.string(1);
    let params = match parse_params_json(args.string(2)) {
        Ok(params) => params,
        Err(e) => return format!("ERROR: {}", e),
    };
    match with_conn(path, |conn| query_rows(conn, sql, params)) {
        Ok(json) => json,
        Err(e) => format!("ERROR: {}", e),
    }
});

// Return the first column of the first row as a string, or "" when absent.
export_string!(sqldb_scalar, args, {
    let path = args.string(0);
    let sql = args.string(1);
    match with_conn(path, |conn| {
        conn.query_row(sql, [], |row| row.get::<usize, rusqlite::types::Value>(0))
    }) {
        Ok(value) => value_to_scalar(value),
        // An absent row is "" rather than an error, matching the reference.
        Err(rusqlite::Error::QueryReturnedNoRows) => String::new(),
        Err(e) => format!("ERROR: {}", e),
    }
});

// Parameterized form of scalar().
export_string!(sqldb_scalar_args, args, {
    let path = args.string(0);
    let sql = args.string(1);
    let params = match parse_params_json(args.string(2)) {
        Ok(params) => params,
        Err(e) => return format!("ERROR: {}", e),
    };
    match with_conn(path, |conn| {
        conn.query_row(sql, rusqlite::params_from_iter(params), |row| {
            row.get::<usize, rusqlite::types::Value>(0)
        })
    }) {
        Ok(value) => value_to_scalar(value),
        Err(rusqlite::Error::QueryReturnedNoRows) => String::new(),
        Err(e) => format!("ERROR: {}", e),
    }
});

// Execute an insert/update and return SQLite's lastrowid as an integer.
// Returns -1 on error or when SQLite reports no row id.
export_int!(sqldb_last_insert_id, args, {
    let path = args.string(0);
    let sql = args.string(1);
    let params = match parse_params_json(args.string(2)) {
        Ok(params) => params,
        Err(_) => return -1,
    };
    // The row id is only meaningful on the connection that ran the insert, so
    // it is read before `with_conn` closes it.
    match with_conn(path, |conn| {
        conn.execute(sql, rusqlite::params_from_iter(params))?;
        Ok(conn.last_insert_rowid())
    }) {
        Ok(id) if id > 0 => id,
        _ => -1,
    }
});

// Return whether a table exists in the database.
export_int!(sqldb_table_exists, args, {
    let path = args.string(0);
    let table_name = args.string(1);
    match with_conn(path, |conn| {
        conn.query_row(
            "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?",
            rusqlite::params![table_name],
            |_| Ok(()),
        )
    }) {
        Ok(_) => 1,
        Err(_) => 0,
    }
});

// Return table names as a JSON array.
export_string!(sqldb_tables, args, {
    let path = args.string(0);
    match with_conn(path, list_tables) {
        Ok(json) => json,
        Err(e) => format!("ERROR: {}", e),
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
) -> SqlResult<String> {
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

fn list_tables(conn: &Connection) -> SqlResult<String> {
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
        rusqlite::types::Value::Blob(b) => base64::encode(&b),
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
];

clynxer_module!(OPS);
