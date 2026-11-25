// Bobcat reference: CLI IDE code extracted verbatim from lynxer/shell.cpp
// (lines 2043-4230). The original remains in Lynxer. Not compiled by Bobcat.

std::string ideRenderLine(const std::string& line, bool lynx,
                          std::size_t from, std::size_t to,
                          std::size_t cursorColumn, bool current) {
    std::string display;
    for (const char c : line) display += (c == '\t') ? ' ' : c;
    const std::vector<int> colors =
        lynx ? ideTokenColors(display) : std::vector<int>(display.size(), IC_TEXT);
    std::string out;
    int active = -1;
    for (std::size_t i = from; i < to && i < display.size();) {
        const int length = ideUtf8Length(static_cast<unsigned char>(display[i]));
        const std::size_t take =
            (i + static_cast<std::size_t>(length) <= to)
                ? static_cast<std::size_t>(length)
                : 1;
        const int color = colors[i];
        const bool caret = current && i == cursorColumn;
        if (color != active || caret) {
            out += ideFg(color);
            active = color;
        }
        if (caret) out += kIdeReverse;
        out.append(display, i, take);
        if (caret) out += "\x1b[27m" + ideFg(color);
        i += take;
    }
    if (current && cursorColumn >= display.size()) {
        out += ideFg(IC_TEXT) + std::string(kIdeReverse) + " " + "\x1b[27m";
    }
    return out;
}

struct IdeBuffer {
    std::string path;
    std::vector<std::string> lines;
    std::string saved;
    std::size_t cursorLine = 0;
    std::size_t cursorColumn = 0;
    std::size_t topLine = 0;
    // Wrap segment of `topLine` shown on the first editor row: a long line
    // occupies several rows and the view can start in the middle of it.
    std::size_t topWrap = 0;
};

struct IdeTreeRow {
    std::string name;
    std::string path;
    bool isDir = false;
    int depth = 0;
    bool expanded = false;
    bool nonText = false;
};

struct IdeEntry {
    std::string name;
    std::string path;
    bool isDir = false;
    bool nonText = false;
};

struct IdeDiagnostic {
    std::string file;
    int line = 0;
    int column = 0;
    std::string severity;
    std::string code;
    std::string message;
};

enum class IdePrompt { None, Open, New, Folder, Goto, Find, SaveAll };

struct IdeState {
    std::string root;
    std::string interpreter;
    std::string version;
    std::vector<IdeTreeRow> tree;
    std::size_t treeSelection = 0;
    std::size_t treeTop = 0;
    // Horizontal scroll of the tree pane, in display columns. A name that
    // is wider than the pane is cut off, and the arrow keys scroll it into
    // view.
    std::size_t treeScroll = 0;
    bool explorerFocus = false;
    std::vector<IdeBuffer> buffers;
    std::size_t active = 0;
    std::string output;
    std::vector<IdeDiagnostic> diagnostics;
    std::size_t diagnosticIndex = 0;
    std::string status;
    std::string findQuery;
    bool showOutput = false;  // nano-like: no empty panel until a command runs
    // The output panel takes the arrow keys while it has the focus, so the
    // output of a run can be read without leaving the file.
    bool outputFocus = false;
    std::size_t outputScroll = 0;
    bool showHelp = false;
    std::size_t helpTop = 0;
    bool showCommands = false;
    std::size_t commandIndex = 0;
    std::size_t commandTop = 0;
    IdePrompt prompt = IdePrompt::None;
    std::string promptLabel;
    std::string promptValue;
    // Set when the save-all prompt was raised by a toggle-close, a folder
    // collapse or by quit.
    std::string pendingClosePath;
    std::string pendingCollapsePath;
    bool pendingQuit = false;
    int columns = 80;
    int rows = 24;
};

// At most nine buffers so the 1-9 keys can reach every one.
constexpr std::size_t kIdeMaxBuffers = 9;

// The editor column shows a line number and a separator before the text, so
// the text of a row is this many columns narrower than the column. The
// number is at least four digits wide and grows with the file.
int ideGutterColumns(IdeState& state);

// Width of the file-tree column. The renderer and the caret movement share
// it so both agree on where a long line wraps.
int ideExplorerWidth(int columns) {
    return std::min(32, std::max(22, columns / 4));
}

// Display columns available for the text of one editor row.
int ideWrapWidth(IdeState& state) {
    const int columns = state.columns > 0 ? state.columns : 80;
    const int editorWidth = columns - ideExplorerWidth(columns);
    return std::max(8, editorWidth - ideGutterColumns(state));
}

// Index of the wrap segment that holds `column` in `line`. The last segment
// also owns the position past the end of the line.
std::size_t ideCaretSegment(const std::string& line, std::size_t column,
                            int width) {
    const std::vector<std::size_t> offsets = ideWrapOffsets(line, width);
    for (std::size_t i = 0; i + 1 < offsets.size(); ++i) {
        if (column >= offsets[i] && column < offsets[i + 1]) return i;
    }
    return offsets.size() - 1;
}

// Visual row of a buffer position, counted from the first line of the file.
// A soft-wrapped line counts as one row per segment.
std::size_t ideVisualRow(const IdeBuffer& buffer, std::size_t line,
                         std::size_t column, int width) {
    std::size_t row = 0;
    for (std::size_t i = 0; i < line && i < buffer.lines.size(); ++i) {
        row += static_cast<std::size_t>(ideLineRows(buffer.lines[i], width));
    }
    if (line >= buffer.lines.size()) return row;
    return row + ideCaretSegment(buffer.lines[line], column, width);
}

// Buffer line and wrap segment of a visual row counted from the first line.
void ideVisualRowAt(const IdeBuffer& buffer, std::size_t row, int width,
                    std::size_t& line, std::size_t& segment) {
    for (std::size_t i = 0; i < buffer.lines.size(); ++i) {
        const int rows = ideLineRows(buffer.lines[i], width);
        if (row < static_cast<std::size_t>(rows)) {
            line = i;
            segment = row;
            return;
        }
        row -= static_cast<std::size_t>(rows);
    }
    line = buffer.lines.empty() ? 0 : buffer.lines.size() - 1;
    segment = 0;
    if (!buffer.lines.empty()) {
        segment = static_cast<std::size_t>(
            ideLineRows(buffer.lines[line], width) - 1);
    }
}

// The command menu (Ctrl+Alt+C) lists every action with its combo. The same
// table drives the nano-style shortcut bar, the help overlay and key dispatch,
// so a binding is defined in exactly one place.
enum class IdeCommandId {
    Help, Open, Save, Exit, CloseTab, New, Find, Run, Lint, Format, Output,
    Goto, NextMatch, NextProblem, Build, RunProject, Folder, Sidebar, Editor,
    Clear, FocusOutput,
};

struct IdeCommand {
    IdeCommandId id;
    const char* label;   // full label, shown in help and the command menu
    const char* bar;     // compact label for the nano-style shortcut bar
    const char* combo;
    int control;  // control byte, or 0 when only reachable from the menu
};

const IdeCommand* ideCommandById(IdeCommandId id);

const std::vector<IdeCommand>& ideCommands() {
    static const std::vector<IdeCommand> commands = {
        {IdeCommandId::Help,        "Help / key bindings", "Help",   "^G",  7},
        {IdeCommandId::Open,        "Open file",           "Open",   "^O", 15},
        {IdeCommandId::Save,        "Save",                "Save",   "^S", 19},
        {IdeCommandId::Exit,        "Exit",                "Exit",   "^X", 24},
        {IdeCommandId::CloseTab,    "Close tab",           "Close",  "^W", 23},
        {IdeCommandId::New,         "New file",            "New",    "^N", 14},
        {IdeCommandId::Find,        "Find",                "Find",   "^F",  6},
        {IdeCommandId::Run,         "Run file",            "Run",    "^R", 18},
        {IdeCommandId::Lint,        "Lint file",           "Lint",   "^L", 12},
        {IdeCommandId::Format,      "Format file",         "Format", "^T", 20},
        {IdeCommandId::Output,      "Toggle output pane",  "Output", "^Z", 26},
        {IdeCommandId::Goto,        "Go to line",          "Goto",   "^_", 31},
        {IdeCommandId::NextMatch,   "Next match",          "Next",   "^D",  4},
        {IdeCommandId::NextProblem, "Next problem",        "Problem","^V", 22},
        {IdeCommandId::Build,       "Build project",       "Build",  "^Y", 25},
        {IdeCommandId::RunProject,  "Run project",         "RunProj","^U", 21},
        {IdeCommandId::Folder,      "Change folder",       "Folder", "^B",  2},
        {IdeCommandId::Sidebar,     "Focus sidebar",       "Side",  "^P", 16},
        {IdeCommandId::Editor,      "Focus editor",        "Edit",  "^E",  5},
        {IdeCommandId::Clear,       "Clear output",        "Clear",  "^K", 11},
        {IdeCommandId::FocusOutput, "Focus output",        "Output", "^]", 29},
    };
    return commands;
}

// Shortcuts shown in the two-row nano-style bar at the bottom of the screen.
const std::vector<IdeCommandId>& ideBarShortcuts() {
    static const std::vector<IdeCommandId> ids = {
        IdeCommandId::Help, IdeCommandId::Open, IdeCommandId::Save,
        IdeCommandId::Exit, IdeCommandId::CloseTab, IdeCommandId::New,
        IdeCommandId::Find, IdeCommandId::Run, IdeCommandId::Lint,
        IdeCommandId::Format, IdeCommandId::Sidebar, IdeCommandId::Editor,
    };
    return ids;
}

int ideOutputHeight(const IdeState& state) {
    return state.showOutput ? std::min(8, state.rows / 3) : 0;
}

int ideEditorHeight(const IdeState& state) {
    // Rows 0-1 are bars, the last four rows are the open-files list, message
    // line and two shortcut rows.
    return std::max(1, state.rows - 6 - ideOutputHeight(state));
}

std::vector<std::string> ideSplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (const char c : text) {
        if (c == '\n') {
            if (!current.empty() && current.back() == '\r') current.pop_back();
            lines.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) lines.push_back(current);
    if (lines.empty()) lines.push_back(std::string());
    return lines;
}

std::string ideJoinLines(const std::vector<std::string>& lines) {
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) out += '\n';
        out += lines[i];
    }
    out += '\n';
    return out;
}

// The output rows the panel shows below its header.
int ideOutputRows(const IdeState& state) {
    return std::max(1, ideOutputHeight(state) - 1);
}

// The first output line the panel may show.
std::size_t ideOutputMaxScroll(const IdeState& state) {
    const std::size_t lines = ideSplitLines(state.output).size();
    const std::size_t rows = static_cast<std::size_t>(ideOutputRows(state));
    return lines > rows ? lines - rows : 0;
}

