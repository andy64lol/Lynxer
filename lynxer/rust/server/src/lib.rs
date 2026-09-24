//! Lynxer `server` stdlib backend: HTTP/WebSocket server on axum + tokio.
//!
//! Replaces the previous Crow/Boost.Asio backend. The Clynxer-facing contract is
//! unchanged: routes may only be registered while stopped, duplicates and
//! invalid paths are rejected, fixed routes serve HTML, `routeEcho` returns
//! `text/plain` with the request body, WebSocket routes echo text/binary, and
//! `stop()` blocks until the listener is closed.

use std::sync::Mutex;

use axum::extract::ws::{Message, WebSocket, WebSocketUpgrade};
use axum::http::header::CONTENT_TYPE;
use axum::response::Response;
use axum::routing::{get, post};
use axum::Router;

use lynxer_abi::{export_int, export_string, clynxer_module};

#[derive(Clone, Copy, PartialEq, Eq)]
enum Method {
    Get,
    Post,
}

struct Route {
    method: Method,
    path: String,
    body: String,
    content_type: &'static str,
    echo: bool,
}

struct Server {
    port: i64,
    shutdown: Option<tokio::sync::oneshot::Sender<()>>,
    thread: Option<std::thread::JoinHandle<()>>,
}

struct State {
    routes: Vec<Route>,
    ws_paths: Vec<String>,
    server: Option<Server>,
}

static STATE: Mutex<State> = Mutex::new(State {
    routes: Vec::new(),
    ws_paths: Vec::new(),
    server: None,
});

fn error_text(message: &str) -> String {
    format!("ERROR: {message}")
}

fn has_route(routes: &[Route], method: Method, path: &str) -> bool {
    routes
        .iter()
        .any(|route| route.method == method && route.path == path)
}

fn valid_path(path: &str) -> bool {
    !path.is_empty() && path.starts_with('/')
}

// --- WebSocket handlers -----------------------------------------------------

async fn ws_handler(upgrade: WebSocketUpgrade) -> Response {
    upgrade.on_upgrade(handle_socket)
}

async fn handle_socket(mut socket: WebSocket) {
    while let Some(Ok(message)) = socket.recv().await {
        let reply = match message {
            Message::Text(text) => Message::Text(text),
            Message::Binary(bytes) => Message::Binary(bytes),
            Message::Ping(payload) => Message::Pong(payload),
            Message::Pong(_) => continue,
            Message::Close(_) => break,
        };
        if socket.send(reply).await.is_err() {
            break;
        }
    }
}

// --- router -----------------------------------------------------------------

fn build_router(routes: &[Route], ws_paths: &[String]) -> Router {
    let mut router = Router::new();
    for route in routes {
        if route.echo {
            router = router.route(
                &route.path,
                post(|body: String| async move {
                    ([(CONTENT_TYPE, "text/plain")], body)
                }),
            );
        } else {
            let body = route.body.clone();
            let content_type = route.content_type;
            let handler = move || {
                let body = body.clone();
                async move { ([(CONTENT_TYPE, content_type)], body) }
            };
            router = match route.method {
                Method::Get => router.route(&route.path, get(handler)),
                Method::Post => router.route(&route.path, post(handler)),
            };
        }
    }
    for path in ws_paths {
        router = router.route(path, get(ws_handler));
    }
    router
}

// --- ops --------------------------------------------------------------------

export_string!(server_route_get, args, {
    let mut state = STATE.lock().unwrap();
    if state.server.is_some() {
        return error_text("server already running");
    }
    let path = args.string(0).to_string();
    if !valid_path(&path) {
        return error_text("path must start with '/'");
    }
    if has_route(&state.routes, Method::Get, &path) {
        return error_text("GET route already registered");
    }
    state.routes.push(Route {
        method: Method::Get,
        path,
        body: args.string(1).to_string(),
        content_type: "text/html; charset=utf-8",
        echo: false,
    });
    "ok".to_string()
});

export_string!(server_route_post, args, {
    let mut state = STATE.lock().unwrap();
    if state.server.is_some() {
        return error_text("server already running");
    }
    let path = args.string(0).to_string();
    if !valid_path(&path) {
        return error_text("path must start with '/'");
    }
    if has_route(&state.routes, Method::Post, &path) {
        return error_text("POST route already registered");
    }
    state.routes.push(Route {
        method: Method::Post,
        path,
        body: args.string(1).to_string(),
        content_type: "text/html; charset=utf-8",
        echo: false,
    });
    "ok".to_string()
});

