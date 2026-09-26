//! Lynxer `server` stdlib backend: HTTP/WebSocket server on axum + tokio.
//!
//! Replaces the previous Crow/Boost.Asio backend, then the Flask-backed
//! original. Routes are registered while the server is stopped and served from
//! a single dispatcher, so every route kind — fixed text, JSON, templates,
//! files, redirects, static trees — shares one code path for CORS, global
//! headers, request logging and the custom error bodies.
//!
//! Request-context readers (`getArg`, `getHeader`, …) return values from the
//! **most recently handled request**. A Lynxer route is a fixed string, not a
//! callback, and the interpreter evaluates one frame at a time, so there is no
//! point at which such a reader could run *inside* a request. `run()` starts the
//! listener and then blocks, matching the original.

use std::net::SocketAddr;
use std::sync::{Mutex, MutexGuard};

use axum::body::Body;
use axum::extract::ws::{Message, WebSocket, WebSocketUpgrade};
use axum::extract::{ConnectInfo, Request};
use axum::http::header::CONTENT_TYPE;
use axum::http::{HeaderName, HeaderValue, Method, StatusCode};
use axum::response::Response;
use axum::routing::get;
use axum::Router;

use lynxer_abi::{export_int, export_string, lynxer_module};

// --- route model ------------------------------------------------------------

const METHOD_GET: u8 = 1;
const METHOD_POST: u8 = 2;
const METHOD_PUT: u8 = 4;
const METHOD_DELETE: u8 = 8;
const METHOD_PATCH: u8 = 16;
const METHOD_ALL: u8 = METHOD_GET | METHOD_POST | METHOD_PUT | METHOD_DELETE | METHOD_PATCH;

/// The method mask a request matches. `HEAD` is served by `GET` routes, which is
/// what the previous backend did and what `network.httpHead` relies on.
fn request_flag(method: &Method) -> u8 {
    if *method == Method::HEAD {
        return METHOD_GET;
    }
    match *method {
        Method::GET => METHOD_GET,
        Method::POST => METHOD_POST,
        Method::PUT => METHOD_PUT,
        Method::DELETE => METHOD_DELETE,
        Method::PATCH => METHOD_PATCH,
        _ => 0,
    }
}

fn method_label(mask: u8) -> &'static str {
    match mask {
        METHOD_GET => "GET",
        METHOD_POST => "POST",
        METHOD_PUT => "PUT",
        METHOD_DELETE => "DELETE",
        METHOD_PATCH => "PATCH",
        _ => "route",
    }
}

enum RouteBody {
    Text {
        body: String,
        content_type: &'static str,
        status: u16,
    },
    Template {
        file: Option<String>,
        inline: Option<String>,
        data: String,
    },
    File {
        path: String,
    },
    Redirect {
        target: String,
        status: u16,
    },
    Echo,
}

struct Route {
    methods: u8,
    path: String,
    body: RouteBody,
}

struct StaticTree {
    prefix: String,
    directory: String,
}

/// The most recently handled request, which the `get*` readers expose.
struct LastRequest {
    method: String,
    path: String,
    url: String,
    query: String,
    body: String,
    content_type: String,
    remote_addr: String,
    headers: Vec<(String, String)>,
    cookies: Vec<(String, String)>,
}

struct Config {
    host: String,
    port: i64,
    debug: bool,
    template_folder: String,
    cors: Option<String>,
    global_headers: Vec<(String, String)>,
    request_log: bool,
    not_found: Option<String>,
    server_error: Option<String>,
    forbidden: Option<String>,
    method_not_allowed: Option<String>,
    static_trees: Vec<StaticTree>,
    static_site: Option<String>,
    last_request: Option<LastRequest>,
}

impl Config {
    const fn new() -> Self {
        Config {
            host: String::new(),
            port: 0,
            debug: false,
            template_folder: String::new(),
            cors: None,
            global_headers: Vec::new(),
            request_log: false,
            not_found: None,
            server_error: None,
            forbidden: None,
            method_not_allowed: None,
            static_trees: Vec::new(),
            static_site: None,
            last_request: None,
        }
    }