// Keeps the scroll of the output panel inside the output.
void ideClampOutputScroll(IdeState& state) {
    const std::size_t maxScroll = ideOutputMaxScroll(state);
    if (state.outputScroll > maxScroll) state.outputScroll = maxScroll;
}

// Scrolls the output panel to the newest line.
void ideOutputScrollToEnd(IdeState& state) {
    state.outputScroll = ideOutputMaxScroll(state);
}

// Scrolls the output panel by `delta` lines.
void ideOutputScrollBy(IdeState& state, long delta) {
    const long maxScroll = static_cast<long>(ideOutputMaxScroll(state));
    long scroll = static_cast<long>(state.outputScroll) + delta;
    if (scroll < 0) scroll = 0;
    if (scroll > maxScroll) scroll = maxScroll;
    state.outputScroll = static_cast<std::size_t>(scroll);
}

std::string ideTrim(const std::string& text) {
    std::size_t begin = 0, end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) --end;
    return text.substr(begin, end - begin);
}

std::string ideBaseName(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool ideReadText(const std::string& path, std::string& out, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot read '" + path + "'";
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    out = buffer.str();
    return true;
}

bool ideWriteText(const std::string& path, const std::string& text,
                  std::string& error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot write '" + path + "'";
        return false;
    }
    output << text;
    output.close();
    if (!output) {
        error = "cannot write '" + path + "'";
        return false;
    }
    return true;
}

std::string ideExtension(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    const std::size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos ||
        (slash != std::string::npos && dot < slash)) {
        return std::string();
    }
    std::string extension = path.substr(dot + 1);
    for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return extension;
}

bool ideIsLynx(const std::string& path) {
    return ideExtension(path) == "lynx";
}

// Formats that are never UTF-8 text. Anything with one of these suffixes is
// refused before it is read, so a PDF, PNG, DOCX or archive can never corrupt
// an editor buffer or dump binary into the tree.
const std::unordered_set<std::string>& ideBinaryExtensions() {
    static const std::unordered_set<std::string> extensions = {
        // documents / office
        "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "odt", "ods",
        "odp", "rtf", "pages", "epub", "mobi",
        // images
        "png", "jpg", "jpeg", "gif", "bmp", "ico", "webp", "tif", "tiff",
        "svg", "psd", "ai", "eps", "heic", "heif", "avif", "raw", "cr2",
        "nef", "arw", "dng",
        // audio / video
        "mp3", "wav", "flac", "ogg", "oga", "m4a", "aac", "wma", "opus",
        "mp4", "mkv", "avi", "mov", "wmv", "webm", "flv", "m4v", "mpg",
        "mpeg", "3gp",
        // archives / packages
        "zip", "tar", "gz", "tgz", "bz2", "tbz2", "xz", "txz", "zst", "lz4",
        "7z", "rar", "iso", "dmg", "deb", "rpm", "apk", "jar", "war", "whl",
        "msi", "exe", "appimage",
        // compiled / binary artifacts
        "so", "dll", "dylib", "a", "lib", "o", "obj", "class", "pyc", "pyo",
        "wasm", "bin", "dat", "db", "sqlite", "sqlite3", "mdb", "pak",
        "ttf", "otf", "woff", "woff2", "eot", "fon",
        // misc binaries
        "pdb", "dSYM", "elf", "lnk", "tmp", "swp", "swo",
    };
    return extensions;
}

bool ideIsNonTextFile(const std::string& path) {
    if (ideBinaryExtensions().count(ideExtension(path)) != 0) return true;
    // Sniff the first block: NUL bytes or a high ratio of control bytes mean
    // binary content even when the suffix looks harmless (or is missing, as in
    // Makefile, LICENSE, README, ...).
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    char buffer[4096];
    input.read(buffer, sizeof(buffer));
    const std::streamsize count = input.gcount();
    std::streamsize control = 0;
    for (std::streamsize i = 0; i < count; ++i) {
        const unsigned char c = static_cast<unsigned char>(buffer[i]);
        if (c == 0) return true;
        if (c < 0x09 || (c > 0x0d && c < 0x20)) ++control;
    }
    return count > 0 && control * 10 > count;
}

// Renders a file name safely for the screen. A name may hold control
// characters -- an escape sequence would move the cursor and clear the
// screen, a tab would shift the whole row -- so they are shown as a caret
// notation instead of being written to the terminal.
std::string ideSafeName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (const char c : name) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7f) {
            out += '^';
            out += static_cast<char>(byte == 0x7f ? '?' : byte + 0x40);
            continue;
        }
        out += c;
    }
    return out;
}

// The part of `text` that covers the display columns [from, from + width).
// `cutRight` reports whether text continues past the window. A wide
// character is never cut in half: the window ends before it instead.
std::string ideColumnsSlice(const std::string& text, std::size_t from,
                             std::size_t width, bool& cutRight) {
    cutRight = false;
    std::vector<std::size_t> offsets;
    std::vector<std::size_t> columns;
    std::size_t column = 0;
    std::size_t index = 0;
    while (index < text.size()) {
        offsets.push_back(index);
        columns.push_back(column);
        int length = 0;
        const std::uint32_t code = ideUtf8Decode(text, index, length);
        index += static_cast<std::size_t>(length);
        column += static_cast<std::size_t>(ideCodePointCells(code));
    }
    if (offsets.empty()) return std::string();
    std::size_t first = offsets.size();
    std::size_t last = offsets.size();
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        if (first == offsets.size() && columns[i] >= from) first = i;
        if (columns[i] >= from + width) {
            last = i;
            break;
        }
    }
    if (first == offsets.size()) return std::string();
    const std::size_t begin = offsets[first];
    const std::size_t end = (last < offsets.size()) ? offsets[last] : text.size();
    cutRight = last < offsets.size();
    return text.substr(begin, end - begin);
}

std::vector<IdeEntry> ideListDir(const std::string& path) {
    static const std::unordered_set<std::string> ignored = {
        ".git", "node_modules", "target", ".cache", "__pycache__",
    };
    std::vector<IdeEntry> entries;
    std::error_code error;
    for (const auto& item :
         std::filesystem::directory_iterator(path, error)) {
        if (error) break;
        const std::string name = item.path().filename().string();
        if (!name.empty() && name[0] == '.' && name != ".gitignore") continue;
        if (ignored.count(name) != 0) continue;
        bool isDir = item.is_directory(error);
        const bool nonText = !isDir && ideIsNonTextFile(item.path().string());
        entries.push_back(IdeEntry{name, item.path().string(), isDir, nonText});
    }
    std::sort(entries.begin(), entries.end(),
              [](const IdeEntry& a, const IdeEntry& b) {
                  if (a.isDir != b.isDir) return a.isDir;
                  std::string left = a.name, right = b.name;
                  for (char& c : left) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                  for (char& c : right) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                  return left < right;
              });
    return entries;
}

void ideAppendTree(IdeState& state, const std::string& directory, int depth,
                   const std::unordered_set<std::string>& expanded) {
    if (depth > 32) return;
    for (const IdeEntry& entry : ideListDir(directory)) {
        const std::size_t index = state.tree.size();
        state.tree.push_back(IdeTreeRow{entry.name, entry.path, entry.isDir,
                                        depth, false, entry.nonText});
        if (entry.isDir && depth < 32 && expanded.count(entry.path) != 0) {
            state.tree[index].expanded = true;
            ideAppendTree(state, entry.path, depth + 1, expanded);
        }
    }
}

void ideRefreshTree(IdeState& state) {
    std::unordered_set<std::string> expanded;
    for (const IdeTreeRow& row : state.tree) {
        if (row.expanded) expanded.insert(row.path);
    }
    state.tree.clear();
    ideAppendTree(state, state.root, 0, expanded);
    if (state.treeSelection >= state.tree.size()) {
        state.treeSelection = state.tree.empty() ? 0 : state.tree.size() - 1;
    }
}

void ideToggleDir(IdeState& state, std::size_t index) {
    if (index >= state.tree.size() || !state.tree[index].isDir) return;
    const IdeTreeRow row = state.tree[index];
    if (row.expanded) {
        std::size_t end = index + 1;
        while (end < state.tree.size() && state.tree[end].depth > row.depth) ++end;
        state.tree.erase(state.tree.begin() + static_cast<std::ptrdiff_t>(index + 1),
                         state.tree.begin() + static_cast<std::ptrdiff_t>(end));
        state.tree[index].expanded = false;
    } else {
        std::vector<IdeTreeRow> children;
        for (const IdeEntry& entry : ideListDir(row.path)) {
            children.push_back(IdeTreeRow{entry.name, entry.path, entry.isDir,
                                          row.depth + 1, false, entry.nonText});
        }
        state.tree[index].expanded = true;
        state.tree.insert(state.tree.begin() + static_cast<std::ptrdiff_t>(index + 1),
                          children.begin(), children.end());
    }
}

bool ideOpenFile(IdeState& state, const std::string& path);
void ideClosePath(IdeState& state, const std::string& path);
void ideCollapsePath(IdeState& state, const std::string& path);
bool ideAnyDirty(const IdeState& state);

bool ideBufferIsOpen(const IdeState& state, const std::string& path) {
    for (const IdeBuffer& buffer : state.buffers) {
        if (buffer.path == path) return true;
    }
    return false;
}

// Perform the action deferred by the save-all prompt (close a file, collapse a
// folder, quit).
void ideApplyPendingAction(IdeState& state, bool& quit) {
    if (!state.pendingClosePath.empty()) {
        ideClosePath(state, state.pendingClosePath);
    }
    if (!state.pendingCollapsePath.empty()) {
        ideCollapsePath(state, state.pendingCollapsePath);
    }
    if (state.pendingQuit) quit = true;
    state.pendingClosePath.clear();
    state.pendingCollapsePath.clear();
    state.pendingQuit = false;
}

// Close a file, collapse a folder or quit. When any buffer has unsaved edits,
// ask once to save all of them first; with a clean workspace the action happens
// immediately.
void ideRequestSaveAll(IdeState& state, const std::string& closePath,
                       const std::string& collapsePath, bool quitAfter,
                       bool& quit) {
    if (!ideAnyDirty(state)) {
        if (!closePath.empty()) ideClosePath(state, closePath);
        if (!collapsePath.empty()) ideCollapsePath(state, collapsePath);
        if (quitAfter) quit = true;
        state.status = closePath.empty() && collapsePath.empty()
                           ? "Exiting"
                           : state.status;
        return;
    }
    state.pendingClosePath = closePath;
    state.pendingCollapsePath = collapsePath;
    state.pendingQuit = quitAfter;
    state.prompt = IdePrompt::SaveAll;
    state.promptLabel = "Save all changes? (Y/n/c to cancel)";
    state.promptValue.clear();
}

