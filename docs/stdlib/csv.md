# csv

CSV and TSV parsing, writing and transformation.

**Backend:** native — `stdlib/csv.so`, built from `stdlib/csv.cpp` (a
hand-written reader/writer) and `stdlib/native_json.hpp`.
**Import:** `import("csv")` → `global.csv.*`

Rows are exchanged as JSON strings: arrays of arrays for positional access and
arrays of objects for keyed access, so `csv` pairs naturally with
[json](json.md).

| Function | Signature | Returns |
| --- | --- | --- |
| `readCSV` | `(str path)` | Raw file content, or `"ERROR: ..."` |
| `writeCSV` | `(str path, str jsonRows, str headers)` | `"ok"` or `"ERROR: ..."` |
| `buildCSV` | `(str jsonRows, str headers)` | CSV text (no disk write) |
| `appendRow` | `(str csvStr, str jsonRow)` | CSV text with one row appended |
| `parseRows` | `(str csvStr)` | JSON array-of-arrays, header row included |
| `parseHeaders` | `(str csvStr)` | JSON array of the header row |
| `parseToJson` | `(str csvStr)` | JSON array of objects keyed by the header row |
| `getColumn` | `(str csvStr, str colName)` | JSON array of the column's values |
| `getRow` | `(str csvStr, int rowIdx)` | JSON array for data row `rowIdx` (0-based) |
| `rowCount` | `(str csvStr)` | Data rows, excluding the header |
| `colCount` | `(str csvStr)` | Columns in the header row |
| `hasColumn` | `(str csvStr, str colName)` | `1` or `0` |
| `filterRows` | `(str csvStr, str colName, str value)` | CSV text with matching rows only |
| `sortCSV` | `(str csvStr, str colName)` | CSV text sorted ascending (lexicographic) |
| `sortCSVDesc` | `(str csvStr, str colName)` | CSV text sorted descending |
| `columnStats` | `(str csvStr, str colName)` | JSON `{count, min, max, sum, mean}` or `{}` |
| `toTSV` | `(str csvStr)` | TSV text |
| `fromTSV` | `(str tsvStr)` | CSV text |
| `readWithDelimiter` | `(str path, str delimiter)` | Raw file content |
| `parseDelimited` | `(str csvStr, str delimiter)` | JSON array of objects using a custom delimiter |

Dialect follows Python's `csv` module: `,` delimiter, `"` quoting with `""`
escaping, and `\r\n` line terminators on output. Non-string JSON values are
rendered as `""` / `true` / `false`.

## Example

```lynx
global setup(){ import("csv"); import("json"); }

global main(){
    str data = "name,age\nalpha,30\nbeta,25\n";
    println(global.csv.rowCount(data));
    println(global.csv.getColumn(data, "name"));
    println(global.csv.columnStats(data, "age"));
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
