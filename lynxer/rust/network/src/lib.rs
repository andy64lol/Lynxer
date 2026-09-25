//! Lynxer `network` stdlib backend: HTTP + WebSocket client.
//!
//! Replaces the previous cpp-httplib backend (and its OpenSSL dependency) with
//! `ureq` (rustls) for HTTP and `tungstenite` for WebSockets. The Lynxer-facing
//! contract is unchanged, including the `"ERROR: ..."` sentinels and the
//! `"receive timeout"` / `"receive failed"` WebSocket messages.

use std::cell::RefCell;
use std::collections::HashMap;
use std::io::{ErrorKind, Read, Write};
use std::net::{TcpStream, ToSocketAddrs, UdpSocket};
use std::time::Duration;

use lynxer_abi::{export_int, export_string, lynxer_module};
use tungstenite::stream::MaybeTlsStream;
use tungstenite::Message;

// --- URL parsing (mirrors the previous cpp-httplib-backed parser) -----------

struct ParsedUrl {
    scheme: String,
    host: String,
    port: i64,
    path: String,
    query: String,
    fragment: String,
    ok: bool,
    error: String,
}

/// `atoi`-style prefix parse, which is what the previous parser used for ports.
fn atoi_prefix(text: &str) -> i64 {
    let trimmed = text.trim_start();
    let bytes = trimmed.as_bytes();
    let mut end = 0;
    if end < bytes.len() && (bytes[end] == b'+' || bytes[end] == b'-') {
        end += 1;
    }
    let digits = end;
    while end < bytes.len() && bytes[end].is_ascii_digit() {
        end += 1;
    }
    if end == digits {
        return 0;
    }
    trimmed[..end].parse::<i64>().unwrap_or(0)
}

fn parse_url(url: &str) -> ParsedUrl {
    let mut parsed = ParsedUrl {
        scheme: String::new(),
        host: String::new(),
        port: -1,
        path: "/".to_string(),
        query: String::new(),
        fragment: String::new(),
        ok: false,
        error: String::new(),
    };

    let scheme_end = match url.find("://") {
        Some(index) => index,
        None => {
            parsed.error = "missing URL scheme".to_string();
            return parsed;
        }
    };
    parsed.scheme = url[..scheme_end].to_string();
    let pos = scheme_end + 3;
    if pos >= url.len() {
        parsed.error = "missing host".to_string();
        return parsed;
    }

    let path_pos = url[pos..].find(['/', '?', '#']).map(|offset| pos + offset);
    let hostport = match path_pos {
        Some(end) => &url[pos..end],
        None => &url[pos..],
    };
    if hostport.is_empty() {
        parsed.error = "missing host".to_string();
        return parsed;
    }

    if hostport.starts_with('[') {
        let close = match hostport.find(']') {
            Some(index) => index,
            None => {
                parsed.error = "invalid IPv6 host".to_string();
                return parsed;
            }
        };
        parsed.host = hostport[1..close].to_string();
        if close + 1 < hostport.len() && hostport.as_bytes()[close + 1] == b':' {
            parsed.port = atoi_prefix(&hostport[close + 2..]);
        }
    } else {
        match hostport.rfind(':') {
            Some(colon) if hostport.find(':') == Some(colon) => {
                parsed.host = hostport[..colon].to_string();
                parsed.port = atoi_prefix(&hostport[colon + 1..]);
            }
            _ => parsed.host = hostport.to_string(),
        }
    }

    match path_pos {
        None => parsed.path = "/".to_string(),
        Some(start) => {
            let mut end = url.len();
            if let Some(hash) = url[start..].find('#').map(|offset| start + offset) {
                parsed.fragment = url[hash + 1..].to_string();
                end = hash;
            }
            if let Some(query) = url[start..].find('?').map(|offset| start + offset) {
                if query < end {
                    parsed.query = url[query + 1..end].to_string();
                    end = query;
                }
            }
            parsed.path = url[start..end].to_string();
            if parsed.path.is_empty() {
                parsed.path = "/".to_string();
            }
        }
    }

    if parsed.port < 0 {
        parsed.port = match parsed.scheme.as_str() {
            "https" | "wss" => 443,
            _ => 80,
        };
    }
    parsed.ok = !parsed.host.is_empty();
    if !parsed.ok {
        parsed.error = "missing host".to_string();
    }
    parsed
}