// Open or toggle-close the selected tree row. A folder expands (and the
// selection steps into its first child); selecting an already-expanded folder
// collapses it. A file opens in the editor; selecting an already-open file
// closes it. `openFiles` is false for the Right arrow, used only to walk into
// folders.
void ideActivateTreeSelection(IdeState& state, bool openFiles) {
    if (state.treeSelection >= state.tree.size()) return;
    const IdeTreeRow row = state.tree[state.treeSelection];
    if (!row.isDir) {
        if (!openFiles) return;
        if (row.nonText) {
            state.status = "Cannot open non-text file: " + row.name;
            return;
        }
        if (ideBufferIsOpen(state, row.path)) {
            bool ignored = false;
            ideRequestSaveAll(state, row.path, std::string(), false, ignored);
        } else if (ideOpenFile(state, row.path)) {
            state.explorerFocus = false;
        }
        return;
    }
    if (row.expanded) {
        bool ignored = false;
        ideRequestSaveAll(state, std::string(), row.path, false, ignored);
        return;
    }
    const int depth = row.depth;
    ideToggleDir(state, state.treeSelection);
    if (state.treeSelection + 1 < state.tree.size() &&
        state.tree[state.treeSelection + 1].depth > depth) {
        ++state.treeSelection;
    }
    state.status = "Folder: " + row.name;
}

// Collapse the selected folder, or jump to its parent row with Left.
void ideCollapseOrParent(IdeState& state) {
    if (state.treeSelection >= state.tree.size()) return;
    const IdeTreeRow row = state.tree[state.treeSelection];
    if (row.expanded) {
        ideToggleDir(state, state.treeSelection);
        return;
    }
    if (row.depth == 0) return;
    for (std::size_t i = state.treeSelection; i > 0; --i) {
        if (state.tree[i - 1].depth < row.depth) {
            state.treeSelection = i - 1;
            state.status = "Folder: " + state.tree[i - 1].name;
            return;
        }
    }
}

// The indent of a tree row. A deep tree is indented as far as the pane
// allows, so the name always stays visible; the ellipsis marks that the
// indentation continues.
std::size_t ideTreeIndentWidth(const IdeTreeRow& item, std::size_t width) {
    // The marker and a readable part of the name always fit.
    const std::size_t reserve = 4 + 8;
    const std::size_t full = static_cast<std::size_t>(item.depth) * 2;
    const std::size_t room = width > reserve ? width - reserve : 0;
    return full > room ? room : full;
}

// The marker of a tree row: a folder, a binary file, a Lynxer file or a
// plain one.
std::string ideTreeMarker(const IdeTreeRow& item) {
    if (item.isDir) return item.expanded ? "[-] " : "[+] ";
    if (item.nonText) return " B  ";
    return ideIsLynx(item.path) ? " L  " : " .  ";
}

// The text of one row of the file tree: the indent, the marker and the file
// name. A name wider than the pane is cut off; the arrow keys scroll it, and
// an ellipsis on either side marks the hidden text. A short name is never
// scrolled: it stays whole, so the tree keeps its shape.
std::string ideTreeRowText(const IdeTreeRow& item, std::size_t scroll,
                            std::size_t width) {
    const std::size_t indentWidth = ideTreeIndentWidth(item, width);
    std::string indent(indentWidth, ' ');
    if (indentWidth < static_cast<std::size_t>(item.depth) * 2) {
        indent = "\xE2\x80\xA6" + indent.substr(1);
    }
    const std::string prefix = indent + ideTreeMarker(item);
    const std::size_t used = ideVisibleWidth(prefix);
    if (used >= width) return prefix.substr(0, width);
    const std::size_t space = width - used;
    const std::string name = ideSafeName(item.name);
    const std::size_t nameWidth = ideVisibleWidth(name);
    if (nameWidth <= space) return prefix + name;
    // The ellipses take a column of their own.
    const std::size_t from = std::min(scroll, nameWidth - space);
    std::size_t visible = space;
    if (from > 0) --visible;
    bool cut = false;
    std::string text = ideColumnsSlice(name, from, visible, cut);
    if (cut) {
        if (visible > 1) --visible;
        text = ideColumnsSlice(name, from, visible, cut);
    }
    if (from > 0) text = "\xE2\x80\xA6" + text;
    if (cut) text += "\xE2\x80\xA6";
    return prefix + text;
}

// The columns the widest name of the tree needs, so the scroll range is known.
std::size_t ideTreeNameWidth(const IdeState& state);

// The width of the file tree pane, without the separator column.
int ideTreePaneWidth(const IdeState& state) {
    const int columns = state.columns > 0 ? state.columns : 80;
    return std::max(4, ideExplorerWidth(columns) - 1);
}

// The columns one tree row needs: the indent, the marker and the name.
std::size_t ideTreeRowWidth(const IdeTreeRow& item, std::size_t pane) {
    return ideTreeIndentWidth(item, pane) + 4 +
           ideVisibleWidth(ideSafeName(item.name));
}

// The columns the widest name of the tree needs, so the scroll range is known.
std::size_t ideTreeNameWidth(const IdeState& state) {
    const std::size_t pane = static_cast<std::size_t>(ideTreePaneWidth(state));
    std::size_t widest = 0;
    for (const IdeTreeRow& item : state.tree) {
        const std::size_t total = ideTreeRowWidth(item, pane);
        if (total > widest) widest = total;
    }
    return widest;
}

// The tree row the cursor is on, or nothing when the tree is empty.
const IdeTreeRow* ideTreeSelected(const IdeState& state) {
    if (state.treeSelection >= state.tree.size()) return nullptr;
    return &state.tree[state.treeSelection];
}

// The selected row does not fit the pane, so the arrow keys scroll it instead
// of expanding and collapsing. A short name keeps the old behaviour.
bool ideTreeOverflows(const IdeState& state) {
    const IdeTreeRow* item = ideTreeSelected(state);
    if (item == nullptr) return false;
    const std::size_t pane = static_cast<std::size_t>(ideTreePaneWidth(state));
    return ideTreeRowWidth(*item, pane) > pane;
}

// How far the tree may be scrolled to the right.
std::size_t ideTreeMaxScroll(const IdeState& state) {
    const std::size_t pane = static_cast<std::size_t>(ideTreePaneWidth(state));
    std::size_t widest = 0;
    for (const IdeTreeRow& item : state.tree) {
        const std::size_t total = ideTreeRowWidth(item, pane);
        if (total > widest) widest = total;
    }
    return widest > pane ? widest - pane : 0;
}

// Scrolls so the selected row is visible: the name of a row that is wider
// than the pane starts at its first column, so a short name is always whole.
void ideTreeScrollToSelection(IdeState& state) {
    const IdeTreeRow* item = ideTreeSelected(state);
    if (item == nullptr) return;
    const std::size_t pane = static_cast<std::size_t>(ideTreePaneWidth(state));
    if (ideTreeRowWidth(*item, pane) <= pane) {
        state.treeScroll = 0;
        return;
    }
    // The name of a long row is scrolled; keep the scroll inside the name.
    const std::size_t nameWidth = ideVisibleWidth(ideSafeName(item->name));
    const std::size_t space = pane > 12 ? pane - 12 : 1;
    const std::size_t limit = nameWidth > space ? nameWidth - space : 0;
    if (state.treeScroll > limit) state.treeScroll = limit;
}

// The tree scrolls a few columns per press, so a long name is readable
// without holding a key down for a hundred presses.
constexpr std::size_t kIdeTreeScrollStep = 4;

bool ideHasBuffer(const IdeState& state) {
    return state.active < state.buffers.size();
}
IdeBuffer& ideActive(IdeState& state) {
    return state.buffers[state.active];
}

// The editor column shows a line number and a separator before the text, so
// the text of a row is this many columns narrower than the column. The
// number is at least four digits wide and grows with the file.
int ideGutterColumns(IdeState& state) {
    int digits = 4;
    if (ideHasBuffer(state)) {
        std::size_t count = ideActive(state).lines.size();
        int found = 1;
        while (count >= 10) {
            count /= 10;
            ++found;
        }
        digits = std::max(digits, found);
    }
    return digits + 3;
}

bool ideOpenFile(IdeState& state, const std::string& path) {
    for (std::size_t i = 0; i < state.buffers.size(); ++i) {
        if (state.buffers[i].path == path) {
            state.active = i;
            return true;
        }
    }
    if (ideIsNonTextFile(path)) {
        state.status = "Cannot open non-text file: " + ideBaseName(path);
        return false;
    }
    if (state.buffers.size() >= kIdeMaxBuffers) {
        state.status = "Only " + std::to_string(kIdeMaxBuffers) +
                       " files can be open; close one first";
        return false;
    }
    std::string text, error;
    if (!ideReadText(path, text, error)) {
        state.status = "Error: " + error;
        return false;
    }
    IdeBuffer buffer;
    buffer.path = path;
    buffer.lines = ideSplitLines(text);
    buffer.saved = ideJoinLines(buffer.lines);
    state.buffers.push_back(std::move(buffer));
    state.active = state.buffers.size() - 1;
    state.status = "Opened " + path;
    return true;
}

// Close a buffer by path, used when toggling an already-open file in the tree.
void ideClosePath(IdeState& state, const std::string& path) {
    for (std::size_t i = 0; i < state.buffers.size(); ++i) {
        if (state.buffers[i].path != path) continue;
        state.buffers.erase(state.buffers.begin() +
                            static_cast<std::ptrdiff_t>(i));
        // Closing a buffer before the active one shifts it down; closing the
        // active last buffer falls back to the new last one.
        if (i < state.active) --state.active;
        if (state.active >= state.buffers.size()) {
            state.active = state.buffers.empty() ? 0 : state.buffers.size() - 1;
        }
        if (state.buffers.empty()) state.explorerFocus = true;
        state.status = "Closed " + ideBaseName(path);
        return;
    }
}

// Collapse an expanded folder by path, used when toggling it in the tree.
void ideCollapsePath(IdeState& state, const std::string& path) {
    for (std::size_t i = 0; i < state.tree.size(); ++i) {
        if (state.tree[i].path != path || !state.tree[i].isDir) continue;
        if (state.tree[i].expanded) ideToggleDir(state, i);
        state.status = "Closed folder: " + state.tree[i].name;
        return;
    }
}

void ideSaveAll(IdeState& state) {
    int saved = 0;
    for (IdeBuffer& buffer : state.buffers) {
        if (ideJoinLines(buffer.lines) == buffer.saved) continue;
        std::string error;
        if (!ideWriteText(buffer.path, ideJoinLines(buffer.lines), error)) {
            state.status = "Error: " + error;
            return;
        }
        buffer.saved = ideJoinLines(buffer.lines);
        ++saved;
    }
    state.status = "Saved " + std::to_string(saved) + " file(s)";
}

void ideSaveActive(IdeState& state) {
    if (!ideHasBuffer(state)) return;
    IdeBuffer& buffer = ideActive(state);
    std::string error;
    if (!ideWriteText(buffer.path, ideJoinLines(buffer.lines), error)) {
        state.status = "Error: " + error;
        return;
    }
    buffer.saved = ideJoinLines(buffer.lines);
    state.status = "Saved " + buffer.path;
}

