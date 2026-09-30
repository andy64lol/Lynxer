# SQLite Database Module

The `sqldb` module provides SQLite database functionality using Rust's `rusqlite` crate.

## Functions

- `execute(path: string, sql: string) -> string`
  Executes one SQL statement and commits it. Returns `"ok"` or `"ERROR: <message>"`.

- `executeArgs(path: string, sql: string, paramsJson: string) -> string`
  Executes one parameterized SQL statement. `paramsJson` must be a JSON array.
  Returns `"ok"` or `"ERROR: <message>"`.

- `script(path: string, sqlScript: string) -> string`
  Executes multiple SQL statements as one transaction. Returns `"ok"` or `"ERROR: <message>"`.

- `query(path: string, sql: string) -> string`
  Query rows and return a JSON array of objects.

- `queryArgs(path: string, sql: string, paramsJson: string) -> string`
  Parameterized form of `query()`.

- `scalar(path: string, sql: string) -> string`
  Returns the first column of the first row as a string, or `""` when absent.

- `scalarArgs(path: string, sql: string, paramsJson: string) -> string`
  Parameterized form of `scalar()`.

- `lastInsertId(path: string, sql: string, paramsJson: string) -> int`
  Executes an insert/update and returns SQLite's lastrowid. Returns `-1` on error.

- `tableExists(path: string, tableName: string) -> bool`
  Returns whether a table exists in the database.

- `tables(path: string) -> string`
  Returns table names as a JSON array.

### Connection handles

Every function above names a path and opens a connection for that call only. To
reuse one connection, open it once and use the `*On` forms with the handle:

- `open(path: string) -> int`
  Opens the database and keeps the connection open. Returns the handle, or `-1`
  when the database cannot be opened.

- `close(handle: int) -> int`
  Releases a handle. Returns `1` when it was live, else `0`.

- `executeOn`, `executeArgsOn`, `scriptOn`, `queryOn`, `queryArgsOn`,
  `scalarOn`, `scalarArgsOn`, `lastInsertIdOn`, `tableExistsOn`, `tablesOn`
  are the handle-taking forms of the functions above (same arguments, with the
  handle in place of the path).

Handles stay valid for the run, and a closed or unknown handle answers the
usual sentinels (`"ERROR: unknown connection handle <n>"`, `-1`, `false`)
rather than raising. Two threads can share a handle: each call takes the
connection for its duration.

## Example

```lynx
global setup(){
    import("sqldb")
}

global main(){
    global.sqldb.execute("database.db", "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT)");
    global.sqldb.execute("database.db", "INSERT INTO users (name) VALUES ('Alice')");
    println(global.sqldb.query("database.db", "SELECT * FROM users"));

    // Or keep one connection open across calls.
    int db = global.sqldb.open("database.db");
    global.sqldb.executeOn(db, "INSERT INTO users (name) VALUES ('Bob')");
    println(global.sqldb.queryOn(db, "SELECT * FROM users ORDER BY id"));
    global.sqldb.close(db);
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
