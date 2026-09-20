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

## Example

```lynx
import("sqldb")

global main(){
    global.sqldb.execute("database.db", "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT)");
    global.sqldb.execute("database.db", "INSERT INTO users (name) VALUES ('Alice')");
    println(global.sqldb.query("database.db", "SELECT * FROM users"));
}
```