// --- HTTP -------------------------------------------------------------------

fn error_text(message: &str) -> String {
    format!("ERROR: {message}")
}

fn agent() -> &'static ureq::Agent {
    static AGENT: std::sync::OnceLock<ureq::Agent> = std::sync::OnceLock::new();
    AGENT.get_or_init(|| {
        let config = ureq::Agent::config_builder()
            .timeout_global(Some(Duration::from_secs(30)))
            .timeout_connect(Some(Duration::from_secs(30)))
            // Surface 4xx/5xx as a response so the caller can format it the same
            // way the previous backend did.
            .http_status_as_error(false)
            .build();
        ureq::Agent::new_with_config(config)
    })
}

fn http_error(status: ureq::http::StatusCode) -> String {
    match status.canonical_reason() {
        Some(reason) => error_text(&format!("HTTP {} {}", status.as_u16(), reason)),
        None => error_text(&format!("HTTP {}", status.as_u16())),
    }
}

fn read_body(response: &mut ureq::http::Response<ureq::Body>) -> String {
    match response.body_mut().read_to_vec() {
        Ok(bytes) => String::from_utf8_lossy(&bytes).into_owned(),
        Err(_) => String::new(),
    }
}

fn response_or_error(result: Result<ureq::http::Response<ureq::Body>, ureq::Error>) -> String {
    match result {
        Ok(mut response) => {
            let status = response.status();
            let body = read_body(&mut response);
            if status.as_u16() >= 400 {
                http_error(status)
            } else {
                body
            }
        }
        Err(error) => error_text(&error.to_string()),
    }
}

fn request_get(url: &str) -> Result<ureq::http::Response<ureq::Body>, ureq::Error> {
    agent().get(url).call()
}

fn request_with_body(
    method: &str,
    url: &str,
    body: &str,
    content_type: &str,
    accept_json: bool,
) -> Result<ureq::http::Response<ureq::Body>, ureq::Error> {
    let mut request = match method {
        "POST" => agent().post(url),
        "PUT" => agent().put(url),
        _ => agent().patch(url),
    };
    request = request.header("Content-Type", content_type);
    if accept_json {
        request = request.header("Accept", "application/json");
    }
    request.send(body)
}

/// The `http` crate lower-cases header names; the previous cpp-httplib backend
/// echoed the server's casing (e.g. `Content-Type`). Restore the usual
/// `Capitalized-Header-Name` form so callers matching on headers keep working.
fn header_name(name: &str) -> String {
    name.split('-')
        .map(|part| {
            let mut characters = part.chars();
            match characters.next() {
                Some(first) => first.to_uppercase().collect::<String>() + characters.as_str(),
                None => String::new(),
            }
        })
        .collect::<Vec<String>>()
        .join("-")
}

fn urlencode(text: &str) -> String {
    let mut output = String::with_capacity(text.len());
    for byte in text.bytes() {
        let character = byte as char;
        if character.is_ascii_alphanumeric() || matches!(character, '-' | '_' | '.' | '~') {
            output.push(character);
        } else if character == ' ' {
            output.push('+');
        } else {
            output.push('%');
            output.push_str(&format!("{byte:02X}"));
        }
    }
    output
}

fn resolve_host(hostname: &str) -> String {
    match (hostname, 0u16).to_socket_addrs() {
        Ok(addresses) => {
            for address in addresses {
                if let std::net::SocketAddr::V4(v4) = address {
                    return v4.ip().to_string();
                }
            }
            error_text("resolve failed")
        }
        Err(_) => error_text("resolve failed"),
    }
}

fn hostname() -> String {
    let mut buffer = [0 as libc::c_char; 256];
    if unsafe { libc::gethostname(buffer.as_mut_ptr(), buffer.len()) } != 0 {
        return String::new();
    }
    let bytes: Vec<u8> = buffer
        .iter()
        .take_while(|character| **character != 0)
        .map(|character| *character as u8)
        .collect();
    String::from_utf8_lossy(&bytes).into_owned()
}

