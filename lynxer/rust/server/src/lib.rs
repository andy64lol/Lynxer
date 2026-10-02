//! Lynxer `server` stdlib backend: HTTP/WebSocket server on axum + tokio.
//!
//! Replaces the previous Crow/Boost.Asio backend, then the Flask-backed
//! original. Routes are registered while the server is stopped and served from
//! a single dispatcher, so every route kind — fixed text, JSON, templates,
//! files, redirects, static trees — shares one code path for CORS, global
//! headers, request logging and the custom error bodies.
//!
//! Request-context readers (`getArg`, `getHeader`, …) are bound to the active
//! request inside named callback routes and retain the last-request behavior
//! outside callbacks. `run()` starts the listener and then blocks, matching the
//! original.
//!
//! `runHTTPS` and `runSSLAdhoc` start a **TLS** listener through `rustls` (the
//! `ring` provider already in this workspace — no OpenSSL, no CMake) and, like
//! `start()`, return as soon as it is listening; `stop()` shuts it down.

use core::ffi::c_void;
use std::cell::RefCell;
use std::net::SocketAddr;
use std::sync::{Mutex, MutexGuard};
use std::time::Duration;

use axum_server::tls_rustls::RustlsConfig;
use axum_server::Handle;

use axum::body::Body;
use axum::extract::ws::{Message, WebSocket, WebSocketUpgrade};
use axum::extract::{ConnectInfo, Request};
use axum::http::header::CONTENT_TYPE;
use axum::http::{HeaderName, HeaderValue, Method, StatusCode};
use axum::response::Response;
use axum::routing::get;
use axum::Router;

use lynxer_abi::{export_int, export_string, lynxer_module};
use lynxer_abi::{invoke_threadsafe, LynxerHostApiV2, HOST_API_VERSION_V2};

mod template;

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

#[derive(Clone)]
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
    Callback {
        handler: String,
    },
}

#[derive(Clone)]
struct Route {
    methods: u8,
    path: String,
    body: RouteBody,
}

struct StaticTree {
    prefix: String,
    directory: String,
}

/// Last-request fallback for reader calls made outside a callback.
#[derive(Clone)]
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

static HOST: Mutex<Option<LynxerHostApiV2>> = Mutex::new(None);

thread_local! {
    static CURRENT_REQUEST: RefCell<Option<LastRequest>> = const { RefCell::new(None) };
    static CALLBACK_RESPONSE: RefCell<Option<CallbackResponse>> = const { RefCell::new(None) };
}

struct CallbackResponse {
    status: u16,
    content_type: String,
    body: String,
}

struct ServerJoin {
    thread: Option<std::thread::JoinHandle<()>>,
}

unsafe extern "C" fn join_server(user: *mut c_void) {
    let join = &mut *(user as *mut ServerJoin);
    if let Some(thread) = join.thread.take() {
        let _ = thread.join();
    }
}

fn join_server_unlocked(thread: std::thread::JoinHandle<()>) {
    let host = *HOST.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
    let mut join = ServerJoin {
        thread: Some(thread),
    };
    if let Some(host) = host {
        if let Some(blocking) = host.blocking {
            let status = unsafe {
                blocking(
                    host.context,
                    join_server,
                    (&mut join as *mut ServerJoin).cast(),
                )
            };
            if join.thread.is_none() || status == 0 {
                return;
            }
        }
    }
    if let Some(thread) = join.thread.take() {
        let _ = thread.join();
    }
}

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