    /// A copy without the per-request record, safe to hand to a response
    /// builder outside the lock.
    fn snapshot(&self) -> Config {
        Config {
            host: self.host.clone(),
            port: self.port,
            debug: self.debug,
            template_folder: self.template_folder.clone(),
            cors: self.cors.clone(),
            global_headers: self.global_headers.clone(),
            request_log: self.request_log,
            not_found: self.not_found.clone(),
            server_error: self.server_error.clone(),
            forbidden: self.forbidden.clone(),
            method_not_allowed: self.method_not_allowed.clone(),
            static_trees: self
                .static_trees
                .iter()
                .map(|tree| StaticTree {
                    prefix: tree.prefix.clone(),
                    directory: tree.directory.clone(),
                })
                .collect(),
            static_site: self.static_site.clone(),
            last_request: None,
        }
    }
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
    config: Config,
}

static STATE: Mutex<State> = Mutex::new(State {
    routes: Vec::new(),
    ws_paths: Vec::new(),
    server: None,
    config: Config::new(),
});

fn error_text(message: &str) -> String {
    format!("ERROR: {message}")
}

fn lock_state() -> MutexGuard<'static, State> {
    STATE
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}

fn has_route(routes: &[Route], methods: u8, path: &str) -> bool {
    routes
        .iter()
        .any(|route| route.methods & methods != 0 && route.path == path)
}

fn valid_path(path: &str) -> bool {
    !path.is_empty() && path.starts_with('/')
}

/// Registers a route, rejecting duplicates of the same mask and path. Every
/// route-registration op shares this so the messages stay uniform.
fn register(methods: u8, path: &str, body: RouteBody) -> String {
    let mut state = lock_state();
    if state.server.is_some() {
        return error_text("server already running");
    }
    if !valid_path(path) {
        return error_text("path must start with '/'");
    }
    if has_route(&state.routes, methods, path) {
        return error_text(&format!(
            "{} route already registered",
            method_label(methods)
        ));
    }
    state.routes.push(Route {
        methods,
        path: path.to_string(),
        body,
    });
    "ok".to_string()
}

fn text_route(
    methods: u8,
    path: &str,
    body: &str,
    content_type: &'static str,
    status: u16,
) -> String {
    register(
        methods,
        path,
        RouteBody::Text {
            body: body.to_string(),
            content_type,
            status,
        },
    )
}

// --- WebSocket handler ------------------------------------------------------

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

// --- request helpers --------------------------------------------------------

fn parse_pairs(text: &str) -> Vec<(String, String)> {
    form_urlencoded::parse(text.as_bytes())
        .map(|(key, value)| (key.into_owned(), value.into_owned()))
        .collect()
}

fn pairs_lookup(pairs: &[(String, String)], name: &str) -> String {
    pairs
        .iter()
        .find(|(key, _)| key == name)
        .map(|(_, value)| value.clone())
        .unwrap_or_default()
}

fn parse_cookies(headers: &[(String, String)]) -> Vec<(String, String)> {
    headers
        .iter()
        .filter(|(name, _)| name.eq_ignore_ascii_case("cookie"))
        .flat_map(|(_, value)| value.split(';'))
        .filter_map(|pair| pair.split_once('='))
        .map(|(name, value)| (name.trim().to_string(), value.trim().to_string()))
        .collect()
}

fn header_lookup(headers: &[(String, String)], name: &str) -> String {
    headers
        .iter()
        .find(|(key, _)| key.eq_ignore_ascii_case(name))
        .map(|(_, value)| value.clone())
        .unwrap_or_default()
}

fn content_type_for(path: &str) -> &'static str {
    match path
        .rsplit('.')
        .next()
        .map(|extension| extension.to_ascii_lowercase())
        .as_deref()
    {
        Some("html") | Some("htm") => "text/html; charset=utf-8",
        Some("css") => "text/css; charset=utf-8",
        Some("js") | Some("mjs") => "text/javascript; charset=utf-8",
        Some("json") => "application/json",
        Some("txt") => "text/plain; charset=utf-8",
        Some("svg") => "image/svg+xml",
        Some("png") => "image/png",
        Some("jpg") | Some("jpeg") => "image/jpeg",
        Some("gif") => "image/gif",
        Some("webp") => "image/webp",
        Some("ico") => "image/x-icon",
        Some("wasm") => "application/wasm",
        Some("pdf") => "application/pdf",
        Some("xml") => "application/xml",
        _ => "application/octet-stream",
    }
}

