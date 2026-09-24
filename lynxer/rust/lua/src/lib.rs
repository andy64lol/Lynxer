//! Lynxer `lua` stdlib backend using an embedded Lua 5.4 runtime.

use lynxer_abi::{export_int, export_string, clynxer_module};
use mlua::{Lua, Value, Variadic};
use std::fs;
use std::rc::Rc;
use std::cell::RefCell;

fn value_text(value: Value) -> String {
    match value {
        Value::Nil => "nil".to_string(),
        Value::Boolean(value) => value.to_string(),
        Value::Integer(value) => value.to_string(),
        Value::Number(value) => value.to_string(),
        Value::String(value) => value.to_str().map(|text| text.to_string()).unwrap_or_default(),
        other => format!("{other:?}"),
    }
}

fn run_source(source: &str, name: &str) -> String {
    let lua = Lua::new();
    let output = Rc::new(RefCell::new(Vec::<String>::new()));
    let output_sink = Rc::clone(&output);
    let print = match lua.create_function(move |_, values: Variadic<Value>| {
        output_sink
            .borrow_mut()
            .push(values.into_iter().map(value_text).collect::<Vec<_>>().join("\t"));
        Ok(())
    }) {
        Ok(function) => function,
        Err(error) => return format!("Error: {error}"),
    };

    if let Err(error) = lua.globals().set("print", print) {
        return format!("Error: {error}");
    }
    if let Err(error) = lua.load(source).set_name(name).exec() {
        return format!("Error: {error}");
    }

    let lines = output.borrow();
    if lines.is_empty() {
        String::new()
    } else {
        format!("{}\n", lines.join("\n"))
    }
}

fn eval_source(expression: &str) -> String {
    let lua = Lua::new();
    // Without an explicit name mlua names the chunk after the Rust call site,
    // which would leak `lua/src/lib.rs:51` into the user-facing error.
    let chunk = lua
        .load(format!("return ({expression})"))
        .set_name("lynxer.lua");
    match chunk.eval::<Value>() {
        Ok(value) => value_text(value),
        Err(error) => format!("Error: {error}"),
    }
}

export_string!(lua_run, args, {
    run_source(args.string(0), "lynxer.lua")
});

export_string!(lua_run_file, args, {
    let path = args.string(0);
    match fs::read_to_string(path) {
        Ok(source) => run_source(&source, path),
        Err(error) => format!("Error: {error}"),
    }
});

export_string!(lua_eval, args, {
    eval_source(args.string(0))
});

export_string!(lua_version_op, _args, {
    "Lua 5.4".to_string()
});

export_int!(lua_exists, _args, {
    1
});

const OPS: &[(&str, &str, &str)] = &[
    ("runLua", "lua_run", "cdecl:cstring(...)"),
    ("runLuaFile", "lua_run_file", "cdecl:cstring(...)"),
    ("evalLua", "lua_eval", "cdecl:cstring(...)"),
    ("luaVersion", "lua_version_op", "cdecl:cstring(...)"),
    ("luaExists", "lua_exists", "cdecl:int64(...)"),
];

clynxer_module!(OPS);
