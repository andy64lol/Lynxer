// Lynxer `network` stdlib backend: HTTP + WebSocket client via cpp-httplib.
// Replaces the old Python `http` / `net` modules for Clynxer.

#include "httplib.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <netdb.h>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

namespace {

const char* stable(std::string value) {
    thread_local std::string result;
    result = std::move(value);
    return result.c_str();
}

std::string textOrEmpty(const char* text) {
    return text == nullptr ? std::string() : std::string(text);
}

std::string errorText(const std::string& message) {
    return "ERROR: " + message;
}

struct ParsedUrl {
    std::string scheme;
    std::string host;
    int port = -1;
    std::string path = "/";
    std::string query;
    std::string fragment;
    bool ok = false;
    std::string error;
};

ParsedUrl parseUrl(const std::string& url) {
    ParsedUrl parsed;
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        parsed.error = "missing URL scheme";
        return parsed;
    }
    parsed.scheme = url.substr(0, scheme_end);
    std::size_t pos = scheme_end + 3;
    if (pos >= url.size()) {
        parsed.error = "missing host";
        return parsed;
    }

    std::size_t path_pos = url.find_first_of("/?#", pos);
    std::string hostport =
        path_pos == std::string::npos ? url.substr(pos) : url.substr(pos, path_pos - pos);
    if (hostport.empty()) {
        parsed.error = "missing host";
        return parsed;
    }

    if (!hostport.empty() && hostport.front() == '[') {
        const auto close = hostport.find(']');
        if (close == std::string::npos) {
            parsed.error = "invalid IPv6 host";
            return parsed;
        }
        parsed.host = hostport.substr(1, close - 1);
        if (close + 1 < hostport.size() && hostport[close + 1] == ':') {
            parsed.port = std::atoi(hostport.c_str() + close + 2);
        }
    } else {
        const auto colon = hostport.rfind(':');
        if (colon != std::string::npos &&
            hostport.find(':') == colon) {
            parsed.host = hostport.substr(0, colon);
            parsed.port = std::atoi(hostport.c_str() + colon + 1);
        } else {
            parsed.host = hostport;
        }
    }

    if (path_pos == std::string::npos) {
        parsed.path = "/";
    } else {
        std::size_t end = url.size();
        const auto hash = url.find('#', path_pos);
        if (hash != std::string::npos) {
            parsed.fragment = url.substr(hash + 1);
            end = hash;
        }
        const auto query = url.find('?', path_pos);
        if (query != std::string::npos && query < end) {
            parsed.query = url.substr(query + 1, end - query - 1);
            end = query;
        }
        parsed.path = url.substr(path_pos, end - path_pos);
        if (parsed.path.empty()) {
            parsed.path = "/";
        }
    }

    if (parsed.port < 0) {
        if (parsed.scheme == "https" || parsed.scheme == "wss") {
            parsed.port = 443;
        } else if (parsed.scheme == "http" || parsed.scheme == "ws") {
            parsed.port = 80;
        } else {
            parsed.port = 80;
        }
    }

    parsed.ok = !parsed.host.empty();
    if (!parsed.ok) {
        parsed.error = "missing host";
    }
    return parsed;
}

std::string requestPath(const ParsedUrl& url) {
    std::string path = url.path.empty() ? "/" : url.path;
    if (!url.query.empty()) {
        path += "?";
        path += url.query;
    }
    return path;
}

std::string baseUrl(const ParsedUrl& url) {
    return url.scheme + "://" + url.host + ":" + std::to_string(url.port);
}

std::unique_ptr<httplib::Client> makeClient(const ParsedUrl& url,
                                            std::string& error) {
    if (!url.ok) {
        error = url.error.empty() ? "invalid URL" : url.error;
        return nullptr;
    }
    auto client = std::make_unique<httplib::Client>(baseUrl(url));
    client->set_connection_timeout(30, 0);
    client->set_read_timeout(30, 0);
    client->set_write_timeout(30, 0);
    if (!client->is_valid()) {
        error = "failed to create HTTP client";
        return nullptr;
    }
    return client;
}

std::string responseOrError(const httplib::Result& result) {
    if (!result) {
        return errorText(httplib::to_string(result.error()));
    }
    if (result->status >= 400) {
        return errorText("HTTP " + std::to_string(result->status) + " " +
                         httplib::status_message(result->status));
    }
    return result->body;
}