/// Joins a URL suffix onto a root directory, refusing to climb out of it.
fn safe_join(directory: &str, relative: &str) -> Option<std::path::PathBuf> {
    let mut path = std::path::PathBuf::from(directory);
    for part in relative.split('/') {
        if part.is_empty() || part == "." {
            continue;
        }
        if part == ".." {
            return None;
        }
        path.push(part);
    }
    Some(path)
}

/// Renders `{{ name }}` (and dotted `{{ a.b }}`) from a JSON object. The
/// original used Jinja2; this is the documented substitution subset — no loops
/// or conditionals.
fn render_template(source: &str, data: &str) -> String {
    let values: serde_json::Value = serde_json::from_str(data).unwrap_or(serde_json::Value::Null);
    let mut output = String::with_capacity(source.len());
    let mut rest = source;
    while let Some(start) = rest.find("{{") {
        output.push_str(&rest[..start]);
        let after = &rest[start + 2..];
        match after.find("}}") {
            Some(end) => {
                let key = after[..end].trim();
                output.push_str(&template_value(&values, key));
                rest = &after[end + 2..];
            }
            None => {
                output.push_str(&rest[start..]);
                return output;
            }
        }
    }
    output.push_str(rest);
    output
}

fn template_value(values: &serde_json::Value, key: &str) -> String {
    let mut current = values;
    for part in key.split('.') {
        match current.get(part.trim()) {
            Some(next) => current = next,
            None => return String::new(),
        }
    }
    match current {
        serde_json::Value::String(text) => text.clone(),
        serde_json::Value::Null => String::new(),
        other => other.to_string(),
    }
}

fn decorate(
    builder: axum::http::response::Builder,
    config: &Config,
) -> axum::http::response::Builder {
    let mut builder = builder;
    for (key, value) in &config.global_headers {
        if let (Ok(name), Ok(value)) = (
            HeaderName::from_bytes(key.as_bytes()),
            HeaderValue::from_str(value),
        ) {
            builder = builder.header(name, value);
        }
    }
    if let Some(origin) = &config.cors {
        builder = builder.header("access-control-allow-origin", origin.as_str());
    }
    builder
}

fn response(status: u16, content_type: &str, body: String, config: &Config) -> Response {
    let builder = Response::builder()
        .status(StatusCode::from_u16(status).unwrap_or(StatusCode::INTERNAL_SERVER_ERROR))
        .header(CONTENT_TYPE, content_type);
    decorate(builder, config)
        .body(Body::from(body))
        .unwrap_or_else(|_| Response::new(Body::from(String::new())))
}

fn error_body(config: &Config, status: u16) -> String {
    let custom = match status {
        403 => config.forbidden.clone(),
        404 => config.not_found.clone(),
        405 => config.method_not_allowed.clone(),
        500 => config.server_error.clone(),
        _ => None,
    };
    custom.unwrap_or_else(|| {
        let reason = StatusCode::from_u16(status)
            .ok()
            .and_then(|code| code.canonical_reason())
            .unwrap_or("Error");
        format!("{status} {reason}")
    })
}

// --- dispatcher -------------------------------------------------------------

enum Plan {
    Route(usize),
    File(Option<std::path::PathBuf>),
    Preflight,
    MethodNotAllowed,
    NotFound,
}

