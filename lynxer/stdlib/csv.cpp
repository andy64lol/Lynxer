// Lynxer `csv` stdlib backend: a small CSV/TSV reader and writer.
//
// Semantics follow Python's `csv` module defaults: ',' delimiter, '"' quote
// character with '""' escaping, and "\r\n" line terminators on output. Bracket
// data is exchanged as JSON strings, parsed with native_json.hpp.
//
// Divergence: Python's `str()` conversion of non-string JSON values produces
// "None"/"True"/"False"; this port emits ""/true/false and never writes the
// literal "None".

#include "native_json.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

using native_json::Type;
using native_json::Value;

using Rows = std::vector<std::vector<std::string>>;

static const char* stable(std::string value) {
    thread_local std::string result;
    result = std::move(value);
    return result.c_str();
}

static std::string textOrEmpty(const char* text) {
    return text == nullptr ? std::string() : std::string(text);
}

/* ---------- Parsing ---------- */

static Rows parseDelimitedText(const std::string& text, char delimiter) {
    Rows rows;
    std::vector<std::string> row;
    std::string field;
    bool inQuotes = false;
    bool fieldStarted = false;
    bool atRowStart = true;
    std::size_t index = 0;

    const auto endRow = [&]() {
        if (fieldStarted || !field.empty() || !row.empty()) {
            row.push_back(field);
            rows.push_back(row);
        } else {
            rows.push_back(std::vector<std::string>{});
        }
        row.clear();
        field.clear();
        fieldStarted = false;
        atRowStart = true;
    };

    while (index < text.size()) {
        const char character = text[index];
        if (inQuotes) {
            if (character == '"') {
                if (index + 1 < text.size() && text[index + 1] == '"') {
                    field += '"';
                    index += 2;
                    continue;
                }
                inQuotes = false;
                ++index;
                continue;
            }
            field += character;
            ++index;
            continue;
        }
        if (character == '"') {
            inQuotes = true;
            fieldStarted = true;
            atRowStart = false;
            ++index;
            continue;
        }
        if (character == delimiter) {
            row.push_back(field);
            field.clear();
            fieldStarted = false;
            atRowStart = false;
            ++index;
            continue;
        }
        if (character == '\r') {
            if (index + 1 < text.size() && text[index + 1] == '\n') {
                ++index;
            }
            endRow();
            ++index;
            continue;
        }
        if (character == '\n') {
            endRow();
            ++index;
            continue;
        }
        field += character;
        fieldStarted = true;
        atRowStart = false;
        ++index;
    }
    if (!atRowStart) {
        endRow();
    }
    return rows;
}

/* ---------- Writing ---------- */

static std::string escapeField(const std::string& value, char delimiter) {
    const bool needsQuotes =
        value.find(delimiter) != std::string::npos ||
        value.find('"') != std::string::npos ||
        value.find('\r') != std::string::npos ||
        value.find('\n') != std::string::npos;
    if (!needsQuotes) {
        return value;
    }
    std::string result = "\"";
    for (const char character : value) {
        if (character == '"') {
            result += "\"\"";
        } else {
            result += character;
        }
    }
    result += '"';
    return result;
}

static void writeRow(std::string& output, const std::vector<std::string>& row,
                     char delimiter) {
    for (std::size_t index = 0; index < row.size(); ++index) {
        if (index > 0) {
            output += delimiter;
        }
        output += escapeField(row[index], delimiter);
    }
    output += "\r\n";
}

static std::string writeRows(const Rows& rows, char delimiter) {
    std::string output;
    for (const auto& row : rows) {
        writeRow(output, row, delimiter);
    }
    return output;
}

/* ---------- JSON helpers ---------- */

static std::string scalarText(const Value& value) {
    switch (value.type) {
        case Type::Null: return "";
        case Type::Bool: return value.boolean ? "true" : "false";
        case Type::Integer: return std::to_string(value.integer);
        case Type::Number: return native_json::numberToString(value.number);
        case Type::String: return value.text;
        case Type::Array:
        case Type::Object: return native_json::dump(value, false);
    }
    return "";
}

static std::string jsonArrayOfTexts(const std::vector<std::string>& values) {
    Value array = native_json::makeArray();
    for (const auto& entry : values) {
        array.items.push_back(native_json::makeString(entry));
    }
    return native_json::dump(array, false);
}

