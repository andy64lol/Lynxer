# sqldb

SQLite helpers backed by Python's standard-library `sqlite3` module. Import the
module in `setup()`:

```lynx
global setup(){
    import("sqldb");
}
```

All operations receive a database path and open a short-lived connection. This
keeps the API compatible with `rawPy`, whose Python namespace is isolated for
each block. SQLite creates the database file automatically when a write
operation uses a new path.

## Functions

| Function | Signature | Returns |
|----------|-----------|---------|
| `execute` | `execute(str path, str sql)` | `"ok"` or `"ERROR: ..."` |
| `executeArgs` | `executeArgs(str path, str sql, str paramsJson)` | `"ok"` or `"ERROR: ..."` |
| `script` | `script(str path, str sqlScript)` | `"ok"` or `"ERROR: ..."` |
| `query` | `query(str path, str sql)` | JSON array of row objects |
| `queryArgs` | `queryArgs(str path, str sql, str paramsJson)` | JSON array of row objects |
| `scalar` | `scalar(str path, str sql)` | First column of first row as a string |
| `scalarArgs` | `scalarArgs(str path, str sql, str paramsJson)` | Parameterized `scalar` |
| `lastInsertId` | `lastInsertId(str path, str sql, str paramsJson)` | Insert row id, or `-1` |
| `tableExists` | `tableExists(str path, str tableName)` | Boolean |
| `tables` | `tables(str path)` | JSON array of table names |

`paramsJson` must be a JSON array. Use parameterized functions for values
instead of interpolating user input into SQL:

```lynx
global main(){
    str db = "people.sqlite3";
    str created = global.sqldb.execute(
        db,
        "CREATE TABLE IF NOT EXISTS people (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)"
    );
    int id = global.sqldb.lastInsertId(
        db,
        "INSERT INTO people (name, age) VALUES (?, ?)",
        "[\"Ada\", 37]"
    );
    str rows = global.sqldb.queryArgs(
        db,
        "SELECT id, name, age FROM people WHERE age >= ?",
        "[18]"
    );
    println(rows);
}
```

Errors are returned as strings beginning with `"ERROR: "` for query and
write operations. `tableExists` returns `false` on an error.