// Keeps the caret inside the visible rows. The view scrolls in visual rows,
// so a long line that wraps over several rows scrolls as one block.
void ideEnsureVisible(IdeState& state, int editorHeight) {
    if (!ideHasBuffer(state)) return;
    IdeBuffer& buffer = ideActive(state);
    if (editorHeight < 1) editorHeight = 1;
    const int width = ideWrapWidth(state);
    const std::size_t row =
        ideVisualRow(buffer, buffer.cursorLine, buffer.cursorColumn, width);
    std::size_t top = buffer.topWrap;
    for (std::size_t i = 0; i < buffer.topLine && i < buffer.lines.size(); ++i) {
        top += static_cast<std::size_t>(ideLineRows(buffer.lines[i], width));
    }
    if (row < top) {
        // Scroll up until the caret is the first visible row.
        buffer.topLine = buffer.cursorLine;
        buffer.topWrap = ideCaretSegment(buffer.lines[buffer.cursorLine],
                                         buffer.cursorColumn, width);
        return;
    }
    if (row >= top + static_cast<std::size_t>(editorHeight)) {
        // Scroll down until the caret is the last visible row.
        const std::size_t target =
            row - static_cast<std::size_t>(editorHeight) + 1;
        ideVisualRowAt(buffer, target, width, buffer.topLine, buffer.topWrap);
    }
}

bool ideFindPosition(const std::string& line, std::size_t& after, int& row,
                     int& column) {
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] != ':') continue;
        std::size_t j = i + 1;
        const std::size_t lineStart = j;
        while (j < line.size() && std::isdigit(static_cast<unsigned char>(line[j])) != 0) ++j;
        if (j == lineStart || j >= line.size() || line[j] != ':') continue;
        const int parsedLine = std::atoi(line.substr(lineStart, j - lineStart).c_str());
        std::size_t k = j + 1;
        const std::size_t columnStart = k;
        while (k < line.size() && std::isdigit(static_cast<unsigned char>(line[k])) != 0) ++k;
        if (k == columnStart || k >= line.size() || line[k] != ':') continue;
        const int parsedColumn = std::atoi(line.substr(columnStart, k - columnStart).c_str());
        after = k + 1;
        row = parsedLine;
        column = parsedColumn;
        return true;
    }
    return false;
}

std::string ideCodeAndMessage(const std::string& rest, std::string& code) {
    const std::size_t open = rest.find('[');
    if (open == std::string::npos) return ideTrim(rest);
    const std::size_t close = rest.find(']', open);
    if (close == std::string::npos) return ideTrim(rest);
    code = rest.substr(open + 1, close - open - 1);
    std::string message = ideTrim(rest.substr(close + 1));
    if (!message.empty() && message[0] == ':') message = ideTrim(message.substr(1));
    return message;
}

std::vector<IdeDiagnostic> ideParseDiagnostics(const std::string& text) {
    std::vector<IdeDiagnostic> out;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        const std::string trimmed = ideTrim(line);
        if (trimmed.empty() || trimmed.rfind("Lint OK", 0) == 0 ||
            trimmed.rfind("help:", 0) == 0 || trimmed[0] == '|') {
            continue;
        }
        std::size_t after = 0;
        int row = 0, column = 0;
        if (!ideFindPosition(line, after, row, column)) {
            if (trimmed.rfind("lynxer:", 0) == 0) {
                out.push_back(IdeDiagnostic{"", 0, 0, "error", "", trimmed});
            }
            continue;
        }
        const std::string rest = ideTrim(line.substr(after));
        if (rest.empty()) continue;
        IdeDiagnostic diagnostic;
        diagnostic.line = row;
        diagnostic.column = column;
        if (rest.rfind("warning[", 0) == 0) {
            diagnostic.severity = "warning";
        } else if (rest.rfind("error[", 0) == 0 || rest[0] == '[') {
            diagnostic.severity = "error";
        } else {
            continue;
        }
        diagnostic.message = ideCodeAndMessage(rest, diagnostic.code);
        if (diagnostic.message.empty()) continue;
        out.push_back(std::move(diagnostic));
    }
    return out;
}

void ideCounts(const IdeState& state, int& errors, int& warnings) {
    errors = 0;
    warnings = 0;
    for (const IdeDiagnostic& diagnostic : state.diagnostics) {
        if (diagnostic.severity == "error") ++errors;
        else if (diagnostic.severity == "warning") ++warnings;
    }
}