static std::vector<std::string> headerNames(const std::string& headers) {
    std::vector<std::string> names;
    std::size_t start = 0;
    while (start <= headers.size()) {
        const std::size_t comma = headers.find(',', start);
        std::string name = headers.substr(
            start, comma == std::string::npos ? std::string::npos
                                              : comma - start);
        std::size_t begin = 0;
        std::size_t end = name.size();
        while (begin < end &&
               (name[begin] == ' ' || name[begin] == '\t')) {
            ++begin;
        }
        while (end > begin &&
               (name[end - 1] == ' ' || name[end - 1] == '\t')) {
            --end;
        }
        name = name.substr(begin, end - begin);
        if (!name.empty()) {
            names.push_back(name);
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return names;
}

// Returns the index of `name` in the header row, or -1.
static std::ptrdiff_t columnIndex(const std::vector<std::string>& header,
                                  const std::string& name) {
    for (std::size_t index = 0; index < header.size(); ++index) {
        if (header[index] == name) {
            return static_cast<std::ptrdiff_t>(index);
        }
    }
    return -1;
}

static std::string fieldAt(const std::vector<std::string>& row,
                           std::ptrdiff_t index) {
    if (index < 0 || static_cast<std::size_t>(index) >= row.size()) {
        return "";
    }
    return row[static_cast<std::size_t>(index)];
}

/* ---------- Public API ---------- */

static std::string readFileOrError(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return "ERROR: could not read '" + path + "'";
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

extern "C" const char* csv_readCSV(const char* path) {
    return stable(readFileOrError(textOrEmpty(path)));
}

extern "C" const char* csv_readWithDelimiter(const char* path, const char*) {
    return stable(readFileOrError(textOrEmpty(path)));
}

static std::string buildFromJson(const std::string& jsonRows,
                                 const std::string& headers, char delimiter) {
    const std::vector<std::string> columns = headerNames(headers);
    Value parsed;
    if (!native_json::parse(jsonRows, parsed) || parsed.type != Type::Array) {
        return "ERROR: rows must be a JSON array";
    }
    std::string output;
    writeRow(output, columns, delimiter);
    for (const auto& item : parsed.items) {
        if (item.type != Type::Object) {
            return "ERROR: rows must be a JSON array of objects";
        }
        std::vector<std::string> row;
        for (const auto& column : columns) {
            const Value* found = native_json::findField(item, column);
            row.push_back(found == nullptr ? std::string()
                                           : scalarText(*found));
        }
        writeRow(output, row, delimiter);
    }
    return output;
}

extern "C" const char* csv_writeCSV(const char* path, const char* jsonRows,
                                    const char* headers) {
    const std::string built =
        buildFromJson(textOrEmpty(jsonRows), textOrEmpty(headers), ',');
    if (built.rfind("ERROR:", 0) == 0) {
        return stable(built);
    }
    std::ofstream output(textOrEmpty(path),
                         std::ios::binary | std::ios::trunc);
    if (!output) {
        return stable("ERROR: could not write '" + textOrEmpty(path) + "'");
    }
    output << built;
    return output ? stable("ok") : stable("ERROR: write failed");
}

extern "C" const char* csv_buildCSV(const char* jsonRows,
                                    const char* headers) {
    return stable(
        buildFromJson(textOrEmpty(jsonRows), textOrEmpty(headers), ','));
}

extern "C" const char* csv_appendRow(const char* csvText,
                                     const char* jsonRow) {
    Value parsed;
    if (!native_json::parse(textOrEmpty(jsonRow), parsed) ||
        parsed.type != Type::Array) {
        return stable("ERROR: row must be a JSON array");
    }
    std::string text = textOrEmpty(csvText);
    while (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '\r') {
        text.pop_back();
    }
    std::string output = text + "\n";
    std::vector<std::string> row;
    for (const auto& item : parsed.items) {
        row.push_back(scalarText(item));
    }
    writeRow(output, row, ',');
    return stable(std::move(output));
}

extern "C" const char* csv_parseRows(const char* csvText) {
    const Rows rows = parseDelimitedText(textOrEmpty(csvText), ',');
    Value array = native_json::makeArray();
    for (const auto& row : rows) {
        Value inner = native_json::makeArray();
        for (const auto& field : row) {
            inner.items.push_back(native_json::makeString(field));
        }
        array.items.push_back(std::move(inner));
    }
    return stable(native_json::dump(array, false));
}

extern "C" const char* csv_parseHeaders(const char* csvText) {
    const Rows rows = parseDelimitedText(textOrEmpty(csvText), ',');
    if (rows.empty()) {
        return stable("[]");
    }
    return stable(jsonArrayOfTexts(rows.front()));
}

extern "C" const char* csv_parseToJson(const char* csvText) {
    const Rows rows = parseDelimitedText(textOrEmpty(csvText), ',');
    if (rows.empty()) {
        return stable("[]");
    }
    const std::vector<std::string>& header = rows.front();
    Value array = native_json::makeArray();
    for (std::size_t index = 1; index < rows.size(); ++index) {
        Value object = native_json::makeObject();
        for (std::size_t column = 0; column < header.size(); ++column) {
            native_json::setField(
                object, header[column],
                native_json::makeString(fieldAt(
                    rows[index], static_cast<std::ptrdiff_t>(column))));
        }
        array.items.push_back(std::move(object));
    }
    return stable(native_json::dump(array, false));
}

extern "C" const char* csv_parseDelimited(const char* csvText,
                                          const char* delimiter) {
    const std::string marker = textOrEmpty(delimiter);
    const char character = marker.empty() ? ',' : marker[0];
    const Rows rows = parseDelimitedText(textOrEmpty(csvText), character);
    if (rows.empty()) {
        return stable("[]");
    }
    const std::vector<std::string>& header = rows.front();
    Value array = native_json::makeArray();
    for (std::size_t index = 1; index < rows.size(); ++index) {
        Value object = native_json::makeObject();
        for (std::size_t column = 0; column < header.size(); ++column) {
            native_json::setField(
                object, header[column],
                native_json::makeString(fieldAt(
                    rows[index], static_cast<std::ptrdiff_t>(column))));
        }
        array.items.push_back(std::move(object));
    }
    return stable(native_json::dump(array, false));
}

extern "C" const char* csv_getColumn(const char* csvText,
                                     const char* column) {
    const Rows rows = parseDelimitedText(textOrEmpty(csvText), ',');
    if (rows.empty()) {
        return stable("[]");
    }
    const std::ptrdiff_t index = columnIndex(rows.front(), textOrEmpty(column));
    std::vector<std::string> values;
    for (std::size_t row = 1; row < rows.size(); ++row) {
        values.push_back(fieldAt(rows[row], index));
    }
    return stable(jsonArrayOfTexts(values));
}

extern "C" const char* csv_getRow(const char* csvText, std::int64_t rowIndex) {
    const Rows rows = parseDelimitedText(textOrEmpty(csvText), ',');
    if (rowIndex < 0 ||
        static_cast<std::size_t>(rowIndex) + 1 >= rows.size()) {
        return stable("[]");
    }
    return stable(
        jsonArrayOfTexts(rows[static_cast<std::size_t>(rowIndex) + 1]));
}

extern "C" std::int64_t csv_rowCount(const char* csvText) {
    const Rows rows = parseDelimitedText(textOrEmpty(csvText), ',');
    if (rows.empty()) {
        return 0;
    }
    return static_cast<std::int64_t>(rows.size() - 1);
}

extern "C" std::int64_t csv_colCount(const char* csvText) {
    const Rows rows = parseDelimitedText(textOrEmpty(csvText), ',');
    return rows.empty() ? 0 : static_cast<std::int64_t>(rows.front().size());
}

extern "C" std::int64_t csv_hasColumn(const char* csvText,
                                      const char* column) {
    const Rows rows = parseDelimitedText(textOrEmpty(csvText), ',');
    if (rows.empty()) {
        return 0;
    }
    return columnIndex(rows.front(), textOrEmpty(column)) < 0 ? 0 : 1;
}

static std::string rowsToCsv(const std::vector<std::string>& header,
                             const Rows& rows) {
    std::string output;
    writeRow(output, header, ',');
    for (const auto& row : rows) {
        writeRow(output, row, ',');
    }
    return output;
}

extern "C" const char* csv_filterRows(const char* csvText,
                                      const char* column,
                                      const char* wanted) {
    const Rows rows = parseDelimitedText(textOrEmpty(csvText), ',');
    if (rows.empty()) {
        return stable("");
    }
    const std::ptrdiff_t index = columnIndex(rows.front(), textOrEmpty(column));
    const std::string target = textOrEmpty(wanted);
    std::vector<std::string> header = rows.front();
    Rows matched;
    for (std::size_t row = 1; row < rows.size(); ++row) {
        if (fieldAt(rows[row], index) == target) {
            matched.push_back(rows[row]);
        }
    }
    return stable(rowsToCsv(header, matched));
}

static std::string sortByColumn(const std::string& csvText,
                                const std::string& column, bool descending) {
    const Rows rows = parseDelimitedText(csvText, ',');
    if (rows.empty()) {
        return "";
    }
    const std::ptrdiff_t index = columnIndex(rows.front(), column);
    std::vector<std::string> header = rows.front();
    Rows data(rows.begin() + 1, rows.end());
    std::stable_sort(data.begin(), data.end(),
                     [index, descending](const std::vector<std::string>& left,
                                         const std::vector<std::string>& right) {
                         const std::string a = fieldAt(left, index);
                         const std::string b = fieldAt(right, index);
                         return descending ? a > b : a < b;
                     });
    return rowsToCsv(header, data);
}

extern "C" const char* csv_sortCSV(const char* csvText, const char* column) {
    return stable(sortByColumn(textOrEmpty(csvText), textOrEmpty(column), false));
}

extern "C" const char* csv_sortCSVDesc(const char* csvText,
                                       const char* column) {
    return stable(sortByColumn(textOrEmpty(csvText), textOrEmpty(column), true));
}

extern "C" const char* csv_columnStats(const char* csvText,
                                       const char* column) {
    const Rows rows = parseDelimitedText(textOrEmpty(csvText), ',');
    if (rows.empty()) {
        return stable("{}");
    }
    const std::ptrdiff_t index = columnIndex(rows.front(), textOrEmpty(column));
    std::vector<double> numbers;
    for (std::size_t row = 1; row < rows.size(); ++row) {
        const std::string text = fieldAt(rows[row], index);
        if (text.empty()) {
            continue;
        }
        char* end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        if (end != text.c_str() && *end == '\0') {
            numbers.push_back(value);
        }
    }
    if (numbers.empty()) {
        return stable("{}");
    }
    double minimum = numbers.front();
    double maximum = numbers.front();
    double total = 0.0;
    for (const double value : numbers) {
        minimum = value < minimum ? value : minimum;
        maximum = value > maximum ? value : maximum;
        total += value;
    }
    Value object = native_json::makeObject();
    native_json::setField(
        object, "count",
        native_json::makeInteger(static_cast<std::int64_t>(numbers.size())));
    native_json::setField(object, "min", native_json::makeNumber(minimum));
    native_json::setField(object, "max", native_json::makeNumber(maximum));
    native_json::setField(object, "sum", native_json::makeNumber(total));
    native_json::setField(
        object, "mean",
        native_json::makeNumber(total / static_cast<double>(numbers.size())));
    return stable(native_json::dump(object, false));
}

extern "C" const char* csv_toTSV(const char* csvText) {
    const Rows rows = parseDelimitedText(textOrEmpty(csvText), ',');
    return stable(writeRows(rows, '\t'));
}

extern "C" const char* csv_fromTSV(const char* tsvText) {
    const Rows rows = parseDelimitedText(textOrEmpty(tsvText), '\t');
    return stable(writeRows(rows, ','));
}

extern "C" int lynxer_module_init_v1(RegisterFunction function,
                                     RegisterConstant, RegisterType) {
    return function("readCSV", "csv_readCSV", "cdecl:cstring(cstring)") &&
                   function("writeCSV", "csv_writeCSV",
                            "cdecl:cstring(cstring,cstring,cstring)") &&
                   function("appendRow", "csv_appendRow",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("buildCSV", "csv_buildCSV",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("parseRows", "csv_parseRows",
                            "cdecl:cstring(cstring)") &&
                   function("parseHeaders", "csv_parseHeaders",
                            "cdecl:cstring(cstring)") &&
                   function("parseToJson", "csv_parseToJson",
                            "cdecl:cstring(cstring)") &&
                   function("getColumn", "csv_getColumn",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("getRow", "csv_getRow",
                            "cdecl:cstring(cstring,int64)") &&
                   function("rowCount", "csv_rowCount",
                            "cdecl:int64(cstring)") &&
                   function("colCount", "csv_colCount",
                            "cdecl:int64(cstring)") &&
                   function("hasColumn", "csv_hasColumn",
                            "cdecl:int64(cstring,cstring)") &&
                   function("filterRows", "csv_filterRows",
                            "cdecl:cstring(cstring,cstring,cstring)") &&
                   function("sortCSV", "csv_sortCSV",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("sortCSVDesc", "csv_sortCSVDesc",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("columnStats", "csv_columnStats",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("toTSV", "csv_toTSV", "cdecl:cstring(cstring)") &&
                   function("fromTSV", "csv_fromTSV",
                            "cdecl:cstring(cstring)") &&
                   function("readWithDelimiter", "csv_readWithDelimiter",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("parseDelimited", "csv_parseDelimited",
                            "cdecl:cstring(cstring,cstring)")
               ? 0
               : 1;
}
