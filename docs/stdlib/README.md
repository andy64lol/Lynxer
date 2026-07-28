# Lynxer Standard Library

This directory contains documentation for all Lynxer standard library modules.

## Modules

| Module | File | What it provides |
|--------|------|-----------------|
| [colorlib](colorlib.md) | `colorlib.lynx` | ANSI colour helpers for terminal output |
| [fileIO](fileIO.md) | `fileIO.lynx` | File reading, writing, copying, moving, and temp files |
| [http](http.md) | `http.lynx` | Simple HTTP client (GET, POST) |
| [js](js.md) | `js.lynx` | Run JavaScript via Node.js |
| [json](json.md) | `json.lynx` | JSON encode / decode / query / mutate |
| [math](math.md) | `math.lynx` | Math utilities, trig, rounding, random numbers |
| [os](os.md) | `os.lynx` | OS and filesystem helpers (dirs, paths, env vars) |
| [random](random.md) | `random.lynx` | Random number and sequence utilities |
| [re](re.md) | `re.lynx` | Regular expressions (search, match, sub, split, groups) |
| [server](server.md) | `server.lynx` | Full-featured HTTP server (Flask): routes, JSON API, Jinja2 templates, static sites, CORS |
| [shell](shell.md) | `shell.lynx` | Run external shell commands, capture output |
| [sys](sys.md) | `sys.lynx` | Runtime info: version, platform, argv, path, exit, recursion limit |
| [time](time.md) | `time.lynx` | Date/time helpers |
| [tkinter](tkinter.md) | `tkinter.lynx` | Comprehensive GUI: windows, widgets, menus, canvas, dialogs |
| [turtle](turtle.md) | `turtle.lynx` | Turtle graphics: drawing, shapes, animation, events |
| [typing](typing.md) | `typing.lynx` | String/list/type conversion utilities |

## Usage

Import any module inside `setup()`:

```c
global setup(){
    import("server");
    import("re");
    import("sys");
    import("tkinter");
}
```

All functions are called as `global.<module>.<function>(...)`.

Each module's documentation page lists every available function with its signature, description, and examples.