void ideRender(IdeState& state) {
    platform::terminalSize(state.columns, state.rows);
    const int columns = state.columns;
    const int rows = state.rows;
    std::vector<std::string> frame(static_cast<std::size_t>(rows), std::string());

    if (columns < 40 || rows < 10) {
        std::cout << "\x1b[2J\x1b[H" << kIdeReset
                  << "Lynxer IDE: terminal too small (need 40x10)\n"
                  << std::flush;
        return;
    }

    // nano-style frame: a reverse-video title bar, the editor body, a message
    // line, then two rows of caret shortcuts. No boxes or panels.
    const int messageRow = rows - 3;
    const int hintStart = rows - 2;
    const int filesRow = rows - 4;
    const int outputHeight = ideOutputHeight(state);
    const int outputStart = filesRow - outputHeight;
    const int mainStart = 2;
    const int mainEnd = outputHeight > 0 ? outputStart - 1 : filesRow - 1;
    const int mainHeight = mainEnd - mainStart + 1;
    const int explorerWidth = ideExplorerWidth(columns);
    // The editor column is filled before the frame is drawn: a soft-wrapped
    // line needs more rows than the buffer has lines.
    std::vector<std::string> editorRows(static_cast<std::size_t>(mainHeight));

    // ---------------------------------------------------------- title bar
    std::ostringstream title;
    title << "  LYNXER IDE " << state.version;
    if (ideHasBuffer(state)) {
        const IdeBuffer& buffer = ideActive(state);
        title << "        File: " << buffer.path;
        if (ideJoinLines(buffer.lines) != buffer.saved) title << "   Modified";
        if (state.buffers.size() > 1) {
            title << "   [" << (state.active + 1) << "/" << state.buffers.size() << "]";
        }
    } else {
        title << "        New Buffer";
    }
    frame[0] = std::string(kIdeReverse) + kIdeBold +
               idePad(ideClamp(title.str(), static_cast<std::size_t>(columns)),
                      static_cast<std::size_t>(columns)) +
               kIdeReset;

    // ------------------------------------------------------------ tab line
    std::string tabs = ideFg(IC_MUTED) + "  ";
    if (state.buffers.empty()) {
        tabs += "No file open    ^O Open    M-^C Commands";
    } else {
        for (std::size_t i = 0; i < state.buffers.size(); ++i) {
            const bool active = i == state.active;
            tabs += (active ? ideFg(IC_ACCENT) : ideFg(IC_MUTED)) +
                    (active ? "[" + ideBaseName(state.buffers[i].path) + "]"
                            : ideBaseName(state.buffers[i].path)) +
                    kIdeReset + "  ";
        }
    }
    ideSetRow(frame, 1, tabs + kIdeReset, columns, IC_SURFACE);

    // --------------------------------------------------------------- body
    std::vector<std::string> helpLines;
    helpLines.push_back("  LYNXER IDE - KEY BINDINGS");
    helpLines.push_back("");
    helpLines.push_back("  Shortcuts (^ = Ctrl, M-^C = Ctrl+Alt+C):");
    for (const IdeCommand& command : ideCommands()) {
        std::ostringstream line;
        line << "    " << std::left << std::setw(6) << command.combo
             << command.label;
        helpLines.push_back(line.str());
    }
    helpLines.push_back("");
    helpLines.push_back("  M-^C / ^A   Command menu: every command, arrow keys + Enter");
    helpLines.push_back("  ^Q          Exit (alias for ^X)");
    helpLines.push_back("  Ctrl+Left / Ctrl+Right   previous / next tab");
    helpLines.push_back("  1-9 / Alt+1-9   switch open file (1-9 in sidebar)");
    helpLines.push_back("");
    helpLines.push_back("  Explorer: Up/Down select files and folders, Enter opens a file");
    helpLines.push_back("            or expands a folder; selecting an open file/folder");
    helpLines.push_back("            closes it (save-all prompt if edits are unsaved)");
    helpLines.push_back("            Right steps into a folder, Left collapses / parent");
    helpLines.push_back("            A long name scrolls with Left / Right");
    helpLines.push_back("  Editor:   arrow keys move, Home/End, PgUp/PgDn, type to edit");
    helpLines.push_back("            Enter newline, Tab indent, Backspace/Delete remove");
    helpLines.push_back("            Left/Right cross line boundaries");
    helpLines.push_back("            Long lines wrap; every row repeats its line number");
    helpLines.push_back("");
    helpLines.push_back("  Output:   ^] moves the focus to the output panel");
    helpLines.push_back("            Up/Down scroll it, PgUp/PgDn page, Home/End ends");
    helpLines.push_back("            any other key hands the focus back to the file");
    helpLines.push_back("");
    helpLines.push_back("  Up to 9 files open at once; the bottom FILES list shows them.");
    helpLines.push_back("  Non-text files (.pdf, .png, .docx, ...) are never opened.");
    helpLines.push_back("  Syntax highlighting is enabled for .lynx files only.");
    helpLines.push_back("  Up/Down scroll; ^G, Esc or q closes this help.");
    const std::size_t maxHelpTop =
        helpLines.size() > static_cast<std::size_t>(mainHeight)
            ? helpLines.size() - static_cast<std::size_t>(mainHeight)
            : 0;
    state.helpTop = std::min(state.helpTop, maxHelpTop);

    const int visibleCommands = std::max(1, mainHeight - 2);
    std::size_t commandTop = 0;
    if (state.commandIndex + 1 > static_cast<std::size_t>(visibleCommands)) {
        commandTop = state.commandIndex + 1 - static_cast<std::size_t>(visibleCommands);
    }
    state.commandTop = commandTop;

    for (int r = 0; r < mainHeight; ++r) {
        const int row = mainStart + r;
        std::string left;
        std::string right;

        if (state.showHelp || state.showCommands) {
            std::string text;
            int color = IC_TEXT;
            bool standout = false;
            if (state.showHelp) {
                const std::size_t index = state.helpTop + static_cast<std::size_t>(r);
                if (index < helpLines.size()) {
                    text = helpLines[index];
                    if (index == 0) color = IC_ACCENT;
                }
            } else if (r == 0) {
                text = "  COMMANDS - Up/Down select, Enter run, Esc close";
                color = IC_ACCENT;
            } else if (r > 1) {
                const std::size_t index = commandTop + static_cast<std::size_t>(r - 2);
                if (index < ideCommands().size()) {
                    const IdeCommand& command = ideCommands()[index];
                    std::ostringstream line;
                    line << (index == state.commandIndex ? "  > " : "    ")
                         << std::left << std::setw(6) << command.combo
                         << command.label;
                    text = line.str();
                    standout = index == state.commandIndex;
                }
            }
            left = (standout ? std::string(kIdeReverse) : std::string()) +
                   ideFg(color) +
                   idePad(text, static_cast<std::size_t>(columns)) + kIdeReset;
            frame[static_cast<std::size_t>(row)] =
                ideClamp(left, static_cast<std::size_t>(columns));
            continue;
        }

        // Explorer column.
        const std::size_t treeIndex = state.treeTop + static_cast<std::size_t>(r);
        if (treeIndex < state.tree.size()) {
            const IdeTreeRow& item = state.tree[treeIndex];
            const bool selected = treeIndex == state.treeSelection && state.explorerFocus;
            left = (selected ? std::string(kIdeReverse) : std::string()) +
                   (item.isDir ? ideFg(IC_ACCENT) :
                                 (item.nonText ? ideFg(IC_ERROR) :
                                  (ideIsLynx(item.path) ? ideFg(IC_OK) : ideFg(IC_MUTED)))) +
                   ideTreeRowText(item, state.treeScroll,
                                  static_cast<std::size_t>(explorerWidth - 1)) +
                   kIdeReset;
        }
        left = idePad(ideClamp(left, static_cast<std::size_t>(explorerWidth - 1)),
                      static_cast<std::size_t>(explorerWidth - 1));
        // The separator between the tree and the editor gets a background of
        // its own, so the two columns never run into each other.
        left += ideBg(IC_PANEL) + ideFg(IC_MUTED) + "|" + kIdeReset;

        // Editor column.
        const int editorWidth = columns - explorerWidth;
        if (ideHasBuffer(state)) {
            IdeBuffer& buffer = ideActive(state);
            const int width = ideWrapWidth(state);
            const bool lynx = ideIsLynx(buffer.path);
            const std::string empty =
                std::string(static_cast<std::size_t>(ideGutterColumns(state)), ' ') +
                "~";
            // A long line wraps over several rows; every row repeats the line
            // number so the wrapped text still reads as one line.
            std::size_t lineIndex = buffer.topLine;
            std::size_t segment = buffer.topWrap;
            for (int row = 0; row < mainHeight; ++row) {
                if (lineIndex >= buffer.lines.size()) {
                    for (int rest = row; rest < mainHeight; ++rest) {
                        editorRows[static_cast<std::size_t>(rest)] =
                            ideFg(IC_GUTTER) + empty + kIdeReset;
                    }
                    break;
                }
                const std::string& text = buffer.lines[lineIndex];
                const std::vector<std::size_t> offsets =
                    ideWrapOffsets(text, width);
                if (segment >= offsets.size()) segment = offsets.size() - 1;
                const std::size_t from = offsets[segment];
                const std::size_t to = (segment + 1 < offsets.size())
                                           ? offsets[segment + 1]
                                           : text.size();
                std::ostringstream gutter;
                // The separator between the line number and the text gets a
                // background of its own, so the gutter never runs into the
                // code.
                gutter << ideFg(IC_GUTTER)
                       << std::setw(ideGutterColumns(state) - 3)
                       << (lineIndex + 1) << ideBg(IC_PANEL) << ideFg(IC_MUTED)
                       << " | " << kIdeReset;
                const bool current = lineIndex == buffer.cursorLine;
                editorRows[static_cast<std::size_t>(row)] =
                    gutter.str() +
                    ideRenderLine(text, lynx, from, to,
                                  buffer.cursorColumn, current) +
                    kIdeReset;
                if (segment + 1 < offsets.size()) {
                    ++segment;
                    continue;
                }
                if (lineIndex + 1 < buffer.lines.size()) {
                    ++lineIndex;
                    segment = 0;
                    continue;
                }
                // The last line of the file is on screen; the rows below it
                // are empty.
                for (int rest = row + 1; rest < mainHeight; ++rest) {
                    editorRows[static_cast<std::size_t>(rest)] =
                        ideFg(IC_GUTTER) + empty + kIdeReset;
                }
                break;
            }
        } else if (r == mainHeight / 2) {
            editorRows[static_cast<std::size_t>(r)] = ideFg(IC_MUTED) +
                "  ^O Open a file, or pick one in the explorer" + kIdeReset;
        }
        right = editorRows[static_cast<std::size_t>(r)];
        frame[static_cast<std::size_t>(row)] =
            ideClamp(left, static_cast<std::size_t>(explorerWidth)) +
            ideClamp(right, static_cast<std::size_t>(editorWidth));
    }

    // ------------------------------------------------------- output panel
    if (outputHeight > 0 && !state.showHelp && !state.showCommands) {
        int errors = 0, warnings = 0;
        ideCounts(state, errors, warnings);
        std::ostringstream panelHeader;
        panelHeader << kIdeBold << ideFg(IC_ACCENT) << " OUTPUT " << kIdeReset
                    << ideFg(errors > 0 ? IC_ERROR : IC_MUTED) << "E " << errors
                    << kIdeReset << "  "
                    << ideFg(warnings > 0 ? IC_WARN : IC_MUTED) << "W " << warnings
                    << kIdeReset << ideFg(IC_MUTED)
                    << "    ^R run  ^L lint  ^T format  ^Y build  ^U run-project"
                    << kIdeReset;
        ideSetRow(frame, outputStart, panelHeader.str(), columns,
                  state.outputFocus ? IC_FOCUS : IC_PANEL);
        const std::vector<std::string> outputLines = ideSplitLines(state.output);
        const int visible = outputHeight - 1;
        const std::size_t first = std::min(state.outputScroll,
                                           outputLines.size());
        for (int i = 0; i < visible; ++i) {
            const std::size_t index = first + static_cast<std::size_t>(i);
            std::string text = index < outputLines.size() ? outputLines[index] : std::string();
            int color = IC_MUTED;
            if (text.find("error") != std::string::npos ||
                text.find("Error") != std::string::npos) color = IC_ERROR;
            else if (text.find("warning") != std::string::npos ||
                     text.find("Warning") != std::string::npos) color = IC_WARN;
            else if (!text.empty() && text[0] == '[') color = IC_INFO;
            ideSetRow(frame, outputStart + 1 + i,
                      ideFg(color) + " " + text + kIdeReset, columns,
                      state.outputFocus ? IC_FOCUS : IC_SURFACE);
        }
    }

    // ---------------------------------------------------- open-files list
    // One row under everything, listing every open buffer with its 1-9 key.
    // The active buffer is highlighted; a trailing * marks unsaved edits.
    {
        std::ostringstream list;
        list << " " << kIdeBold << ideFg(IC_ACCENT) << "FILES " << kIdeReset;
        if (state.buffers.empty()) {
            list << ideFg(IC_MUTED) << "(none - ^O to open)" << kIdeReset;
        } else {
            for (std::size_t i = 0; i < state.buffers.size(); ++i) {
                const IdeBuffer& buffer = state.buffers[i];
                const bool activeBuffer = i == state.active;
                std::string entry = " " + std::to_string(i + 1) + " " +
                                    ideBaseName(buffer.path);
                if (ideJoinLines(buffer.lines) != buffer.saved) entry += "*";
                entry += " ";
                if (activeBuffer) {
                    list << kIdeReverse << ideFg(IC_TEXT) << entry << kIdeReset;
                } else {
                    list << ideFg(IC_MUTED) << entry << kIdeReset;
                }
            }
            list << ideFg(IC_MUTED) << "  ^W close  1-9 / Alt+1-9 switch"
                 << kIdeReset;
        }
        ideSetRow(frame, filesRow, list.str(), columns, IC_SURFACE);
    }

    // ------------------------------------------------------- message line
    // nano shows prompts and status messages on one reverse-video line just
    // above the shortcut bar.
    std::string message;
    if (state.prompt != IdePrompt::None) {
        message = " " + state.promptLabel + ": " + state.promptValue +
                  std::string(kIdeReverse) + " " + "\x1b[27m";
    } else {
        std::ostringstream status;
        if (ideHasBuffer(state)) {
            const IdeBuffer& buffer = ideActive(state);
            // The column is the one on screen: a soft-wrapped line restarts
            // its columns on every row.
            const int width = ideWrapWidth(state);
            const std::vector<std::size_t> offsets =
                ideWrapOffsets(buffer.lines[buffer.cursorLine], width);
            const std::size_t segment = ideCaretSegment(
                buffer.lines[buffer.cursorLine], buffer.cursorColumn, width);
            const std::size_t column = ideColumnsBetween(
                buffer.lines[buffer.cursorLine], offsets[segment],
                buffer.cursorColumn);
            status << " Ln " << (buffer.cursorLine + 1) << ", Col "
                   << (column + 1) << "   "
                   << (ideIsLynx(buffer.path) ? "Lynxer" : "Plain text") << "   UTF-8";
            if (ideJoinLines(buffer.lines) != buffer.saved) status << "   modified";
        } else {
            status << " No file";
        }
        int errors = 0, warnings = 0;
        ideCounts(state, errors, warnings);
        status << "   " << errors << " errors  " << warnings << " warnings";
        if (!state.status.empty()) status << "   " << state.status;
        message = status.str();
    }
    frame[static_cast<std::size_t>(messageRow)] = std::string(kIdeReverse) +
        idePad(ideClamp(message, static_cast<std::size_t>(columns)),
               static_cast<std::size_t>(columns)) + kIdeReset;

    // ------------------------------------------------------- shortcut bar
    // Two rows of nano-style `^X Label` pairs. Overlays and prompts replace
    // them with context-specific keys.
    for (int line = 0; line < 2; ++line) {
        std::string hint;
        if (state.showHelp) {
            hint = line == 0
                       ? " ^G Help   Esc Close   Up/Down Scroll   q Close"
                       : " All actions use Ctrl combos - no function keys";
        } else if (state.showCommands) {
            hint = line == 0
                       ? " Up/Down Select   Enter Run   Esc Close   M-^C Toggle"
                       : " ^G Help   ^X Exit   ^O Open   ^S Save   ^R Run";
        } else if (state.prompt == IdePrompt::SaveAll) {
            hint = line == 0 ? " Y save all   N discard   C cancel   Esc cancel"
                             : " ^S Save   ^W Close   ^X Exit   M-^C Commands";
        } else if (state.prompt != IdePrompt::None) {
            hint = line == 0 ? " Enter Confirm   Esc Cancel   ^G Help"
                             : " ^O Open   ^S Save   ^X Exit   M-^C Commands";
        } else if (state.outputFocus) {
            hint = line == 0
                       ? " Up/Down Scroll   PgUp/PgDn Page   Home/End Ends"
                       : " ^] Back   ^Z Hide   ^G Help   ^X Exit";
        } else if (state.explorerFocus || !ideHasBuffer(state)) {
            hint = line == 0
                       ? " Up/Down Select   Enter Open/Close   Right In   Left Back"
                       : " 1-9 File   ^P Side   ^E Editor   ^W Close   M-^C More";
        } else {
            const std::vector<IdeCommandId>& ids = ideBarShortcuts();
            const std::size_t half = (ids.size() + 1) / 2;
            const std::size_t begin = static_cast<std::size_t>(line) * half;
            const std::size_t end = std::min(begin + half, ids.size());
            hint = " ";
            for (std::size_t i = begin; i < end; ++i) {
                const IdeCommand* command = ideCommandById(ids[i]);
                if (command == nullptr) continue;
                hint += std::string(kIdeBold) + ideFg(IC_ACCENT) +
                        command->combo + kIdeReset + " " + command->bar + "   ";
            }
            if (line == 1) {
                hint += std::string(kIdeBold) + ideFg(IC_ACCENT) + "M-^C" +
                        kIdeReset + " More";
            }
        }
        frame[static_cast<std::size_t>(hintStart + line)] =
            idePad(ideClamp(ideFg(IC_MUTED) + hint + kIdeReset,
                            static_cast<std::size_t>(columns)),
                   static_cast<std::size_t>(columns));
    }

    std::string output;
    for (int i = 0; i < rows; ++i) {
        output += "\x1b[" + std::to_string(i + 1) + ";1H\x1b[0m\x1b[K";
        output += frame[static_cast<std::size_t>(i)];
        output += kIdeReset;
    }
    std::cout << output << std::flush;
}