std::mutex g_ws_mutex;
std::map<std::string, std::unique_ptr<httplib::ws::WebSocketClient>> g_ws;

}  // namespace

extern "C" const char* network_get(const char* url) {
    const ParsedUrl parsed = parseUrl(textOrEmpty(url));
    std::string error;
    auto client = makeClient(parsed, error);
    if (!client) {
        return stable(errorText(error));
    }
    return stable(responseOrError(client->Get(requestPath(parsed))));
}

extern "C" std::int64_t network_getStatus(const char* url) {
    const ParsedUrl parsed = parseUrl(textOrEmpty(url));
    std::string error;
    auto client = makeClient(parsed, error);
    if (!client) {
        return -1;
    }
    const auto result = client->Get(requestPath(parsed));
    if (!result) {
        return -1;
    }
    return result->status;
}

extern "C" const char* network_getHeaders(const char* url) {
    const ParsedUrl parsed = parseUrl(textOrEmpty(url));
    std::string error;
    auto client = makeClient(parsed, error);
    if (!client) {
        return stable(errorText(error));
    }
    const auto result = client->Get(requestPath(parsed));
    if (!result) {
        return stable(errorText(httplib::to_string(result.error())));
    }
    if (result->status >= 400) {
        return stable(errorText("HTTP " + std::to_string(result->status)));
    }
    std::string headers;
    for (const auto& header : result->headers) {
        if (!headers.empty()) {
            headers += '\n';
        }
        headers += header.first;
        headers += ": ";
        headers += header.second;
    }
    return stable(std::move(headers));
}

extern "C" const char* network_post(const char* url, const char* body,
                                    const char* contentType) {
    const ParsedUrl parsed = parseUrl(textOrEmpty(url));
    std::string error;
    auto client = makeClient(parsed, error);
    if (!client) {
        return stable(errorText(error));
    }
    return stable(responseOrError(client->Post(
        requestPath(parsed), textOrEmpty(body),
        textOrEmpty(contentType).c_str())));
}

extern "C" const char* network_put(const char* url, const char* body,
                                   const char* contentType) {
    const ParsedUrl parsed = parseUrl(textOrEmpty(url));
    std::string error;
    auto client = makeClient(parsed, error);
    if (!client) {
        return stable(errorText(error));
    }
    return stable(responseOrError(client->Put(
        requestPath(parsed), textOrEmpty(body),
        textOrEmpty(contentType).c_str())));
}

extern "C" const char* network_delete(const char* url) {
    const ParsedUrl parsed = parseUrl(textOrEmpty(url));
    std::string error;
    auto client = makeClient(parsed, error);
    if (!client) {
        return stable(errorText(error));
    }
    return stable(responseOrError(client->Delete(requestPath(parsed))));
}

extern "C" const char* network_patch(const char* url, const char* body,
                                     const char* contentType) {
    const ParsedUrl parsed = parseUrl(textOrEmpty(url));
    std::string error;
    auto client = makeClient(parsed, error);
    if (!client) {
        return stable(errorText(error));
    }
    return stable(responseOrError(client->Patch(
        requestPath(parsed), textOrEmpty(body),
        textOrEmpty(contentType).c_str())));
}

extern "C" const char* network_getJson(const char* url) {
    const ParsedUrl parsed = parseUrl(textOrEmpty(url));
    std::string error;
    auto client = makeClient(parsed, error);
    if (!client) {
        return stable(errorText(error));
    }
    httplib::Headers headers = {{"Accept", "application/json"}};
    return stable(responseOrError(client->Get(requestPath(parsed), headers)));
}

extern "C" const char* network_postJson(const char* url, const char* jsonBody) {
    const ParsedUrl parsed = parseUrl(textOrEmpty(url));
    std::string error;
    auto client = makeClient(parsed, error);
    if (!client) {
        return stable(errorText(error));
    }
    httplib::Headers headers = {{"Accept", "application/json"}};
    return stable(responseOrError(client->Post(
        requestPath(parsed), headers, textOrEmpty(jsonBody),
        "application/json")));
}