// --- WebSocket --------------------------------------------------------------

struct Connection {
    socket: tungstenite::WebSocket<MaybeTlsStream<std::net::TcpStream>>,
    connected: bool,
}

thread_local! {
    // Named connections. Every Lynxer call happens on the interpreter thread,
    // so a thread-local registry avoids any cross-thread requirements.
    static CONNECTIONS: RefCell<HashMap<String, Connection>> =
        RefCell::new(HashMap::new());
}

fn ws_send(key: &str, message: &str) -> String {
    CONNECTIONS.with(|connections| {
        let mut connections = connections.borrow_mut();
        match connections.get_mut(key) {
            Some(connection) if connection.connected => {
                match connection
                    .socket
                    .send(Message::Text(message.to_string().into()))
                {
                    Ok(()) => "ok".to_string(),
                    Err(_) => error_text("send failed"),
                }
            }
            _ => error_text(&format!("no connection named '{key}'")),
        }
    })
}

fn ws_receive(key: &str) -> String {
    CONNECTIONS.with(|connections| {
        let mut connections = connections.borrow_mut();
        let connection = match connections.get_mut(key) {
            Some(connection) if connection.connected => connection,
            _ => return error_text(&format!("no connection named '{key}'")),
        };
        loop {
            match connection.socket.read() {
                Ok(Message::Text(text)) => return text.as_str().to_string(),
                Ok(Message::Binary(bytes)) => return String::from_utf8_lossy(&bytes).into_owned(),
                Ok(Message::Ping(_)) | Ok(Message::Pong(_)) | Ok(Message::Frame(_)) => continue,
                Ok(Message::Close(_)) => {
                    connection.connected = false;
                    return error_text("receive failed");
                }
                Err(tungstenite::Error::Io(error))
                    if matches!(error.kind(), ErrorKind::WouldBlock | ErrorKind::TimedOut) =>
                {
                    return error_text("receive timeout");
                }
                Err(_) => {
                    connection.connected = false;
                    return error_text("receive failed");
                }
            }
        }
    })
}

fn ws_close(key: &str) -> String {
    CONNECTIONS.with(|connections| {
        let mut connections = connections.borrow_mut();
        match connections.remove(key) {
            Some(mut connection) => {
                let _ = connection.socket.close(None);
                "ok".to_string()
            }
            None => error_text(&format!("no connection named '{key}'")),
        }
    })
}

// --- ops --------------------------------------------------------------------

export_string!(network_get, args, {
    response_or_error(request_get(args.string(0)))
});

export_int!(network_get_status, args, {
    match request_get(args.string(0)) {
        Ok(response) => response.status().as_u16() as i64,
        Err(_) => -1,
    }
});

export_string!(network_get_headers, args, {
    match request_get(args.string(0)) {
        Ok(response) => {
            let status = response.status();
            if status.as_u16() >= 400 {
                return error_text(&format!("HTTP {}", status.as_u16()));
            }
            let mut headers = String::new();
            for (name, value) in response.headers().iter() {
                if !headers.is_empty() {
                    headers.push('\n');
                }
                headers.push_str(&header_name(name.as_str()));
                headers.push_str(": ");
                headers.push_str(value.to_str().unwrap_or(""));
            }
            headers
        }
        Err(error) => error_text(&error.to_string()),
    }
});

export_string!(network_post, args, {
    response_or_error(request_with_body(
        "POST",
        args.string(0),
        args.string(1),
        args.string(2),
        false,
    ))
});

export_string!(network_put, args, {
    response_or_error(request_with_body(
        "PUT",
        args.string(0),
        args.string(1),
        args.string(2),
        false,
    ))
});

export_string!(network_delete, args, {
    response_or_error(agent().delete(args.string(0)).call())
});

export_string!(network_patch, args, {
    response_or_error(request_with_body(
        "PATCH",
        args.string(0),
        args.string(1),
        args.string(2),
        false,
    ))
});