void ideRunCommand(IdeState& state, const std::vector<std::string>& arguments,
                   const std::string& label, bool lint, bool reload) {
    std::vector<std::string> command = {state.interpreter};
    for (const std::string& argument : arguments) command.push_back(argument);
    state.status = label + " ...";
    ideRender(state);
    platform::CapturedCommand captured;
    std::string error;
    if (!platform::captureProcess(command, captured, error)) {
        state.output = "error: " + error + "\n";
        state.showOutput = true;
        state.status = label + " failed";
        return;
    }
    std::ostringstream out;
    out << label << " [exit " << captured.exitCode << "]\n";
    out << captured.output;
    state.output = out.str();
    if (state.output.empty() || state.output.back() != '\n') state.output += '\n';
    state.showOutput = true;
    // The newest output is what the panel shows.
    ideOutputScrollToEnd(state);
    state.status = label + " finished [exit " +
                   std::to_string(captured.exitCode) + "]";
    if (lint) {
        state.diagnostics = ideParseDiagnostics(captured.output);
        state.diagnosticIndex = 0;
    }
    if (reload && ideHasBuffer(state)) {
        std::string text, readError;
        if (ideReadText(ideActive(state).path, text, readError)) {
            IdeBuffer& buffer = ideActive(state);
            buffer.lines = ideSplitLines(text);
            buffer.saved = ideJoinLines(buffer.lines);
            buffer.cursorLine = std::min(buffer.cursorLine, buffer.lines.size() - 1);
            buffer.cursorColumn = 0;
        }
    }
}

void ideCommandForFile(IdeState& state, const std::string& flag,
                       const std::string& label, bool lint, bool reload) {
    if (!ideHasBuffer(state)) {
        state.status = "Open a file first";
        return;
    }
    ideSaveActive(state);
    std::vector<std::string> arguments;
    if (!flag.empty()) arguments.push_back(flag);
    arguments.push_back(ideActive(state).path);
    ideRunCommand(state, arguments, label, lint, reload);
}

void ideInsertText(IdeState& state, const std::string& text) {
    if (!ideHasBuffer(state)) return;
    IdeBuffer& buffer = ideActive(state);
    std::string& line = buffer.lines[buffer.cursorLine];
    buffer.cursorColumn = std::min(buffer.cursorColumn, line.size());
    line.insert(buffer.cursorColumn, text);
    buffer.cursorColumn += text.size();
}

void ideNewline(IdeState& state) {
    IdeBuffer& buffer = ideActive(state);
    const std::string& line = buffer.lines[buffer.cursorLine];
    const std::string tail = line.substr(buffer.cursorColumn);
    buffer.lines[buffer.cursorLine] = line.substr(0, buffer.cursorColumn);
    buffer.lines.insert(buffer.lines.begin() +
                            static_cast<std::ptrdiff_t>(buffer.cursorLine + 1),
                        tail);
    ++buffer.cursorLine;
    buffer.cursorColumn = 0;
}

void ideBackspace(IdeState& state) {
    IdeBuffer& buffer = ideActive(state);
    if (buffer.cursorColumn > 0) {
        std::string& line = buffer.lines[buffer.cursorLine];
        std::size_t start = buffer.cursorColumn - 1;
        while (start > 0 &&
               (static_cast<unsigned char>(line[start]) & 0xC0) == 0x80) {
            --start;
        }
        line.erase(start, buffer.cursorColumn - start);
        buffer.cursorColumn = start;
    } else if (buffer.cursorLine > 0) {
        const std::string tail = buffer.lines[buffer.cursorLine];
        buffer.cursorColumn = buffer.lines[buffer.cursorLine - 1].size();
        buffer.lines[buffer.cursorLine - 1] += tail;
        buffer.lines.erase(buffer.lines.begin() +
                           static_cast<std::ptrdiff_t>(buffer.cursorLine));
        --buffer.cursorLine;
    }
}

void ideDelete(IdeState& state) {
    IdeBuffer& buffer = ideActive(state);
    std::string& line = buffer.lines[buffer.cursorLine];
    if (buffer.cursorColumn < line.size()) {
        std::size_t end = buffer.cursorColumn + 1;
        while (end < line.size() &&
               (static_cast<unsigned char>(line[end]) & 0xC0) == 0x80) {
            ++end;
        }
        line.erase(buffer.cursorColumn, end - buffer.cursorColumn);
    } else if (buffer.cursorLine + 1 < buffer.lines.size()) {
        line += buffer.lines[buffer.cursorLine + 1];
        buffer.lines.erase(buffer.lines.begin() +
                           static_cast<std::ptrdiff_t>(buffer.cursorLine + 1));
    }
}

// Moves the caret one character left or right inside the line. A wrap
// boundary is a byte offset like any other, so no special case is needed.
void ideMoveCursor(IdeState& state, int deltaColumn) {
    IdeBuffer& buffer = ideActive(state);
    const std::string& line = buffer.lines[buffer.cursorLine];
    if (deltaColumn < 0) {
        std::size_t target = buffer.cursorColumn;
        for (int i = 0; i < -deltaColumn && target > 0; ++i) {
            --target;
            while (target > 0 && (static_cast<unsigned char>(line[target]) & 0xC0) == 0x80) --target;
        }
        buffer.cursorColumn = target;
    } else if (deltaColumn > 0) {
        std::size_t target = buffer.cursorColumn;
        for (int i = 0; i < deltaColumn && target < line.size(); ++i) {
            ++target;
            while (target < line.size() && (static_cast<unsigned char>(line[target]) & 0xC0) == 0x80) ++target;
        }
        buffer.cursorColumn = target;
    }
    buffer.cursorColumn = std::min(buffer.cursorColumn, buffer.lines[buffer.cursorLine].size());
}

// Left and Right cross line boundaries: Left at the start of a line jumps to
// the end of the line above, Right at the end of a line jumps to the start of
// the line below. Without this the caret gets stuck at the first and last
// column of a line.
void ideMoveLeft(IdeState& state) {
    IdeBuffer& buffer = ideActive(state);
    if (buffer.cursorColumn > 0) {
        ideMoveCursor(state, -1);
        return;
    }
    if (buffer.cursorLine == 0) return;
    --buffer.cursorLine;
    buffer.cursorColumn = buffer.lines[buffer.cursorLine].size();
}

void ideMoveRight(IdeState& state) {
    IdeBuffer& buffer = ideActive(state);
    if (buffer.cursorColumn < buffer.lines[buffer.cursorLine].size()) {
        ideMoveCursor(state, 1);
        return;
    }
    if (buffer.cursorLine + 1 >= buffer.lines.size()) return;
    ++buffer.cursorLine;
    buffer.cursorColumn = 0;
}

// Up and Down move by visual row, so a soft-wrapped line is walked one row
// at a time. The caret keeps its column inside the row it lands on.
void ideMoveVertical(IdeState& state, int delta) {
    IdeBuffer& buffer = ideActive(state);
    const int width = ideWrapWidth(state);
    const std::vector<std::size_t> offsets =
        ideWrapOffsets(buffer.lines[buffer.cursorLine], width);
    const std::size_t segment = ideCaretSegment(buffer.lines[buffer.cursorLine],
                                                 buffer.cursorColumn, width);
    const std::size_t screenColumn = ideColumnsBetween(
        buffer.lines[buffer.cursorLine], offsets[segment], buffer.cursorColumn);
    const long row = static_cast<long>(
        ideVisualRow(buffer, buffer.cursorLine, buffer.cursorColumn, width));
    const long target = row + delta;
    if (target < 0) return;
    std::size_t line = 0;
    std::size_t index = 0;
    ideVisualRowAt(buffer, static_cast<std::size_t>(target), width, line, index);
    const std::vector<std::size_t> next = ideWrapOffsets(buffer.lines[line], width);
    const std::size_t start = next[index];
    const std::size_t end = (index + 1 < next.size())
                                ? next[index + 1]
                                : buffer.lines[line].size();
    // The caret stays on the row: on its last character, or past the end of
    // the line when the row is the last one.
    const std::size_t limit = (index + 1 < next.size()) ? end - 1 : end;
    const std::size_t column =
        ideOffsetAfter(buffer.lines[line], start, screenColumn);
    buffer.cursorLine = line;
    buffer.cursorColumn = std::min(column, limit);
}

// Home and End work on the visual row: the start and the end of the
// soft-wrapped segment the caret is in.
void ideMoveToRowStart(IdeState& state) {
    IdeBuffer& buffer = ideActive(state);
    const int width = ideWrapWidth(state);
    const std::vector<std::size_t> offsets =
        ideWrapOffsets(buffer.lines[buffer.cursorLine], width);
    const std::size_t segment = ideCaretSegment(buffer.lines[buffer.cursorLine],
                                                 buffer.cursorColumn, width);
    buffer.cursorColumn = offsets[segment];
}

void ideMoveToRowEnd(IdeState& state) {
    IdeBuffer& buffer = ideActive(state);
    const int width = ideWrapWidth(state);
    const std::vector<std::size_t> offsets =
        ideWrapOffsets(buffer.lines[buffer.cursorLine], width);
    const std::size_t segment = ideCaretSegment(buffer.lines[buffer.cursorLine],
                                                 buffer.cursorColumn, width);
    const std::size_t end = (segment + 1 < offsets.size())
                                ? offsets[segment + 1]
                                : buffer.lines[buffer.cursorLine].size();
    buffer.cursorColumn = (segment + 1 < offsets.size()) ? end - 1 : end;
}