async fn dispatch(ConnectInfo(address): ConnectInfo<SocketAddr>, request: Request) -> Response {
    let (parts, body) = request.into_parts();
    let bytes = axum::body::to_bytes(body, 8 * 1024 * 1024)
        .await
        .unwrap_or_default();
    let raw_body = String::from_utf8_lossy(&bytes).into_owned();

    let path = parts.uri.path().to_string();
    let query = parts.uri.query().unwrap_or("").to_string();
    let method = parts.method.as_str().to_string();
    let content_type = parts
        .headers
        .get(CONTENT_TYPE)
        .and_then(|value| value.to_str().ok())
        .unwrap_or("")
        .to_string();
    let headers: Vec<(String, String)> = parts
        .headers
        .iter()
        .filter_map(|(name, value)| {
            value
                .to_str()
                .ok()
                .map(|value| (name.as_str().to_string(), value.to_string()))
        })
        .collect();
    let cookies = parse_cookies(&headers);
    let host = header_lookup(&headers, "host");
    let uri = if query.is_empty() {
        path.clone()
    } else {
        format!("{path}?{query}")
    };
    let url = if host.is_empty() {
        uri.clone()
    } else {
        format!("http://{host}{uri}")
    };

    let (log, plan, config) = {
        let mut state = lock_state();
        let log = state.config.request_log || state.config.debug;
        if parts.method != Method::OPTIONS {
            state.config.last_request = Some(LastRequest {
                method: method.clone(),
                path: path.clone(),
                url: url.clone(),
                query: query.clone(),
                body: raw_body.clone(),
                content_type: content_type.clone(),
                remote_addr: address.ip().to_string(),
                headers: headers.clone(),
                cookies: cookies.clone(),
            });
        }

        let flag = request_flag(&parts.method);
        let mut plan = Plan::NotFound;
        if parts.method == Method::OPTIONS {
            plan = if state.config.cors.is_some() {
                Plan::Preflight
            } else {
                Plan::MethodNotAllowed
            };
        } else if flag == 0 {
            plan = Plan::MethodNotAllowed;
        } else {
            let mut matched: Option<usize> = None;
            let mut allowed = 0u8;
            for (index, route) in state.routes.iter().enumerate() {
                if route.path != path {
                    continue;
                }
                allowed |= route.methods;
                if matched.is_none() && route.methods & flag != 0 {
                    matched = Some(index);
                }
            }
            plan = match matched {
                Some(index) => Plan::Route(index),
                None if allowed != 0 => Plan::MethodNotAllowed,
                None => {
                    let tree = state.config.static_trees.iter().find(|tree| {
                        path.starts_with(&tree.prefix)
                            && (path.len() == tree.prefix.len()
                                || path.as_bytes()[tree.prefix.len()] == b'/')
                    });
                    if let Some(tree) = tree {
                        Plan::File(safe_join(&tree.directory, &path[tree.prefix.len()..]))
                    } else if let Some(directory) = state.config.static_site.clone() {
                        let rest = if path == "/" { "/index.html" } else { &path };
                        Plan::File(safe_join(&directory, rest))
                    } else {
                        Plan::NotFound
                    }
                }
            };
        }
        (log, plan, state.config.snapshot())
    };

    // Log before building the response, so the line always precedes the client
    // seeing it.
    if log {
        println!("{method} {path}");
    }

    match plan {
        Plan::Preflight => {
            let builder = Response::builder()
                .status(StatusCode::NO_CONTENT)
                .header(
                    "access-control-allow-methods",
                    "GET, POST, PUT, DELETE, PATCH",
                )
                .header("access-control-allow-headers", "content-type");
            decorate(builder, &config)
                .body(Body::empty())
                .unwrap_or_else(|_| Response::new(Body::empty()))
        }
        Plan::MethodNotAllowed => response(
            405,
            "text/plain; charset=utf-8",
            error_body(&config, 405),
            &config,
        ),
        Plan::NotFound => response(
            404,
            "text/plain; charset=utf-8",
            error_body(&config, 404),
            &config,
        ),
        Plan::File(path) => match path.and_then(|file| {
            std::fs::read(&file)
                .ok()
                .map(|bytes| (file, String::from_utf8_lossy(&bytes).into_owned()))
        }) {
            Some((file, text)) => response(
                200,
                content_type_for(&file.to_string_lossy()),
                text,
                &config,
            ),
            None => response(
                404,
                "text/plain; charset=utf-8",
                error_body(&config, 404),
                &config,
            ),
        },
        Plan::Route(index) => route_response(index, &raw_body, &config),
    }
}