export_string!(network_get_json, args, {
    response_or_error(
        agent()
            .get(args.string(0))
            .header("Accept", "application/json")
            .call(),
    )
});

export_string!(network_post_json, args, {
    response_or_error(request_with_body(
        "POST",
        args.string(0),
        args.string(1),
        "application/json",
        true,
    ))
});

export_string!(network_download, args, {
    let url = args.string(0);
    let filepath = args.string(1);
    match request_get(url) {
        Ok(mut response) => {
            let status = response.status();
            if status.as_u16() >= 400 {
                return http_error(status);
            }
            let bytes = response.body_mut().read_to_vec().unwrap_or_default();
            match std::fs::write(filepath, &bytes) {
                Ok(()) => "ok".to_string(),
                Err(_) => error_text("cannot write file"),
            }
        }
        Err(error) => error_text(&error.to_string()),
    }
});

export_string!(network_urlencode, args, { urlencode(args.string(0)) });

export_int!(network_http_head, args, {
    match agent().head(args.string(0)).call() {
        Ok(response) => response.status().as_u16() as i64,
        Err(_) => -1,
    }
});

export_string!(network_url_scheme, args, {
    parse_url(args.string(0)).scheme
});

export_string!(network_url_host, args, {
    let parsed = parse_url(args.string(0));
    if !parsed.ok {
        return String::new();
    }
    let default_port = matches!(
        (parsed.scheme.as_str(), parsed.port),
        ("http", 80) | ("https", 443) | ("ws", 80) | ("wss", 443)
    );
    if default_port {
        parsed.host
    } else {
        format!("{}:{}", parsed.host, parsed.port)
    }
});

export_string!(network_url_path, args, { parse_url(args.string(0)).path });

export_string!(network_url_parse, args, {
    let parsed = parse_url(args.string(0));
    format!(
        "{{\"scheme\":\"{}\",\"host\":\"{}\",\"port\":{},\"path\":\"{}\",\"query\":\"{}\",\"fragment\":\"{}\"}}",
        parsed.scheme, parsed.host, parsed.port, parsed.path, parsed.query, parsed.fragment
    )
});

export_string!(network_get_hostname, args, {
    let _ = args;
    hostname()
});

export_string!(network_resolve_host, args, { resolve_host(args.string(0)) });

export_string!(network_ws_connect, args, {
    let key = args.string(0).to_string();
    if key.is_empty() {
        return error_text("empty connection name");
    }
    match tungstenite::connect(args.string(1)) {
        Ok((socket, _response)) => {
            if let MaybeTlsStream::Plain(stream) = socket.get_ref() {
                let _ = stream.set_read_timeout(Some(Duration::from_secs(30)));
            }
            CONNECTIONS.with(|connections| {
                connections.borrow_mut().insert(
                    key,
                    Connection {
                        socket,
                        connected: true,
                    },
                );
            });
            "ok".to_string()
        }
        Err(error) => error_text(&error.to_string()),
    }
});

export_string!(network_ws_send, args, {
    ws_send(args.string(0), args.string(1))
});

export_string!(network_ws_receive, args, { ws_receive(args.string(0)) });

export_string!(network_ws_send_receive, args, {
    let key = args.string(0);
    let sent = ws_send(key, args.string(1));
    if sent.starts_with("ERROR:") {
        sent
    } else {
        ws_receive(key)
    }
});

export_string!(network_ws_close, args, { ws_close(args.string(0)) });

export_int!(network_ws_connected, args, {
    CONNECTIONS.with(|connections| {
        connections
            .borrow()
            .get(args.string(0))
            .map(|connection| connection.connected as i64)
            .unwrap_or(0)
    })
});

// --- Raw TCP client ---------------------------------------------------------
//
// Named connections, like the WebSocket registry. `tcp*` is deliberately
// plaintext: TLS belongs to the HTTP/WebSocket client above.

thread_local! {
    static TCP_CONNECTIONS: RefCell<HashMap<String, TcpStream>> =
        RefCell::new(HashMap::new());
}

const TCP_TIMEOUT: Duration = Duration::from_secs(30);