void ideFindNext(IdeState& state) {
    if (!ideHasBuffer(state) || state.findQuery.empty()) {
        state.status = "Nothing to find (use Ctrl+F)";
        return;
    }
    IdeBuffer& buffer = ideActive(state);
    const std::size_t count = buffer.lines.size();
    for (std::size_t step = 0; step < count; ++step) {
        const std::size_t index = (buffer.cursorLine + step) % count;
        std::size_t from = (step == 0) ? buffer.cursorColumn + 1 : 0;
        const std::string& line = buffer.lines[index];
        const std::size_t found = line.find(state.findQuery, from);
        if (found != std::string::npos) {
            buffer.cursorLine = index;
            buffer.cursorColumn = found;
            state.status = "Found '" + state.findQuery + "'";
            return;
        }
    }
    state.status = "Not found: " + state.findQuery;
}

void ideNextDiagnostic(IdeState& state) {
    if (state.diagnostics.empty()) {
        state.status = "No problems";
        return;
    }
    for (std::size_t step = 0; step < state.diagnostics.size(); ++step) {
        state.diagnosticIndex = (state.diagnosticIndex + 1) % state.diagnostics.size();
        const IdeDiagnostic& diagnostic = state.diagnostics[state.diagnosticIndex];
        if (diagnostic.line <= 0 || !ideHasBuffer(state)) continue;
        IdeBuffer& buffer = ideActive(state);
        buffer.cursorLine = std::min(static_cast<std::size_t>(diagnostic.line - 1),
                                     buffer.lines.size() - 1);
        buffer.cursorColumn = static_cast<std::size_t>(std::max(0, diagnostic.column - 1));
        buffer.cursorColumn = std::min(buffer.cursorColumn,
                                       buffer.lines[buffer.cursorLine].size());
        state.status = diagnostic.severity + ": " + diagnostic.message;
        return;
    }
    state.status = "No located problems";
}

void ideConfirmPrompt(IdeState& state, bool& quit) {
    const std::string value = ideTrim(state.promptValue);
    switch (state.prompt) {
        case IdePrompt::Open:
            if (!value.empty()) {
                if (ideOpenFile(state, value)) state.explorerFocus = false;
            }
            break;
        case IdePrompt::New: {
            if (value.empty()) break;
            std::string error;
            if (!ideWriteText(value, "\n", error)) {
                state.status = "Error: " + error;
                break;
            }
            ideRefreshTree(state);
            if (ideOpenFile(state, value)) state.explorerFocus = false;
            break;
        }
        case IdePrompt::Folder: {
            std::error_code error;
            if (!value.empty() && std::filesystem::is_directory(value, error)) {
                state.root = std::filesystem::absolute(value, error).string();
                state.tree.clear();
                state.treeSelection = 0;
                state.treeTop = 0;
                ideRefreshTree(state);
                state.status = "Folder: " + state.root;
            } else {
                state.status = "Not a directory: " + value;
            }
            break;
        }
        case IdePrompt::Goto: {
            const int target = std::atoi(value.c_str());
            if (ideHasBuffer(state) && target > 0) {
                IdeBuffer& buffer = ideActive(state);
                buffer.cursorLine = std::min(static_cast<std::size_t>(target - 1),
                                             buffer.lines.size() - 1);
                buffer.cursorColumn = 0;
                state.status = "Line " + std::to_string(target);
            }
            break;
        }
        case IdePrompt::Find:
            state.findQuery = value;
            ideFindNext(state);
            break;
        case IdePrompt::SaveAll: {
            const bool cancel = value == "c" || value == "C" ||
                                value == "cancel" || value == "Cancel";
            if (cancel) {
                state.status = "Close cancelled";
                state.pendingClosePath.clear();
                state.pendingCollapsePath.clear();
                state.pendingQuit = false;
                break;
            }
            const bool no = value == "n" || value == "N" || value == "no";
            if (!no) ideSaveAll(state);  // default (Y / Enter) saves all
            ideApplyPendingAction(state, quit);
            break;
        }
        case IdePrompt::None:
            break;
    }
    state.prompt = IdePrompt::None;
    state.promptValue.clear();
}

bool ideAnyDirty(const IdeState& state) {
    for (const IdeBuffer& buffer : state.buffers) {
        if (ideJoinLines(buffer.lines) != buffer.saved) return true;
    }
    return false;
}

// Runs one command from either a key combo or the Ctrl+Alt+C menu. Returns
// true when the IDE should exit.
bool ideExecuteCommand(IdeState& state, IdeCommandId id) {
    switch (id) {
        case IdeCommandId::Help:
            state.showHelp = true;
            state.helpTop = 0;
            return false;
        case IdeCommandId::Open:
            state.prompt = IdePrompt::Open;
            state.promptLabel = "Open file";
            state.promptValue.clear();
            return false;
        case IdeCommandId::New:
            state.prompt = IdePrompt::New;
            state.promptLabel = "New file";
            state.promptValue.clear();
            return false;
        case IdeCommandId::Save:
            ideSaveActive(state);
            return false;
        case IdeCommandId::Exit: {
            bool quit = false;
            ideRequestSaveAll(state, std::string(), std::string(), true, quit);
            return quit;
        }
        case IdeCommandId::CloseTab:
            if (ideHasBuffer(state)) {
                bool ignored = false;
                ideRequestSaveAll(state, ideActive(state).path, std::string(),
                                  false, ignored);
            }
            return false;
        case IdeCommandId::Find:
            state.prompt = IdePrompt::Find;
            state.promptLabel = "Find";
            state.promptValue = state.findQuery;
            return false;
        case IdeCommandId::Run:
            ideCommandForFile(state, "", "Run", false, false);
            return false;
        case IdeCommandId::Lint:
            ideCommandForFile(state, "--lint", "Lint", true, false);
            return false;
        case IdeCommandId::Format:
            ideCommandForFile(state, "--format", "Format", false, true);
            return false;
        case IdeCommandId::Output:
            state.showOutput = !state.showOutput;
            return false;
        case IdeCommandId::Goto:
            state.prompt = IdePrompt::Goto;
            state.promptLabel = "Go to line";
            state.promptValue.clear();
            return false;
        case IdeCommandId::NextMatch:
            ideFindNext(state);
            return false;
        case IdeCommandId::NextProblem:
            ideNextDiagnostic(state);
            return false;
        case IdeCommandId::Build:
            ideRunCommand(state, {"--build"}, "Build project", false, false);
            return false;
        case IdeCommandId::RunProject:
            ideRunCommand(state, {"--run"}, "Run project", false, false);
            return false;
        case IdeCommandId::Folder:
            state.prompt = IdePrompt::Folder;
            state.promptLabel = "Open folder";
            state.promptValue = state.root;
            return false;
        case IdeCommandId::Sidebar:
            state.explorerFocus = true;
            state.status = "Sidebar focused";
            return false;
        case IdeCommandId::Editor:
            state.explorerFocus = false;
            state.status = "Editor focused";
            return false;
        case IdeCommandId::Clear:
            state.output.clear();
            state.diagnostics.clear();
            state.outputScroll = 0;
            state.status = "Output cleared";
            return false;
        case IdeCommandId::FocusOutput:
            state.showOutput = true;
            state.outputFocus = !state.outputFocus;
            if (state.outputFocus) ideOutputScrollToEnd(state);
            state.status = state.outputFocus ? "Output: Up/Down scroll, ^] back"
                                             : "Output closed";
            return false;
    }
    return false;
}

const IdeCommand* ideCommandById(IdeCommandId id) {
    for (const IdeCommand& command : ideCommands()) {
        if (command.id == id) return &command;
    }
    return nullptr;
}