fn register_callback(methods: u8, path: &str, handler: &str) -> String {
    if handler.is_empty() {
        return error_text("callback name must not be empty");
    }
    register(
        methods,
        path,
        RouteBody::Callback {
            handler: handler.to_string(),
        },
    )
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

/// The per-request data a template can read. Routes are fixed strings, not
/// callbacks, so this is the way a request reaches a template: the query
/// arguments, headers, cookies and body are exposed under `request` (and the
/// query arguments also overlay the root, so `{{ name }}` resolves from
/// `?name=Ada`).
struct RequestContext {
    method: String,
    path: String,
    url: String,
    query: String,
    content_type: String,
    body: String,
    remote_addr: String,
    headers: Vec<(String, String)>,
    cookies: Vec<(String, String)>,
}

impl RequestContext {
    fn reader_record(&self) -> LastRequest {
        LastRequest {
            method: self.method.clone(),
            path: self.path.clone(),
            url: self.url.clone(),
            query: self.query.clone(),
            body: self.body.clone(),
            content_type: self.content_type.clone(),
            remote_addr: self.remote_addr.clone(),
            headers: self.headers.clone(),
            cookies: self.cookies.clone(),
        }
    }
}

/// Builds the template root: the static `data` object, with a `request` object
/// and the query arguments merged in.
fn template_context(data: &str, request: &RequestContext) -> serde_json::Value {
    let mut root = match serde_json::from_str::<serde_json::Value>(data) {
        Ok(serde_json::Value::Object(map)) => map,
        _ => serde_json::Map::new(),
    };

    let mut request_object = serde_json::Map::new();
    request_object.insert(
        "method".to_string(),
        serde_json::Value::String(request.method.clone()),
    );
    request_object.insert(
        "path".to_string(),
        serde_json::Value::String(request.path.clone()),
    );
    request_object.insert(
        "query".to_string(),
        serde_json::Value::String(request.query.clone()),
    );
    request_object.insert(
        "body".to_string(),
        serde_json::Value::String(request.body.clone()),
    );
    request_object.insert(
        "contentType".to_string(),
        serde_json::Value::String(request.content_type.clone()),
    );

    let mut args = serde_json::Map::new();
    for (key, value) in parse_pairs(&request.query) {
        args.insert(key, serde_json::Value::String(value));
    }
    request_object.insert("args".to_string(), serde_json::Value::Object(args.clone()));

    let mut headers = serde_json::Map::new();
    for (key, value) in &request.headers {
        headers.insert(key.clone(), serde_json::Value::String(value.clone()));
    }
    request_object.insert("headers".to_string(), serde_json::Value::Object(headers));

    let mut cookies = serde_json::Map::new();
    for (key, value) in &request.cookies {
        cookies.insert(key.clone(), serde_json::Value::String(value.clone()));
    }
    request_object.insert("cookies".to_string(), serde_json::Value::Object(cookies));

    // A JSON body is exposed as `request.json`.
    if request.content_type.contains("application/json") {
        if let Ok(value) = serde_json::from_str::<serde_json::Value>(&request.body) {
            request_object.insert("json".to_string(), value);
        }
    }
    // A form body is exposed as `request.form`.
    if request
        .content_type
        .contains("application/x-www-form-urlencoded")
    {
        let mut form = serde_json::Map::new();
        for (key, value) in parse_pairs(&request.body) {
            form.insert(key, serde_json::Value::String(value));
        }
        request_object.insert("form".to_string(), serde_json::Value::Object(form));
    }

    root.insert(
        "request".to_string(),
        serde_json::Value::Object(request_object),
    );
    // Query arguments overlay the root so `{{ name }}` works without a prefix.
    for (key, value) in args {
        root.entry(key).or_insert(value);
    }
    serde_json::Value::Object(root)
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

    let request_context = RequestContext {
        method: method.clone(),
        path: path.clone(),
        url: url.clone(),
        query: query.clone(),
        content_type: content_type.clone(),
        body: raw_body.clone(),
        remote_addr: address.ip().to_string(),
        headers: headers.clone(),
        cookies: cookies.clone(),
    };

    let (log, plan, config) = {
        let mut state = lock_state();
        let log = state.config.request_log || state.config.debug;
        if parts.method != Method::OPTIONS {
            state.config.last_request = Some(request_context.reader_record());
        }

        let flag = request_flag(&parts.method);
        let plan;
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
        Plan::Route(index) => route_response(index, &request_context, &config),
    }
}

fn route_response(index: usize, request: &RequestContext, config: &Config) -> Response {
    let (body, template_folder) = {
        let state = lock_state();
        let Some(route) = state.routes.get(index) else {
            return response(
                404,
                "text/plain; charset=utf-8",
                error_body(config, 404),
                config,
            );
        };
        (route.body.clone(), state.config.template_folder.clone())
    };
    match body {
        RouteBody::Text {
            body,
            content_type,
            status,
        } => response(status, content_type, body, config),
        RouteBody::Template { file, inline, data } => {
            let loader = |path: &str| {
                let full = if template_folder.is_empty() {
                    std::path::PathBuf::from(path)
                } else {
                    safe_join(&template_folder, path)
                        .ok_or_else(|| "template path escapes its configured folder".to_string())?
                };
                std::fs::read_to_string(&full)
                    .map_err(|error| format!("cannot read template '{}': {error}", full.display()))
            };
            let source = match (file, inline) {
                (Some(path), _) => match loader(&path) {
                    Ok(source) => source,
                    Err(error) => {
                        return response(
                            500,
                            "text/plain; charset=utf-8",
                            format!("template error: {error}"),
                            config,
                        )
                    }
                },
                (_, Some(text)) => text,
                _ => String::new(),
            };
            let values = template_context(&data, request);
            match template::render_with(&source, &values, loader) {
                Ok(rendered) => response(200, "text/html; charset=utf-8", rendered, config),
                Err(message) => response(
                    500,
                    "text/plain; charset=utf-8",
                    format!("template error: {message}"),
                    config,
                ),
            }
        }
        RouteBody::File { path } => match std::fs::read(&path) {
            Ok(bytes) => response(
                200,
                content_type_for(&path),
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
                .status(StatusCode::from_u16(status).unwrap_or(StatusCode::FOUND))
                .header("location", target.as_str());
            decorate(builder, config)
                .body(Body::empty())
                .unwrap_or_else(|_| Response::new(Body::empty()))
        }
        RouteBody::Echo => response(200, "text/plain", request.body.clone(), config),
        RouteBody::Callback { handler } => run_route_callback(&handler, request, config),
    }
}

fn run_route_callback(handler: &str, request: &RequestContext, config: &Config) -> Response {
    let host = *HOST.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
    let Some(host) = host else {
        return response(
            500,
            "text/plain; charset=utf-8",
            "server callback host is unavailable".to_string(),
            config,
        );
    };
    let previous_request =
        CURRENT_REQUEST.with(|current| current.replace(Some(request.reader_record())));
    CALLBACK_RESPONSE.with(|current| current.replace(None));
    let status = invoke_threadsafe(&host, handler, None);
    let callback_response = CALLBACK_RESPONSE.with(|current| current.replace(None));
    CURRENT_REQUEST.with(|current| current.replace(previous_request));
    if status != 0 {
        return response(
            500,
            "text/plain; charset=utf-8",
            "route callback failed".to_string(),
            config,
        );
    }
    match callback_response {
        Some(result) => response(result.status, &result.content_type, result.body, config),
        None => response(204, "text/plain; charset=utf-8", String::new(), config),
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

/// rustls 0.23 needs a process-wide crypto provider. `ring` is the one the rest
/// of the workspace already builds (`ureq`, `tungstenite`), so installing it
/// keeps a single crypto stack and avoids `aws-lc-rs`'s CMake requirement. A
/// provider installed by an earlier module is left alone.
fn install_crypto_provider() {
    let _ = rustls::crypto::ring::default_provider().install_default();
}

/// Reads and validates a PEM certificate/key pair before the listener starts,
/// so a bad file is reported to the caller rather than lost in the server
/// thread. `rustls` parses the same bytes again when it builds the config.
fn validate_pem(cert_pem: &[u8], key_pem: &[u8]) -> Result<(), String> {
    let mut cert_bytes = cert_pem;
    let certificates: Vec<_> = rustls_pemfile::certs(&mut cert_bytes)
        .collect::<Result<Vec<_>, _>>()
        .map_err(|error| format!("cannot parse the certificate file: {error}"))?;
    if certificates.is_empty() {
        return Err("the certificate file contains no PEM certificate".to_string());
    }
    let key = rustls_pemfile::private_key(&mut std::io::Cursor::new(key_pem))
        .map_err(|error| format!("cannot parse the key file: {error}"))?;
    let Some(key) = key else {
        return Err("the key file contains no PEM private key".to_string());
    };
    install_crypto_provider();
    rustls::ServerConfig::builder()
        .with_no_client_auth()
        .with_single_cert(certificates, key)
        .map_err(|_| "the certificate and private key do not match".to_string())?;
    Ok(())
}

/// Generates a self-signed certificate for `localhost`/`127.0.0.1`, as PEM.
fn self_signed_pem() -> Result<(Vec<u8>, Vec<u8>), String> {
    let certified =
        rcgen::generate_simple_self_signed(vec!["localhost".to_string(), "127.0.0.1".to_string()])
            .map_err(|error| format!("cannot generate a self-signed certificate: {error}"))?;
    Ok((
        certified.cert.pem().into_bytes(),
        certified.signing_key.serialize_pem().into_bytes(),
    ))
}

/// Binds and serves `router` over TLS. Like [`start_server`], the socket is
/// bound here so a port clash is reported to the caller.
fn start_server_tls(
    host: &str,
    port: i64,
    cert_pem: Vec<u8>,
    key_pem: Vec<u8>,
) -> Result<(), String> {
    let mut state = lock_state();
    if state.server.is_some() {
        return Err("server already running".to_string());
    }
    if port <= 0 || port > 65535 {
        return Err("invalid port".to_string());
    }
    let host = if host.is_empty() { "127.0.0.1" } else { host };

    let listener =
        std::net::TcpListener::bind((host, port as u16)).map_err(|error| error.to_string())?;
    let _ = listener.set_nonblocking(true);

    let router = build_router(&state.ws_paths);
    let (shutdown, wait) = tokio::sync::oneshot::channel::<()>();

    let thread = std::thread::spawn(move || {
        install_crypto_provider();
        let runtime = match tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
        {
            Ok(runtime) => runtime,
            Err(_) => return,
        };
        runtime.block_on(async move {
            let config = match RustlsConfig::from_pem(cert_pem, key_pem).await {
                Ok(config) => config,
                Err(_) => return,
            };
            let handle = Handle::new();
            let shutdown_handle = handle.clone();
            tokio::spawn(async move {
                let _ = wait.await;
                shutdown_handle.graceful_shutdown(Some(Duration::from_secs(1)));
            });
            let server = match axum_server::from_tcp_rustls(listener, config) {
                Ok(server) => server,
                Err(_) => return,
            };
            let _ = server
                .handle(handle)
                .serve(router.into_make_service_with_connect_info::<SocketAddr>())
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

export_string!(server_route_get_callback, args, {
    register_callback(METHOD_GET, args.string(0), args.string(1))
});

export_string!(server_route_post_callback, args, {
    register_callback(METHOD_POST, args.string(0), args.string(1))
});

export_string!(server_route_callback, args, {
    register_callback(METHOD_ALL, args.string(0), args.string(1))
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
    let active = CURRENT_REQUEST.with(|current| current.borrow().clone());
    if let Some(active) = active {
        Some(reader(&active))
    } else {
        lock_state().config.last_request.as_ref().map(reader)
    }
}

export_string!(server_respond, args, {
    let status = args.int(0).clamp(100, 599) as u16;
    let content_type = args.string(0).to_string();
    let body = args.string(1).to_string();
    let active = CURRENT_REQUEST.with(|current| current.borrow().is_some());
    if !active {
        return error_text("respond may only be used inside a route callback");
    }
    CALLBACK_RESPONSE.with(|current| {
        current.replace(Some(CallbackResponse {
            status,
            content_type,
            body,
        }))
    });
    "ok".to_string()
});

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
        join_server_unlocked(thread);
    }
    "ok".to_string()
});

export_string!(server_stop, args, {
    let _ = args;
    let (shutdown, thread) = {
        let mut state = lock_state();
        let Some(server) = state.server.as_mut() else {
            return error_text("server is not running");
        };
        let Some(shutdown) = server.shutdown.take() else {
            return error_text("server is stopping");
        };
        (shutdown, server.thread.take())
    };
    let _ = shutdown.send(());
    if let Some(thread) = thread {
        join_server_unlocked(thread);
    }
    lock_state().server = None;
    "ok".to_string()
});

export_string!(server_run_https, args, {
    let (cert_path, key_path) = (args.string(0), args.string(1));
    let (port, host) = {
        let state = lock_state();
        (state.config.port, state.config.host.clone())
    };
    if port <= 0 {
        return error_text("call init(host, port) before runHTTPS()");
    }
    let cert_pem = match std::fs::read(cert_path) {
        Ok(pem) => pem,
        Err(error) => return error_text(&format!("cannot read '{cert_path}': {error}")),
    };
    let key_pem = match std::fs::read(key_path) {
        Ok(pem) => pem,
        Err(error) => return error_text(&format!("cannot read '{key_path}': {error}")),
    };
    if let Err(message) = validate_pem(&cert_pem, &key_pem) {
        return error_text(&message);
    }
    match start_server_tls(&host, port, cert_pem, key_pem) {
        Ok(()) => "ok".to_string(),
        Err(message) => error_text(&message),
    }
});

export_string!(server_run_ssl_adhoc, args, {
    let _ = args;
    let (port, host) = {
        let state = lock_state();
        (state.config.port, state.config.host.clone())
    };
    if port <= 0 {
        return error_text("call init(host, port) before runSSLAdhoc()");
    }
    let (cert_pem, key_pem) = match self_signed_pem() {
        Ok(pair) => pair,
        Err(message) => return error_text(&message),
    };
    match start_server_tls(&host, port, cert_pem, key_pem) {
        Ok(()) => "ok".to_string(),
        Err(message) => error_text(&message),
    }
});

#[no_mangle]
pub unsafe extern "C" fn lynxer_module_attach_v2(host: *const LynxerHostApiV2) -> i32 {
    let Some(host) = host.as_ref() else {
        return 1;
    };
    if host.version != HOST_API_VERSION_V2 || host.invoke_threadsafe.is_none() {
        return 1;
    }
    *HOST.lock().unwrap_or_else(|poisoned| poisoned.into_inner()) = Some(*host);
    0
}

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
    (
        "routeGetCallback",
        "server_route_get_callback",
        "cdecl:cstring(...)",
    ),
    (
        "routePostCallback",
        "server_route_post_callback",
        "cdecl:cstring(...)",
    ),
    (
        "routeCallback",
        "server_route_callback",
        "cdecl:cstring(...)",
    ),
    ("respond", "server_respond", "cdecl:cstring(...)"),
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

#[cfg(test)]
mod tests {
    use super::*;

    fn request_record(id: &str) -> LastRequest {
        LastRequest {
            method: "GET".to_string(),
            path: format!("/{id}"),
            url: format!("http://localhost/{id}"),
            query: format!("id={id}"),
            body: id.to_string(),
            content_type: "text/plain".to_string(),
            remote_addr: "127.0.0.1".to_string(),
            headers: vec![("x-request".to_string(), id.to_string())],
            cookies: Vec::new(),
        }
    }

    #[test]
    fn concurrent_request_contexts_are_thread_local() {
        let workers = ["alpha", "beta", "gamma", "delta"]
            .into_iter()
            .map(|id| {
                std::thread::spawn(move || {
                    CURRENT_REQUEST.with(|current| current.replace(Some(request_record(id))));
                    let expected_path = format!("/{id}");
                    for _ in 0..100 {
                        assert_eq!(
                            last_request(|record| record.path.clone()).as_deref(),
                            Some(expected_path.as_str())
                        );
                        assert_eq!(
                            last_request(|record| header_lookup(&record.headers, "X-Request"))
                                .as_deref(),
                            Some(id)
                        );
                        std::thread::yield_now();
                    }
                    CURRENT_REQUEST.with(|current| current.replace(None));
                })
            })
            .collect::<Vec<_>>();
        for worker in workers {
            worker.join().unwrap();
        }
    }

    #[test]
    fn tls_validation_rejects_a_mismatched_private_key() {
        let (certificate, _) = self_signed_pem().unwrap();
        let (_, other_key) = self_signed_pem().unwrap();
        assert_eq!(
            validate_pem(&certificate, &other_key).unwrap_err(),
            "the certificate and private key do not match"
        );
    }
}