export_string!(server_route_echo, args, {
    let mut state = STATE.lock().unwrap();
    if state.server.is_some() {
        return error_text("server already running");
    }
    let path = args.string(0).to_string();
    if !valid_path(&path) {
        return error_text("path must start with '/'");
    }
    if has_route(&state.routes, Method::Post, &path) {
        return error_text("POST route already registered");
    }
    state.routes.push(Route {
        method: Method::Post,
        path,
        body: String::new(),
        content_type: "text/plain",
        echo: true,
    });
    "ok".to_string()
});

export_string!(server_ws_echo, args, {
    let mut state = STATE.lock().unwrap();
    if state.server.is_some() {
        return error_text("server already running");
    }
    let path = args.string(0).to_string();
    if !valid_path(&path) {
        return error_text("path must start with '/'");
    }
    if state.ws_paths.iter().any(|existing| *existing == path) {
        return error_text("WebSocket route already registered");
    }
    state.ws_paths.push(path);
    "ok".to_string()
});

export_string!(server_clear_routes, args, {
    let _ = args;
    let mut state = STATE.lock().unwrap();
    if state.server.is_some() {
        return error_text("server already running");
    }
    state.routes.clear();
    state.ws_paths.clear();
    "ok".to_string()
});

export_string!(server_start, args, {
    let port = args.int(0);
    let mut state = STATE.lock().unwrap();
    if state.server.is_some() {
        return error_text("server already running");
    }
    if port <= 0 || port > 65535 {
        return error_text("invalid port");
    }

    // Bind synchronously so a port clash is reported by `start`, not lost in
    // the server thread.
    let listener = match std::net::TcpListener::bind(("127.0.0.1", port as u16)) {
        Ok(listener) => listener,
        Err(error) => return error_text(&error.to_string()),
    };
    let _ = listener.set_nonblocking(true);

    let router = build_router(&state.routes, &state.ws_paths);
    let (shutdown, wait) = tokio::sync::oneshot::channel::<()>();

    let thread = std::thread::spawn(move || {
        let runtime = match tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
        {
            Ok(runtime) => runtime,
            Err(_) => return,
        };
        runtime.block_on(async move {
            let listener = match tokio::net::TcpListener::from_std(listener) {
                Ok(listener) => listener,
                Err(_) => return,
            };
            let _ = axum::serve(listener, router)
                .with_graceful_shutdown(async move {
                    let _ = wait.await;
                })
                .await;
        });
    });

    state.server = Some(Server {
        port,
        shutdown: Some(shutdown),
        thread: Some(thread),
    });
    "ok".to_string()
});

export_string!(server_stop, args, {
    let _ = args;
    let server = {
        let mut state = STATE.lock().unwrap();
        if state.server.is_none() {
            return error_text("server is not running");
        }
        state.server.take()
    };
    if let Some(mut server) = server {
        if let Some(shutdown) = server.shutdown.take() {
            let _ = shutdown.send(());
        }
        if let Some(thread) = server.thread.take() {
            let _ = thread.join();
        }
    }
    "ok".to_string()
});

export_int!(server_running, args, {
    let _ = args;
    STATE.lock().unwrap().server.is_some() as i64
});

export_int!(server_port, args, {
    let _ = args;
    STATE
        .lock()
        .unwrap()
        .server
        .as_ref()
        .map(|server| server.port)
        .unwrap_or(0)
});

const OPS: &[(&str, &str, &str)] = &[
    ("routeGet", "server_route_get", "cdecl:cstring(...)"),
    ("routePost", "server_route_post", "cdecl:cstring(...)"),
    ("routeEcho", "server_route_echo", "cdecl:cstring(...)"),
    ("wsEcho", "server_ws_echo", "cdecl:cstring(...)"),
    ("clearRoutes", "server_clear_routes", "cdecl:cstring(...)"),
    ("start", "server_start", "cdecl:cstring(...)"),
    ("stop", "server_stop", "cdecl:cstring(...)"),
    ("running", "server_running", "cdecl:int64(...)"),
    ("port", "server_port", "cdecl:int64(...)"),
];

clynxer_module!(OPS);
