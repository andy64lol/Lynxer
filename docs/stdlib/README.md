# Lynxer Standard Library

This directory contains documentation for all Lynxer standard library modules.

## Modules

| Module | File | What it provides |
|--------|------|-----------------|
| [colorlib](colorlib.md) | `colorlib.lynx` | ANSI colour helpers for terminal output |
| [csv](csv.md) | `csv.lynx` | CSV read, write, parse, filter, and transform |
| [debug](debug.md) | `debug.lynx` | Assertions, type inspection, structured logging, timers |
| [fileIO](fileIO.md) | `fileIO.lynx` | File reading, writing, copying, moving, and temp files |
| [game](game.md) | `game.lynx` | 2-D game development: window, shapes, sprites, scenes, tilemap, camera, physics (Arcade) |
| [image](image.md) | `image.lynx` | Image processing: load, resize, filter, draw, composite, base64 (Pillow) |
| [http](http.md) | `http.lynx` | Simple HTTP client (GET, POST, PUT, DELETE, download) |
| [js](js.md) | `js.lynx` | Run JavaScript via Node.js |
| [lua](lua.md) | `lua.lynx` | Run embedded Lua via lupa |
| [json](json.md) | `json.lynx` | JSON encode / decode / query / mutate |
| [math](math.md) | `math.lynx` | Math utilities, trig, rounding, statistics (NumPy) |
| [multiprocessing](../multiprocessing.md) | `multiprocessing.lynx` | Run shell commands in parallel using process/thread pools |
| [net](net.md) | `net.lynx` | WebSocket client, TCP client, hostname/IP/URL utilities |
| [os](os.md) | `os.lynx` | OS and filesystem helpers (dirs, paths, env vars) |
| [path](path.md) | `path.lynx` | `pathlib.Path` path manipulation, traversal, filesystem, and text helpers |
| [random](random.md) | `random.lynx` | Random number and sequence utilities |
| [re](re.md) | `re.lynx` | Regular expressions (search, match, sub, split, groups) |
| [regex](regex.md) | `regex.lynx` | Extended regex: compiled cache, Unicode, advanced helpers |
| [server](server.md) | `server.lynx` | Full-featured HTTP server (Flask): routes, JSON API, Jinja2 templates, static sites, CORS |
| [shell](shell.md) | `shell.lynx` | Run external shell commands, capture output |
| [sqldb](sqldb.md) | `sqldb.lynx` | SQLite database operations through Python `sqlite3` |
| [sys](sys.md) | `sys.lynx` | Runtime info: version, platform, argv, path, exit, recursion limit |
| [time](time.md) | `time.lynx` | Date/time helpers |
| [tkinter](tkinter.md) | `tkinter.lynx` | Comprehensive GUI: windows, widgets, menus, canvas, dialogs; includes CTk extension (`ctk*` functions) |
| [turtle](turtle.md) | `turtle.lynx` | Turtle graphics: drawing, shapes, animation, events |
| [typing](typing.md) | `typing.lynx` | String/list/type conversion utilities |
| [tui](tui.md) | `tui.lynx` | Rich terminal UI rendering and prompts |
| [venv](venv.md) | `venv.lynx` | Python virtual environment lifecycle and package management |

## Usage

Import any module inside `setup()`:

```c
global setup(){
    import("math");
    import("typing");
    import("json");
    import("re");
    import("regex");
    import("fileIO");
    import("csv");
    import("debug");
    import("http");
    import("net");
    import("os");
    import("path");
    import("random");
    import("time");
    import("shell");
    import("sys");
    import("server");
    import("tkinter");
    import("turtle");
    import("game");
    import("image");
    import("venv");
}
```

All functions are called as `global.<module>.<function>(...)`.

Each module's documentation page lists every available function with its signature, description, and examples.