fn tcp_resolve(host: &str, port: i64) -> Result<std::net::SocketAddr, String> {
    let Ok(port) = u16::try_from(port) else {
        return Err("invalid port".to_string());
    };
    match (host, port).to_socket_addrs() {
        Ok(mut addresses) => addresses.next().ok_or_else(|| "resolve failed".to_string()),
        Err(_) => Err("resolve failed".to_string()),
    }
}

fn tcp_connect(name: &str, host: &str, port: i64) -> String {
    if name.is_empty() {
        return error_text("empty connection name");
    }
    let address = match tcp_resolve(host, port) {
        Ok(address) => address,
        Err(message) => return error_text(&message),
    };
    match TcpStream::connect_timeout(&address, TCP_TIMEOUT) {
        Ok(stream) => {
            // Bound every later receive, so a quiet peer cannot wedge the
            // interpreter thread.
            let _ = stream.set_read_timeout(Some(TCP_TIMEOUT));
            TCP_CONNECTIONS.with(|connections| {
                connections.borrow_mut().insert(name.to_string(), stream);
            });
            "ok".to_string()
        }
        Err(error) => error_text(&error.to_string()),
    }
}

fn tcp_send_to(stream: &mut TcpStream, data: &str) -> String {
    match stream.write_all(data.as_bytes()).and_then(|()| stream.flush()) {
        Ok(()) => "ok".to_string(),
        Err(_) => error_text("send failed"),
    }
}

fn tcp_receive_from(stream: &mut TcpStream, buffer_size: i64) -> String {
    if buffer_size <= 0 {
        return error_text("receive size must be positive");
    }
    let mut buffer = vec![0u8; buffer_size as usize];
    match stream.read(&mut buffer) {
        Ok(0) => String::new(),
        Ok(count) => String::from_utf8_lossy(&buffer[..count]).into_owned(),
        Err(error) if matches!(error.kind(), ErrorKind::WouldBlock | ErrorKind::TimedOut) => {
            error_text("receive timeout")
        }
        Err(_) => error_text("receive failed"),
    }
}

fn tcp_send(key: &str, data: &str) -> String {
    TCP_CONNECTIONS.with(|connections| {
        let mut connections = connections.borrow_mut();
        match connections.get_mut(key) {
            Some(stream) => tcp_send_to(stream, data),
            None => error_text(&format!("no connection named '{key}'")),
        }
    })
}

fn tcp_receive(key: &str, buffer_size: i64) -> String {
    TCP_CONNECTIONS.with(|connections| {
        let mut connections = connections.borrow_mut();
        match connections.get_mut(key) {
            Some(stream) => tcp_receive_from(stream, buffer_size),
            None => error_text(&format!("no connection named '{key}'")),
        }
    })
}

fn tcp_send_receive(key: &str, data: &str, buffer_size: i64) -> String {
    let sent = tcp_send(key, data);
    if sent.starts_with("ERROR:") {
        sent
    } else {
        tcp_receive(key, buffer_size)
    }
}

fn tcp_close(key: &str) -> String {
    let removed = TCP_CONNECTIONS.with(|connections| connections.borrow_mut().remove(key));
    match removed {
        Some(stream) => {
            let _ = stream.shutdown(std::net::Shutdown::Both);
            "ok".to_string()
        }
        None => error_text(&format!("no connection named '{key}'")),
    }
}

/// The primary local IPv4 address. A connected UDP socket reports the interface
/// the kernel would route through, without sending anything; the hostname's
/// first non-loopback address is the fallback.
fn local_ip() -> String {
    if let Ok(socket) = UdpSocket::bind("0.0.0.0:0") {
        if socket.connect("8.8.8.8:80").is_ok() {
            if let Ok(std::net::SocketAddr::V4(address)) = socket.local_addr() {
                let ip = address.ip().to_string();
                if !ip.is_empty() && ip != "0.0.0.0" {
                    return ip;
                }
            }
        }
    }
    if let Ok(addresses) = (hostname().as_str(), 0u16).to_socket_addrs() {
        for address in addresses {
            if let std::net::SocketAddr::V4(v4) = address {
                if !v4.ip().is_loopback() {
                    return v4.ip().to_string();
                }
            }
        }
    }
    "127.0.0.1".to_string()
}

