# csv

CSV (comma-separated values) parsing, writing, and transformation helpers.

```c
global setup(){
    import("csv");
}
```

All row data is exchanged as JSON arrays of objects (for keyed access) or JSON
arrays of arrays (for positional access). This lets you pass structured data
between the CSV module and the `json` module cleanly.

---

## Reading CSV

| Function | Signature | Description |
|----------|-----------|-------------|
| `readCSV` | `readCSV(str path)` | Read a CSV file; returns JSON array of objects (first row = headers). Returns `"[]"` on error. |
| `readCSVRaw` | `readCSVRaw(str path)` | Read a CSV file; returns JSON array of arrays (including header row). Returns `"[]"` on error. |
| `parseCSV` | `parseCSV(str csvStr)` | Parse a CSV string into a JSON array of objects. |
| `parseCSVRaw` | `parseCSVRaw(str csvStr)` | Parse a CSV string into a JSON array of arrays. |
| `csvRow` | `csvRow(str csvStr, int index)` | Return row `index` as a JSON object (0-based, header row not counted). |
| `csvRowCount` | `csvRowCount(str csvStr)` | Number of data rows (not counting the header). |
| `csvHeaders` | `csvHeaders(str csvStr)` | Headers as a comma-separated string. |
| `csvColumn` | `csvColumn(str csvStr, str col)` | All values in column `col` as a JSON array of strings. |
| `readWithDelimiter` | `readWithDelimiter(str path, str delimiter)` | Read a delimited file using a custom separator character. Returns raw file contents. |
| `parseDelimited` | `parseDelimited(str csvStr, str delimiter)` | Parse a custom-delimited string into a JSON array of objects. |

### Example — read a CSV file

```c
global setup(){
    import("csv");
    import("json");
}

global main(){
    // Assume scores.csv:
    //   name,score
    //   Alice,95
    //   Bob,87

    str rows = global.csv.readCSV("scores.csv");
    print(global.csv.csvRowCount(rows)); print("\n");   // 2

    str first = global.csv.csvRow(rows, 0);
    print(global.json.jsonGet(first, "name")); print("\n");   // Alice
    print(global.json.jsonGet(first, "score")); print("\n");  // 95

    str names = global.csv.csvColumn(rows, "name");
    print(names); print("\n");   // ["Alice","Bob"]
}
```

---

## Writing CSV

| Function | Signature | Description |
|----------|-----------|-------------|
| `writeCSV` | `writeCSV(str path, str jsonRows, str headers)` | Write a CSV file from a JSON array of objects. `headers` is a comma-separated list of column names. Returns `"ok"` or `"ERROR: ..."`. |
| `buildCSV` | `buildCSV(str jsonRows, str headers)` | Like `writeCSV` but returns the CSV string instead of writing to disk. |
| `appendRow` | `appendRow(str csvStr, str jsonRow)` | Append a single row (JSON array of values) to a CSV string. Returns the new CSV string. |

### Example — build and write a CSV

```c
global setup(){
    import("csv");
    import("json");
}

global main(){
    // Build rows as a JSON array of objects
    str row1 = '{"name":"Alice","age":"30","city":"London"}';
    str row2 = '{"name":"Bob","age":"25","city":"Paris"}';
    str rows = "[" + row1 + "," + row2 + "]";

    str result = global.csv.writeCSV("people.csv", rows, "name,age,city");
    print(result); print("\n");    // ok
}
```

### Example — append a row

```c
global main(){
    str csv = global.csv.readCSVRaw("people.csv");   // raw arrays
    str updated = global.csv.appendRow(csv, '["Charlie","28","Berlin"]');
    global.fileIO.writeFile("people.csv", updated);
}
```

---

## Transforming CSV

| Function | Signature | Description |
|----------|-----------|-------------|
| `filterCSV` | `filterCSV(str csvStr, str col, str value)` | Keep only rows where column `col` equals `value`. Returns a new CSV string. |
| `sortCSV` | `sortCSV(str csvStr, str col)` | Sort rows by column `col` (ascending, lexicographic). Returns a new CSV string. |
| `dedupCSV` | `dedupCSV(str csvStr, str col)` | Remove duplicate rows by the value of column `col`. Returns a new CSV string. |
| `fromTSV` | `fromTSV(str tsvStr)` | Convert a TSV (tab-separated) string to a standard CSV string. |

### Example — filter and sort

```c
global main(){
    str data = global.csv.readCSV("people.csv");

    // Keep only rows where city is "London"
    str london = global.csv.filterCSV(data, "city", "London");
    print(global.csv.csvRowCount(london)); print("\n");

    // Sort by age
    str sorted = global.csv.sortCSV(data, "age");
    print(global.csv.csvRow(sorted, 0)); print("\n");
}
```

---

## Notes

- All functions return safe defaults (`"[]"`, `""`, `"ERROR: ..."`) on exceptions — never crash.
- `readCSV` / `parseCSV` treat the first row as a header. Use `readCSVRaw` / `parseCSVRaw` for raw positional access.
- Column names in `headers` must match the keys in your JSON objects exactly (case-sensitive).
- Use `parseDelimited` with `delimiter=";"` for European-style semicolon-separated files.