fn route_response(index: usize, request_body: &str, config: &Config) -> Response {
    let state = lock_state();
    let Some(route) = state.routes.get(index) else {
        return response(
            404,
            "text/plain; charset=utf-8",
            error_body(config, 404),
            config,
        );
    };
    match &route.body {
        RouteBody::Text {
            body,
            content_type,
            status,
        } => response(*status, content_type, body.clone(), config),
        RouteBody::Template { file, inline, data } => {
            let source = match (file, inline) {
                (Some(path), _) => {
                    let folder = &state.config.template_folder;
                    let full = if folder.is_empty() {
                        std::path::PathBuf::from(path)
                    } else {
                        safe_join(folder, path).unwrap_or_else(|| std::path::PathBuf::from(path))
                    };
                    match std::fs::read_to_string(&full) {
                        Ok(text) => text,
                        Err(_) => {
                            return response(
                                500,
                                "text/plain; charset=utf-8",
                                error_body(config, 500),
                                config,
                            )
                        }
                    }
                }
                (_, Some(text)) => text.clone(),
                _ => String::new(),
            };
            response(
                200,
                "text/html; charset=utf-8",
                render_template(&source, data),
                config,
            )
        }
        RouteBody::File { path } => match std::fs::read(path) {
            Ok(bytes) => response(
                200,
                content_type_for(path),
                String::from_utf8_lossy(&bytes).into_owned(),
                config,
            ),
            Err(_) => response(
                404,
                "text/plain; charset=utf-8",
                error_body(config, 404),
                config,
            ),
        },
        RouteBody::Redirect { target, status } => {
            let builder = Response::builder()
                .status(StatusCode::from_u16(*status).unwrap_or(StatusCode::FOUND))
                .header("location", target.as_str());
            decorate(builder, config)
                .body(Body::empty())
                .unwrap_or_else(|_| Response::new(Body::empty()))
        }
        RouteBody::Echo => response(200, "text/plain", request_body.to_string(), config),
    }
}

// --- server lifecycle -------------------------------------------------------

fn build_router(ws_paths: &[String]) -> Router {
    let mut router = Router::new();
    for path in ws_paths {
        router = router.route(path, get(ws_handler));
    }
    router.fallback(dispatch)
}

fn start_server(host: &str, port: i64) -> Result<(), String> {
    let mut state = lock_state();
    if state.server.is_some() {
        return Err("server already running".to_string());
    }
    if port <= 0 || port > 65535 {
        return Err("invalid port".to_string());
    }
    let host = if host.is_empty() { "127.0.0.1" } else { host };

    // Bind synchronously so a port clash is reported by the caller, not lost in
    // the server thread.
    let listener =
        std::net::TcpListener::bind((host, port as u16)).map_err(|error| error.to_string())?;
    let _ = listener.set_nonblocking(true);

    let router = build_router(&state.ws_paths);
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
            let _ = axum::serve(
                listener,
                router.into_make_service_with_connect_info::<SocketAddr>(),
            )
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
    Ok(())
}

// --- route registration ops -------------------------------------------------

export_string!(server_route_get, args, {
    text_route(
        METHOD_GET,
        args.string(0),
        args.string(1),
        "text/html; charset=utf-8",
        200,
    )
});

export_string!(server_route_post, args, {
    text_route(
        METHOD_POST,
        args.string(0),
        args.string(1),
        "text/html; charset=utf-8",
        200,
    )
});

export_string!(server_put, args, {
    text_route(
        METHOD_PUT,
        args.string(0),
        args.string(1),
        "text/html; charset=utf-8",
        200,
    )
});

export_string!(server_delete, args, {
    text_route(
        METHOD_DELETE,
        args.string(0),
        args.string(1),
        "text/html; charset=utf-8",
        200,
    )
});

export_string!(server_patch, args, {
    text_route(
        METHOD_PATCH,
        args.string(0),
        args.string(1),
        "text/html; charset=utf-8",
        200,
    )
});

export_string!(server_any_http, args, {
    text_route(
        METHOD_ALL,
        args.string(0),
        args.string(1),
        "text/html; charset=utf-8",
        200,
    )
});

export_string!(server_get_status, args, {
    // `path` and `body` are strings, so `status` is the first number.
    let status = args.int(0).clamp(100, 599) as u16;
    text_route(
        METHOD_GET,
        args.string(0),
        args.string(1),
        "text/html; charset=utf-8",
        status,
    )
});

export_string!(server_json_get, args, {
    text_route(
        METHOD_GET,
        args.string(0),
        args.string(1),
        "application/json",
        200,
    )
});

export_string!(server_json_post, args, {
    text_route(
        METHOD_POST,
        args.string(0),
        args.string(1),
        "application/json",
        200,
    )
});