fn is_port_open(host: &str, port: i64, timeout_secs: i64) -> bool {
    let address = match tcp_resolve(host, port) {
        Ok(address) => address,
        Err(_) => return false,
    };
    // A zero timeout is an immediate error in `connect_timeout`, which is never
    // what a caller means here.
    let timeout = Duration::from_secs(timeout_secs.max(1) as u64);
    TcpStream::connect_timeout(&address, timeout).is_ok()
}

export_string!(network_tcp_connect, args, {
    tcp_connect(args.string(0), args.string(1), args.int(0))
});

export_string!(network_tcp_send, args, {
    tcp_send(args.string(0), args.string(1))
});

export_string!(network_tcp_receive, args, {
    tcp_receive(args.string(0), args.int(0))
});

export_string!(network_tcp_send_receive, args, {
    tcp_send_receive(args.string(0), args.string(1), args.int(0))
});

export_string!(network_tcp_close, args, { tcp_close(args.string(0)) });

export_int!(network_is_port_open, args, {
    is_port_open(args.string(0), args.int(0), args.int(1)) as i64
});

export_int!(network_ping, args, {
    // The original's reachability probe: TCP port 80 within three seconds.
    is_port_open(args.string(0), 80, 3) as i64
});

export_string!(network_local_ip, args, {
    let _ = args;
    local_ip()
});

const OPS: &[(&str, &str, &str)] = &[
    ("get", "network_get", "cdecl:cstring(...)"),
    ("getStatus", "network_get_status", "cdecl:int64(...)"),
    ("getHeaders", "network_get_headers", "cdecl:cstring(...)"),
    ("post", "network_post", "cdecl:cstring(...)"),
    ("put", "network_put", "cdecl:cstring(...)"),
    ("delete", "network_delete", "cdecl:cstring(...)"),
    ("patch", "network_patch", "cdecl:cstring(...)"),
    ("getJson", "network_get_json", "cdecl:cstring(...)"),
    ("postJson", "network_post_json", "cdecl:cstring(...)"),
    ("download", "network_download", "cdecl:cstring(...)"),
    ("urlencode", "network_urlencode", "cdecl:cstring(...)"),
    ("httpHead", "network_http_head", "cdecl:int64(...)"),
    ("urlScheme", "network_url_scheme", "cdecl:cstring(...)"),
    ("urlHost", "network_url_host", "cdecl:cstring(...)"),
    ("urlPath", "network_url_path", "cdecl:cstring(...)"),
    ("urlParse", "network_url_parse", "cdecl:cstring(...)"),
    ("getHostname", "network_get_hostname", "cdecl:cstring(...)"),
    ("resolveHost", "network_resolve_host", "cdecl:cstring(...)"),
    ("wsConnect", "network_ws_connect", "cdecl:cstring(...)"),
    ("wsSend", "network_ws_send", "cdecl:cstring(...)"),
    ("wsReceive", "network_ws_receive", "cdecl:cstring(...)"),
    (
        "wsSendReceive",
        "network_ws_send_receive",
        "cdecl:cstring(...)",
    ),
    ("wsClose", "network_ws_close", "cdecl:cstring(...)"),
    ("wsConnected", "network_ws_connected", "cdecl:int64(...)"),
    ("tcpConnect", "network_tcp_connect", "cdecl:cstring(...)"),
    ("tcpSend", "network_tcp_send", "cdecl:cstring(...)"),
    ("tcpReceive", "network_tcp_receive", "cdecl:cstring(...)"),
    ("tcpSendReceive", "network_tcp_send_receive", "cdecl:cstring(...)"),
    ("tcpClose", "network_tcp_close", "cdecl:cstring(...)"),
    ("isPortOpen", "network_is_port_open", "cdecl:int64(...)"),
    ("ping", "network_ping", "cdecl:int64(...)"),
    ("getLocalIP", "network_local_ip", "cdecl:cstring(...)"),
];

lynxer_module!(OPS);
