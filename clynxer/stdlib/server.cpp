// Lynxer `server` stdlib backend: HTTP/WebSocket server via Crow + Boost.Asio.
// Companion to `network` (cpp-httplib client).

#ifndef CROW_USE_BOOST
#define CROW_USE_BOOST
#endif
#include "crow.h"

#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
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

struct FixedRoute {
    crow::HTTPMethod method = crow::HTTPMethod::GET;
    std::string path;
    std::string body;
    std::string content_type = "text/html";
    bool echo = false;
};

std::mutex g_mutex;
std::vector<FixedRoute> g_routes;
std::vector<std::string> g_ws_echo_paths;
std::unique_ptr<crow::SimpleApp> g_app;
std::future<void> g_runner;
bool g_running = false;
std::int64_t g_port = 0;

bool hasRoute(crow::HTTPMethod method, const std::string& path) {
    for (const auto& route : g_routes) {
        if (route.method == method && route.path == path) {
            return true;
        }
    }
    return false;
}

}  // namespace

extern "C" const char* server_routeGet(const char* path, const char* body) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_running) {
        return stable(errorText("server already running"));
    }
    const std::string route_path = textOrEmpty(path);
    if (route_path.empty() || route_path.front() != '/') {
        return stable(errorText("path must start with '/'"));
    }
    if (hasRoute(crow::HTTPMethod::GET, route_path)) {
        return stable(errorText("GET route already registered"));
    }
    g_routes.push_back(
        FixedRoute{crow::HTTPMethod::GET, route_path, textOrEmpty(body),
                   "text/plain", false});
    return stable("ok");
}

extern "C" const char* server_routePost(const char* path, const char* body) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_running) {
        return stable(errorText("server already running"));
    }
    const std::string route_path = textOrEmpty(path);
    if (route_path.empty() || route_path.front() != '/') {
        return stable(errorText("path must start with '/'"));
    }
    if (hasRoute(crow::HTTPMethod::POST, route_path)) {
        return stable(errorText("POST route already registered"));
    }
    g_routes.push_back(
        FixedRoute{crow::HTTPMethod::POST, route_path, textOrEmpty(body),
                   "text/plain", false});
    return stable("ok");
}

extern "C" const char* server_routeEcho(const char* path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_running) {
        return stable(errorText("server already running"));
    }
    const std::string route_path = textOrEmpty(path);
    if (route_path.empty() || route_path.front() != '/') {
        return stable(errorText("path must start with '/'"));
    }
    if (hasRoute(crow::HTTPMethod::POST, route_path)) {
        return stable(errorText("POST route already registered"));
    }
    g_routes.push_back(FixedRoute{crow::HTTPMethod::POST, route_path, "",
                                  "text/plain", true});
    return stable("ok");
}

extern "C" const char* server_wsEcho(const char* path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_running) {
        return stable(errorText("server already running"));
    }
    const std::string route_path = textOrEmpty(path);
    if (route_path.empty() || route_path.front() != '/') {
        return stable(errorText("path must start with '/'"));
    }
    for (const auto& existing : g_ws_echo_paths) {
        if (existing == route_path) {
            return stable(errorText("WebSocket route already registered"));
        }
    }
    g_ws_echo_paths.push_back(route_path);
    return stable("ok");
}

extern "C" const char* server_clearRoutes() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_running) {
        return stable(errorText("server already running"));
    }
    g_routes.clear();
    g_ws_echo_paths.clear();
    return stable("ok");
}

extern "C" const char* server_start(std::int64_t port) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_running) {
        return stable(errorText("server already running"));
    }
    if (port <= 0 || port > 65535) {
        return stable(errorText("invalid port"));
    }

    crow::logger::setLogLevel(crow::LogLevel::Error);
    g_app = std::make_unique<crow::SimpleApp>();

    for (const auto& route : g_routes) {
        if (route.echo) {
            g_app->route_dynamic(route.path)
                .methods(route.method)([](const crow::request& request) {
                    return crow::response(request.body);
                });
        } else {
            const std::string body = route.body;
            const std::string content_type = route.content_type;
            g_app->route_dynamic(route.path)
                .methods(route.method)([body, content_type]() {
                    crow::response response(body);
                    response.set_header("Content-Type", content_type);
                    return response;
                });
        }
    }

    for (const auto& path : g_ws_echo_paths) {
        g_app->route_dynamic(path)
            .websocket<crow::SimpleApp>(g_app.get())
            .onmessage([](crow::websocket::connection& connection,
                          const std::string& data, bool is_binary) {
                if (is_binary) {
                    connection.send_binary(data);
                } else {
                    connection.send_text(data);
                }
            });
    }

    try {
        g_runner =
            g_app->bindaddr("127.0.0.1").port(static_cast<uint16_t>(port))
                .multithreaded()
                .run_async();
    } catch (const std::exception& exception) {
        g_app.reset();
        return stable(errorText(exception.what()));
    }

    g_running = true;
    g_port = port;
    return stable("ok");
}

extern "C" const char* server_stop() {
    std::unique_ptr<crow::SimpleApp> app;
    std::future<void> runner;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_running) {
            return stable(errorText("server is not running"));
        }
        app = std::move(g_app);
        runner = std::move(g_runner);
        g_running = false;
        g_port = 0;
    }
    if (app) {
        app->stop();
    }
    if (runner.valid()) {
        runner.wait();
    }
    return stable("ok");
}

extern "C" std::int64_t server_running() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_running ? 1 : 0;
}

extern "C" std::int64_t server_port() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_port;
}

extern "C" int lynxer_module_init_v1(RegisterFunction function,
                                     RegisterConstant, RegisterType) {
    return function("routeGet", "server_routeGet",
                    "cdecl:cstring(cstring,cstring)") &&
                   function("routePost", "server_routePost",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("routeEcho", "server_routeEcho",
                            "cdecl:cstring(cstring)") &&
                   function("wsEcho", "server_wsEcho",
                            "cdecl:cstring(cstring)") &&
                   function("clearRoutes", "server_clearRoutes",
                            "cdecl:cstring()") &&
                   function("start", "server_start", "cdecl:cstring(int64)") &&
                   function("stop", "server_stop", "cdecl:cstring()") &&
                   function("running", "server_running", "cdecl:int64()") &&
                   function("port", "server_port", "cdecl:int64()")
               ? 0
               : 1;
}