export_string!(server_json_route, args, {
    text_route(
        METHOD_GET | METHOD_POST,
        args.string(0),
        args.string(1),
        "application/json",
        200,
    )
});

export_string!(server_json_status, args, {
    let status = args.int(0).clamp(100, 599) as u16;
    text_route(
        METHOD_GET,
        args.string(0),
        args.string(1),
        "application/json",
        status,
    )
});

export_string!(server_route_echo, args, {
    register(METHOD_POST, args.string(0), RouteBody::Echo)
});

export_string!(server_redirect, args, {
    register(
        METHOD_GET,
        args.string(0),
        RouteBody::Redirect {
            target: args.string(1).to_string(),
            status: 302,
        },
    )
});

export_string!(server_redirect301, args, {
    register(
        METHOD_GET,
        args.string(0),
        RouteBody::Redirect {
            target: args.string(1).to_string(),
            status: 301,
        },
    )
});

export_string!(server_template, args, {
    register(
        METHOD_GET,
        args.string(0),
        RouteBody::Template {
            file: Some(args.string(1).to_string()),
            inline: None,
            data: args.string(2).to_string(),
        },
    )
});

export_string!(server_template_post, args, {
    register(
        METHOD_POST,
        args.string(0),
        RouteBody::Template {
            file: Some(args.string(1).to_string()),
            inline: None,
            data: args.string(2).to_string(),
        },
    )
});

export_string!(server_template_string, args, {
    register(
        METHOD_GET,
        args.string(0),
        RouteBody::Template {
            file: None,
            inline: Some(args.string(1).to_string()),
            data: args.string(2).to_string(),
        },
    )
});

export_string!(server_serve_file, args, {
    register(
        METHOD_GET,
        args.string(0),
        RouteBody::File {
            path: args.string(1).to_string(),
        },
    )
});