extern "C" const char* network_download(const char* url, const char* filepath) {
    const ParsedUrl parsed = parseUrl(textOrEmpty(url));
    std::string error;
    auto client = makeClient(parsed, error);
    if (!client) {
        return stable(errorText(error));
    }
    const auto result = client->Get(requestPath(parsed));
    if (!result) {
        return stable(errorText(httplib::to_string(result.error())));
    }
    if (result->status >= 400) {
        return stable(errorText("HTTP " + std::to_string(result->status) + " " +
                                httplib::status_message(result->status)));
    }
    std::ofstream out(textOrEmpty(filepath), std::ios::binary);
    if (!out) {
        return stable(errorText("cannot write file"));
    }
    out.write(result->body.data(),
              static_cast<std::streamsize>(result->body.size()));
    if (!out) {
        return stable(errorText("write failed"));
    }
    return stable("ok");
}

extern "C" const char* network_urlencode(const char* text) {
    return stable(httplib::encode_query_component(textOrEmpty(text), true));
}

extern "C" std::int64_t network_httpHead(const char* url) {
    const ParsedUrl parsed = parseUrl(textOrEmpty(url));
    std::string error;
    auto client = makeClient(parsed, error);
    if (!client) {
        return -1;
    }
    const auto result = client->Head(requestPath(parsed));
    if (!result) {
        return -1;
    }
    return result->status;
}

extern "C" const char* network_urlScheme(const char* url) {
    return stable(parseUrl(textOrEmpty(url)).scheme);
}

extern "C" const char* network_urlHost(const char* url) {
    const ParsedUrl parsed = parseUrl(textOrEmpty(url));
    if (!parsed.ok) {
        return stable("");
    }
    if ((parsed.scheme == "http" && parsed.port == 80) ||
        (parsed.scheme == "https" && parsed.port == 443) ||
        (parsed.scheme == "ws" && parsed.port == 80) ||
        (parsed.scheme == "wss" && parsed.port == 443)) {
        return stable(parsed.host);
    }
    return stable(parsed.host + ":" + std::to_string(parsed.port));
}

extern "C" const char* network_urlPath(const char* url) {
    return stable(parseUrl(textOrEmpty(url)).path);
}

extern "C" const char* network_urlParse(const char* url) {
    const ParsedUrl parsed = parseUrl(textOrEmpty(url));
    std::string json = "{";
    json += "\"scheme\":\"" + parsed.scheme + "\",";
    json += "\"host\":\"" + parsed.host + "\",";
    json += "\"port\":" + std::to_string(parsed.port) + ",";
    json += "\"path\":\"" + parsed.path + "\",";
    json += "\"query\":\"" + parsed.query + "\",";
    json += "\"fragment\":\"" + parsed.fragment + "\"";
    json += "}";
    return stable(std::move(json));
}

extern "C" const char* network_getHostname() {
    char buffer[256];
    if (gethostname(buffer, sizeof(buffer)) != 0) {
        return stable("");
    }
    buffer[sizeof(buffer) - 1] = '\0';
    return stable(buffer);
}

extern "C" const char* network_resolveHost(const char* hostname) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(hostname, nullptr, &hints, &result) != 0 ||
        result == nullptr) {
        return stable(errorText("resolve failed"));
    }
    char host[NI_MAXHOST];
    const int status = getnameinfo(result->ai_addr, result->ai_addrlen, host,
                                   sizeof(host), nullptr, 0, NI_NUMERICHOST);
    freeaddrinfo(result);
    if (status != 0) {
        return stable(errorText("resolve failed"));
    }
    return stable(host);
}

extern "C" const char* network_wsConnect(const char* name, const char* uri) {
    const std::string key = textOrEmpty(name);
    if (key.empty()) {
        return stable(errorText("empty connection name"));
    }
    auto client =
        std::make_unique<httplib::ws::WebSocketClient>(textOrEmpty(uri));
    client->set_connection_timeout(10, 0);
    client->set_read_timeout(30, 0);
    const auto connected = client->connect();
    if (!connected) {
        return stable(errorText(httplib::to_string(connected.error())));
    }
    std::lock_guard<std::mutex> lock(g_ws_mutex);
    g_ws[key] = std::move(client);
    return stable("ok");
}