bool ideHandleKey(IdeState& state, const platform::KeyEvent& event) {
    bool quit = false;

    // ------------------------------------------------------- prompt editing
    if (state.prompt != IdePrompt::None) {
        if (event.code == 0 && !event.bytes.empty()) {
            const unsigned char c = static_cast<unsigned char>(event.bytes[0]);
            if (c == 27) {
                state.prompt = IdePrompt::None;
                state.promptValue.clear();
                state.pendingClosePath.clear();
                state.pendingCollapsePath.clear();
                state.pendingQuit = false;
                state.status = "Cancelled";
            } else if (c == '\r' || c == '\n') {
                ideConfirmPrompt(state, quit);
            } else if (c == 8 || c == 127) {
                if (!state.promptValue.empty()) state.promptValue.pop_back();
            } else if (c >= 32) {
                state.promptValue += event.bytes;
            }
        }
        return quit;
    }

    const int key = event.code;
    int control = 0;
    if (key == 0 && event.bytes.size() == 1) {
        const unsigned char c = static_cast<unsigned char>(event.bytes[0]);
        if (c < 32) control = static_cast<int>(c);
    }
    const bool escape =
        key == 0 && event.bytes.size() == 1 && event.bytes[0] == 27;
    // Backspace reaches the terminal as BS (8) or as DEL (127) depending on
    // the terminal and the keyboard layout, so both bytes must reach the
    // editor. DEL is not a control character, so it never shows up in
    // `control`.
    const bool backspaceKey =
        key == 0 && event.bytes.size() == 1 &&
        (event.bytes[0] == 8 || event.bytes[0] == 127);
    const bool commandKey = key == platform::KEY_COMMAND || control == 1 ||
                            control == 3;  // ^A / ^C aliases
    // With no file open there is no editor to move a caret in, so the arrow
    // keys browse the tree regardless of the focus flag.
    const bool inExplorer = state.explorerFocus || !ideHasBuffer(state);

    // 1-9 switch open buffers. Plain digits are only shortcuts while the
    // sidebar is focused (typing a digit in the editor must insert it);
    // Alt+1-9 works from anywhere, including during editing.
    int bufferIndex = -1;
    bool focusEditor = false;
    if (key >= platform::KEY_ALT_1 && key <= platform::KEY_ALT_9) {
        bufferIndex = static_cast<int>(key - platform::KEY_ALT_1);
        focusEditor = true;
    } else if (inExplorer && key == 0 && event.bytes.size() == 1 &&
               event.bytes[0] >= '1' && event.bytes[0] <= '9') {
        bufferIndex = static_cast<int>(event.bytes[0] - '1');
    }
    if (bufferIndex >= 0) {
        if (static_cast<std::size_t>(bufferIndex) < state.buffers.size()) {
            state.active = static_cast<std::size_t>(bufferIndex);
            if (focusEditor) state.explorerFocus = false;
            state.status = "File " + std::to_string(bufferIndex + 1) + ": " +
                           ideBaseName(ideActive(state).path);
        } else {
            state.status = "No file " + std::to_string(bufferIndex + 1);
        }
        return false;
    }

    // ------------------------------------------- command menu (Ctrl+Alt+C)
    if (state.showCommands) {
        const std::size_t count = ideCommands().size();
        switch (key) {
            case platform::KEY_UP:
            case platform::KEY_CTRL_UP:
                if (state.commandIndex > 0) --state.commandIndex;
                break;
            case platform::KEY_DOWN:
            case platform::KEY_CTRL_DOWN:
                if (state.commandIndex + 1 < count) ++state.commandIndex;
                break;
            case platform::KEY_HOME: state.commandIndex = 0; break;
            case platform::KEY_END: state.commandIndex = count - 1; break;
            case platform::KEY_PAGE_UP:
                state.commandIndex = state.commandIndex > 5 ? state.commandIndex - 5 : 0;
                break;
            case platform::KEY_PAGE_DOWN:
                state.commandIndex = std::min(state.commandIndex + 5, count - 1);
                break;
            default: break;
        }
        if (control == 13 || control == 10) {  // Enter runs the selection
            const IdeCommandId id = ideCommands()[state.commandIndex].id;
            state.showCommands = false;
            return ideExecuteCommand(state, id);
        }
        if (escape || commandKey || control == 7 ||
            (key == 0 && event.bytes.size() == 1 && event.bytes[0] == 'q')) {
            state.showCommands = false;
            return false;
        }
        // Combos stay live while the menu is open, so ^R runs immediately.
        if (control != 0) {
            for (const IdeCommand& command : ideCommands()) {
                if (command.control != 0 && command.control == control) {
                    state.showCommands = false;
                    return ideExecuteCommand(state, command.id);
                }
            }
        }
        return false;
    }

    // ------------------------------------------------------------- help pane
    if (state.showHelp) {
        if (key == platform::KEY_UP &&
            state.helpTop > 0) --state.helpTop;
        else if (key == platform::KEY_DOWN) ++state.helpTop;
        else if (key == platform::KEY_PAGE_UP)
            state.helpTop = state.helpTop > 5 ? state.helpTop - 5 : 0;
        else if (key == platform::KEY_PAGE_DOWN) state.helpTop += 5;
        else if (key == platform::KEY_HOME) state.helpTop = 0;
        else if (key == platform::KEY_END) state.helpTop = 100;
        else if (control == 7 || escape || commandKey ||
                 (key == 0 && event.bytes.size() == 1 && event.bytes[0] == 'q'))
            state.showHelp = false;
        return false;
    }

    // ------------------------------------ Ctrl+Alt+C opens the command menu
    if (commandKey) {
        state.showCommands = true;
        state.commandIndex = 0;
        state.commandTop = 0;
        return false;
    }

    // ---------------------------------------------------------- global keys
    if (control == 17) {  // ^Q is a nano-familiar alias for ^X exit
        return ideExecuteCommand(state, IdeCommandId::Exit);
    }
    for (const IdeCommand& command : ideCommands()) {
        if (command.control != 0 && command.control == control) {
            return ideExecuteCommand(state, command.id);
        }
    }
    switch (control) {
        case 9:  // Tab
            if (inExplorer) {
                state.explorerFocus = false;
            } else {
                ideInsertText(state, "    ");
            }
            return false;
        case 8:   // Backspace (BS)
            if (!inExplorer) ideBackspace(state);
            return false;
        case 13:  // Enter: open the selected file or folder, else newline
            if (inExplorer) {
                ideActivateTreeSelection(state, true);
            } else {
                ideNewline(state);
            }
            return false;
        default:
            break;
    }
    // Backspace as DEL (127), the byte most terminals send.
    if (backspaceKey) {
        if (!inExplorer) ideBackspace(state);
        return false;
    }

    if (key == platform::KEY_CTRL_LEFT) {
        if (ideHasBuffer(state)) state.active = state.active > 0 ? state.active - 1 : state.buffers.size() - 1;
        return false;
    }
    if (key == platform::KEY_CTRL_RIGHT) {
        if (ideHasBuffer(state)) state.active = (state.active + 1) % state.buffers.size();
        return false;
    }

    // The output panel takes the arrow keys while it has the focus, so the
    // output of a run can be read without leaving the file. Any other key
    // hands the focus back and is handled as usual.
    if (state.outputFocus) {
        const int page = ideOutputRows(state);
        switch (key) {
            case platform::KEY_UP: ideOutputScrollBy(state, -1); return false;
            case platform::KEY_DOWN: ideOutputScrollBy(state, 1); return false;
            case platform::KEY_PAGE_UP: ideOutputScrollBy(state, -page); return false;
            case platform::KEY_PAGE_DOWN: ideOutputScrollBy(state, page); return false;
            case platform::KEY_HOME:
                state.outputScroll = 0;
                return false;
            case platform::KEY_END: ideOutputScrollToEnd(state); return false;
            default: break;
        }
        state.outputFocus = false;
    }

    // A lone Escape moves focus to the explorer, so the arrow keys can be
    // used to navigate the file tree as well as the editor.
    if (escape) {
        state.explorerFocus = true;
        return false;
    }

    if (inExplorer) {
        // A tree wider than its pane scrolls with the arrow keys; otherwise
        // they expand and collapse folders as usual.
        const bool scrolls = ideTreeOverflows(state);
        switch (key) {
            case platform::KEY_UP:
                if (state.treeSelection > 0) --state.treeSelection;
                break;
            case platform::KEY_DOWN:
                if (state.treeSelection + 1 < state.tree.size()) ++state.treeSelection;
                break;
            case platform::KEY_RIGHT:
                if (scrolls) {
                    const std::size_t maxScroll = ideTreeMaxScroll(state);
                    state.treeScroll = std::min(maxScroll,
                                                state.treeScroll + kIdeTreeScrollStep);
                    break;
                }
                // Expand a folder and step into it; files are opened by Enter.
                ideActivateTreeSelection(state, false);
                break;
            case platform::KEY_LEFT:
                if (scrolls && state.treeScroll > 0) {
                    state.treeScroll = state.treeScroll > kIdeTreeScrollStep
                                           ? state.treeScroll - kIdeTreeScrollStep
                                           : 0;
                    break;
                }
                ideCollapseOrParent(state);
                break;
            case platform::KEY_HOME:
                state.treeSelection = 0;
                break;
            case platform::KEY_END:
                if (!state.tree.empty()) state.treeSelection = state.tree.size() - 1;
                break;
            case platform::KEY_PAGE_UP:
                state.treeSelection = state.treeSelection > 10 ? state.treeSelection - 10 : 0;
                break;
            case platform::KEY_PAGE_DOWN:
                state.treeSelection = std::min(state.treeSelection + 10,
                                               state.tree.empty() ? 0 : state.tree.size() - 1);
                break;
            default:
                break;
        }
        // A selected row that is wider than the pane scrolls into view.
        ideTreeScrollToSelection(state);
        // Keep the selection inside the visible explorer pane.
        const int paneHeight = ideEditorHeight(state);
        if (state.treeSelection < state.treeTop) {
            state.treeTop = state.treeSelection;
        } else if (state.treeSelection >=
                   state.treeTop + static_cast<std::size_t>(paneHeight)) {
            state.treeTop = state.treeSelection -
                            static_cast<std::size_t>(paneHeight) + 1;
        }
        return false;
    }

    // ---------------------------------------------------------- editor keys
    switch (key) {
        case platform::KEY_UP: ideMoveVertical(state, -1); break;
        case platform::KEY_DOWN: ideMoveVertical(state, 1); break;
        case platform::KEY_LEFT: ideMoveLeft(state); break;
        case platform::KEY_RIGHT: ideMoveRight(state); break;
        case platform::KEY_HOME:
            ideMoveToRowStart(state);
            break;
        case platform::KEY_END:
            ideMoveToRowEnd(state);
            break;
        case platform::KEY_PAGE_UP:
            ideMoveVertical(state, -std::max(1, state.rows / 2));
            break;
        case platform::KEY_PAGE_DOWN:
            ideMoveVertical(state, std::max(1, state.rows / 2));
            break;
        case platform::KEY_DELETE:
            ideDelete(state);
            break;
        default:
            break;
    }
    if (key == 0 && control == 0 && !event.bytes.empty() &&
        static_cast<unsigned char>(event.bytes[0]) >= 32 &&
        static_cast<unsigned char>(event.bytes[0]) != 127) {
        ideInsertText(state, event.bytes);
    }

    ideEnsureVisible(state, ideEditorHeight(state));
    return false;
}

int launchCliIde(const char* argv0,
                 const std::vector<std::string>& arguments) {
    if (arguments.size() > 1) {
        std::cerr << "lynxer: --cli-ide takes at most one file or folder\n";
        return 1;
    }
    if (!platform::isInteractiveTerminal()) {
        std::cerr << "lynxer: --cli-ide requires an interactive terminal\n";
        std::cerr << "lynxer: stdin and stdout must both be attached to a TTY\n";
        return 1;
    }

    IdeState state;
    state.interpreter = runningExecutable(argv0);
    if (state.interpreter.empty()) {
        std::cerr << "lynxer: could not locate the running executable\n";
        return 1;
    }
    std::error_code pathError;
    const std::filesystem::path absolute =
        std::filesystem::absolute(state.interpreter, pathError);
    if (!pathError) state.interpreter = absolute.string();
    state.version = "Lynxer " +
                    Config::instance().get("version", "0.1.9b3");

    std::error_code cwdError;
    state.root = std::filesystem::current_path(cwdError).string();
    if (cwdError) state.root = ".";

    if (!arguments.empty()) {
        const std::string& target = arguments.front();
        std::error_code error;
        if (std::filesystem::is_directory(target, error)) {
            const std::filesystem::path resolved =
                std::filesystem::absolute(target, error);
            state.root = error ? target : resolved.string();
        } else if (!ideOpenFile(state, target)) {
            std::cerr << "lynxer: " << state.status << '\n';
            return 1;
        }
    }
    ideRefreshTree(state);
    // No file open: start in the explorer so the arrow keys browse files and
    // folders immediately. When a file was passed, focus the editor.
    state.explorerFocus = !ideHasBuffer(state);
    state.status = state.explorerFocus ? "Browse: Up/Down, Enter opens" : "Ready";

    std::string error;
    if (!platform::enableRawInput(error)) {
        std::cerr << "lynxer: " << error << '\n';
        return 1;
    }
    std::cout << "\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H" << std::flush;

    struct Guard {
        ~Guard() {
            std::cout << "\x1b[0m\x1b[?25h\x1b[?1049l" << std::flush;
            platform::disableRawInput();
        }
    } guard;

    while (true) {
        ideRender(state);
        const bool quit = ideHandleKey(state, platform::readKey());
        if (quit) break;
    }
    return 0;
}