export_string!(server_ws_echo, args, {
    let mut state = lock_state();
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

export_string!(server_static_files, args, {
    let mut state = lock_state();
    if state.server.is_some() {
        return error_text("server already running");
    }
    let prefix = args.string(0).to_string();
    if !valid_path(&prefix) {
        return error_text("path must start with '/'");
    }
    state.config.static_trees.push(StaticTree {
        prefix,
        directory: args.string(1).to_string(),
    });
    "ok".to_string()
});

export_string!(server_static_site, args, {
    let mut state = lock_state();
    if state.server.is_some() {
        return error_text("server already running");
    }
    state.config.static_site = Some(args.string(0).to_string());
    "ok".to_string()
});

export_string!(server_clear_routes, args, {
    let _ = args;
    let mut state = lock_state();
    if state.server.is_some() {
        return error_text("server already running");
    }
    state.routes.clear();
    state.ws_paths.clear();
    state.config.static_trees.clear();
    state.config.static_site = None;
    "ok".to_string()
});

// --- configuration ops -----------------------------------------------------

export_string!(server_cors, args, {
    let _ = args;
    lock_state().config.cors = Some("*".to_string());
    "ok".to_string()
});

export_string!(server_cors_origin, args, {
    let origin = args.string(0).to_string();
    if origin.is_empty() {
        return error_text("origin must not be empty");
    }
    lock_state().config.cors = Some(origin);
    "ok".to_string()
});

export_string!(server_add_global_header, args, {
    let key = args.string(0).to_string();
    let value = args.string(1).to_string();
    if HeaderName::from_bytes(key.as_bytes()).is_err() || HeaderValue::from_str(&value).is_err() {
        return error_text("invalid header name or value");
    }
    lock_state().config.global_headers.push((key, value));
    "ok".to_string()
});

export_string!(server_enable_request_log, args, {
    let _ = args;
    lock_state().config.request_log = true;
    "ok".to_string()
});

export_string!(server_set_debug, args, {
    lock_state().config.debug = args.bool(0);
    "ok".to_string()
});

export_string!(server_set_template_folder, args, {
    lock_state().config.template_folder = args.string(0).to_string();
    "ok".to_string()
});

export_string!(server_not_found, args, {
    lock_state().config.not_found = Some(args.string(0).to_string());
    "ok".to_string()
});

export_string!(server_server_error, args, {
    lock_state().config.server_error = Some(args.string(0).to_string());
    "ok".to_string()
});

export_string!(server_forbidden, args, {
    lock_state().config.forbidden = Some(args.string(0).to_string());
    "ok".to_string()
});

export_string!(server_method_not_allowed, args, {
    lock_state().config.method_not_allowed = Some(args.string(0).to_string());
    "ok".to_string()
});

// --- request-context readers -----------------------------------------------

fn last_request<R>(reader: impl FnOnce(&LastRequest) -> R) -> Option<R> {
    lock_state().config.last_request.as_ref().map(reader)
}

export_string!(server_get_method, args, {
    let _ = args;
    last_request(|record| record.method.clone()).unwrap_or_default()
});

export_string!(server_get_path, args, {
    let _ = args;
    last_request(|record| record.path.clone()).unwrap_or_default()
});

export_string!(server_get_url, args, {
    let _ = args;
    last_request(|record| record.url.clone()).unwrap_or_default()
});

export_string!(server_get_body, args, {
    let _ = args;
    last_request(|record| record.body.clone()).unwrap_or_default()
});

export_string!(server_get_content_type, args, {
    let _ = args;
    last_request(|record| record.content_type.clone()).unwrap_or_default()
});

export_string!(server_get_remote_addr, args, {
    let _ = args;
    last_request(|record| record.remote_addr.clone()).unwrap_or_default()
});

export_string!(server_get_header, args, {
    let name = args.string(0).to_string();
    last_request(|record| header_lookup(&record.headers, &name)).unwrap_or_default()
});

export_string!(server_get_cookie, args, {
    let name = args.string(0).to_string();
    last_request(|record| pairs_lookup(&record.cookies, &name)).unwrap_or_default()
});

export_string!(server_get_arg, args, {
    let name = args.string(0).to_string();
    last_request(|record| pairs_lookup(&parse_pairs(&record.query), &name)).unwrap_or_default()
});

export_string!(server_get_form, args, {
    let name = args.string(0).to_string();
    last_request(|record| {
        if record
            .content_type
            .starts_with("application/x-www-form-urlencoded")
        {
            pairs_lookup(&parse_pairs(&record.body), &name)
        } else {
            String::new()
        }
    })
    .unwrap_or_default()
});

// --- lifecycle ops ---------------------------------------------------------

export_string!(server_init, args, {
    let host = args.string(0).to_string();
    // `port` is the first number, after the string host.
    let port = args.int(0);
    if port <= 0 || port > 65535 {
        return error_text("invalid port");
    }
    let mut state = lock_state();
    if state.server.is_some() {
        return error_text("server already running");
    }
    state.config.host = if host.is_empty() {
        "127.0.0.1".to_string()
    } else {
        host
    };
    state.config.port = port;
    "ok".to_string()
});

export_string!(server_start, args, {
    match start_server("127.0.0.1", args.int(0)) {
        Ok(()) => "ok".to_string(),
        Err(message) => error_text(&message),
    }
});

export_string!(server_run, args, {
    let _ = args;
    let (host, port) = {
        let state = lock_state();
        (state.config.host.clone(), state.config.port)
    };
    if port <= 0 {
        return error_text("call init(host, port) before run()");
    }
    if let Err(message) = start_server(&host, port) {
        return error_text(&message);
    }
    // Blocking, like the original: a server owns the process while it runs.
    let thread = {
        let mut state = lock_state();
        state
            .server
            .as_mut()
            .and_then(|server| server.thread.take())
    };
    if let Some(thread) = thread {
        let _ = thread.join();
    }
    "ok".to_string()
});

export_string!(server_stop, args, {
    let _ = args;
    let server = {
        let mut state = lock_state();
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

export_string!(server_run_https, args, {
    let (cert, key) = (args.string(0), args.string(1));
    error_text(&format!(
        "runHTTPS('{cert}', '{key}') is not available: this build has no TLS backend \
         (axum-server and tokio-rustls are not among the pinned dependencies)"
    ))
});

export_string!(server_run_ssl_adhoc, args, {
    let _ = args;
    error_text(
        "runSSLAdhoc() is not available: generating a self-signed certificate needs rcgen, \
         which is not among the pinned dependencies",
    )
});

export_int!(server_running, args, {
    let _ = args;
    lock_state().server.is_some() as i64
});

export_int!(server_port, args, {
    let _ = args;
    lock_state()
        .server
        .as_ref()
        .map(|server| server.port)
        .unwrap_or(0)
});

const OPS: &[(&str, &str, &str)] = &[
    ("routeGet", "server_route_get", "cdecl:cstring(...)"),
    ("routePost", "server_route_post", "cdecl:cstring(...)"),
    ("routeEcho", "server_route_echo", "cdecl:cstring(...)"),
    ("clearRoutes", "server_clear_routes", "cdecl:cstring(...)"),
    ("start", "server_start", "cdecl:cstring(...)"),
    ("stop", "server_stop", "cdecl:cstring(...)"),
    ("running", "server_running", "cdecl:int64(...)"),
    ("port", "server_port", "cdecl:int64(...)"),
    ("put", "server_put", "cdecl:cstring(...)"),
    ("delete", "server_delete", "cdecl:cstring(...)"),
    ("patch", "server_patch", "cdecl:cstring(...)"),
    ("anyHttp", "server_any_http", "cdecl:cstring(...)"),
    ("getStatus", "server_get_status", "cdecl:cstring(...)"),
    ("jsonGet", "server_json_get", "cdecl:cstring(...)"),
    ("jsonPost", "server_json_post", "cdecl:cstring(...)"),
    ("jsonRoute", "server_json_route", "cdecl:cstring(...)"),
    ("jsonStatus", "server_json_status", "cdecl:cstring(...)"),
    ("redirect", "server_redirect", "cdecl:cstring(...)"),
    ("redirect301", "server_redirect301", "cdecl:cstring(...)"),
    ("template", "server_template", "cdecl:cstring(...)"),
    ("templatePost", "server_template_post", "cdecl:cstring(...)"),
    (
        "templateString",
        "server_template_string",
        "cdecl:cstring(...)",
    ),
    ("serveFile", "server_serve_file", "cdecl:cstring(...)"),
    ("wsEcho", "server_ws_echo", "cdecl:cstring(...)"),
    ("staticFiles", "server_static_files", "cdecl:cstring(...)"),
    ("staticSite", "server_static_site", "cdecl:cstring(...)"),
    ("cors", "server_cors", "cdecl:cstring(...)"),
    ("corsOrigin", "server_cors_origin", "cdecl:cstring(...)"),
    (
        "addGlobalHeader",
        "server_add_global_header",
        "cdecl:cstring(...)",
    ),
    (
        "enableRequestLog",
        "server_enable_request_log",
        "cdecl:cstring(...)",
    ),
    ("setDebug", "server_set_debug", "cdecl:cstring(...)"),
    (
        "setTemplateFolder",
        "server_set_template_folder",
        "cdecl:cstring(...)",
    ),
    ("notFound", "server_not_found", "cdecl:cstring(...)"),
    ("serverError", "server_server_error", "cdecl:cstring(...)"),
    ("forbidden", "server_forbidden", "cdecl:cstring(...)"),
    (
        "methodNotAllowed",
        "server_method_not_allowed",
        "cdecl:cstring(...)",
    ),
    ("getMethod", "server_get_method", "cdecl:cstring(...)"),
    ("getPath", "server_get_path", "cdecl:cstring(...)"),
    ("getUrl", "server_get_url", "cdecl:cstring(...)"),
    ("getBody", "server_get_body", "cdecl:cstring(...)"),
    (
        "getContentType",
        "server_get_content_type",
        "cdecl:cstring(...)",
    ),
    (
        "getRemoteAddr",
        "server_get_remote_addr",
        "cdecl:cstring(...)",
    ),
    ("getHeader", "server_get_header", "cdecl:cstring(...)"),
    ("getCookie", "server_get_cookie", "cdecl:cstring(...)"),
    ("getArg", "server_get_arg", "cdecl:cstring(...)"),
    ("getForm", "server_get_form", "cdecl:cstring(...)"),
    ("init", "server_init", "cdecl:cstring(...)"),
    ("run", "server_run", "cdecl:cstring(...)"),
    ("runHTTPS", "server_run_https", "cdecl:cstring(...)"),
    ("runSSLAdhoc", "server_run_ssl_adhoc", "cdecl:cstring(...)"),
];

lynxer_module!(OPS);