extern "C" const char* network_wsSend(const char* name, const char* message) {
    std::lock_guard<std::mutex> lock(g_ws_mutex);
    const auto it = g_ws.find(textOrEmpty(name));
    if (it == g_ws.end() || !it->second || !it->second->is_open()) {
        return stable(errorText("no connection named '" + textOrEmpty(name) +
                                "'"));
    }
    if (!it->second->send(textOrEmpty(message))) {
        return stable(errorText("send failed"));
    }
    return stable("ok");
}

extern "C" const char* network_wsReceive(const char* name) {
    std::unique_ptr<httplib::ws::WebSocketClient>* client = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_ws_mutex);
        const auto it = g_ws.find(textOrEmpty(name));
        if (it == g_ws.end() || !it->second || !it->second->is_open()) {
            return stable(errorText("no connection named '" +
                                    textOrEmpty(name) + "'"));
        }
        client = &it->second;
    }
    std::string message;
    const httplib::ws::ReadResult kind = (*client)->read(message);
    if (kind == httplib::ws::ReadResult::Fail) {
        return stable(errorText("receive failed"));
    }
    if (kind == httplib::ws::ReadResult::Timeout) {
        return stable(errorText("receive timeout"));
    }
    return stable(std::move(message));
}

extern "C" const char* network_wsSendReceive(const char* name,
                                             const char* message) {
    const char* sent = network_wsSend(name, message);
    if (std::string(sent).rfind("ERROR:", 0) == 0) {
        return sent;
    }
    return network_wsReceive(name);
}

extern "C" const char* network_wsClose(const char* name) {
    std::lock_guard<std::mutex> lock(g_ws_mutex);
    const auto it = g_ws.find(textOrEmpty(name));
    if (it == g_ws.end()) {
        return stable(errorText("no connection named '" + textOrEmpty(name) +
                                "'"));
    }
    if (it->second) {
        it->second->close();
    }
    g_ws.erase(it);
    return stable("ok");
}

extern "C" std::int64_t network_wsConnected(const char* name) {
    std::lock_guard<std::mutex> lock(g_ws_mutex);
    const auto it = g_ws.find(textOrEmpty(name));
    if (it == g_ws.end() || !it->second) {
        return 0;
    }
    return it->second->is_open() ? 1 : 0;
}

extern "C" int lynxer_module_init_v1(RegisterFunction function,
                                     RegisterConstant, RegisterType) {
    return function("get", "network_get", "cdecl:cstring(cstring)") &&
                   function("getStatus", "network_getStatus",
                            "cdecl:int64(cstring)") &&
                   function("getHeaders", "network_getHeaders",
                            "cdecl:cstring(cstring)") &&
                   function("post", "network_post",
                            "cdecl:cstring(cstring,cstring,cstring)") &&
                   function("put", "network_put",
                            "cdecl:cstring(cstring,cstring,cstring)") &&
                   function("delete", "network_delete",
                            "cdecl:cstring(cstring)") &&
                   function("patch", "network_patch",
                            "cdecl:cstring(cstring,cstring,cstring)") &&
                   function("getJson", "network_getJson",
                            "cdecl:cstring(cstring)") &&
                   function("postJson", "network_postJson",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("download", "network_download",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("urlencode", "network_urlencode",
                            "cdecl:cstring(cstring)") &&
                   function("httpHead", "network_httpHead",
                            "cdecl:int64(cstring)") &&
                   function("urlScheme", "network_urlScheme",
                            "cdecl:cstring(cstring)") &&
                   function("urlHost", "network_urlHost",
                            "cdecl:cstring(cstring)") &&
                   function("urlPath", "network_urlPath",
                            "cdecl:cstring(cstring)") &&
                   function("urlParse", "network_urlParse",
                            "cdecl:cstring(cstring)") &&
                   function("getHostname", "network_getHostname",
                            "cdecl:cstring()") &&
                   function("resolveHost", "network_resolveHost",
                            "cdecl:cstring(cstring)") &&
                   function("wsConnect", "network_wsConnect",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("wsSend", "network_wsSend",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("wsReceive", "network_wsReceive",
                            "cdecl:cstring(cstring)") &&
                   function("wsSendReceive", "network_wsSendReceive",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("wsClose", "network_wsClose",
                            "cdecl:cstring(cstring)") &&
                   function("wsConnected", "network_wsConnected",
                            "cdecl:int64(cstring)")
               ? 0
               : 1;
}
