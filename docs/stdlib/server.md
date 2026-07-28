# server

Full-featured HTTP server built on **Flask**. Supports plain HTML/text routes, JSON APIs, Jinja2 templates, static file directories, complete static-site hosting, redirects, error pages, CORS, request-context readers, and HTTPS.

Requires Flask: `pip install flask`

---

## Quick start

```c
global setup(){ import("server"); }

global main(){
    global.server.init("0.0.0.0", 8080);
    global.server.get("/", "<h1>Hello from Lynxer!</h1>");
    global.server.run();
}
```

---

## Initialisation

| Function | Signature | Description |
|----------|-----------|-------------|
| `init` | `init(str host, int port)` | Create Flask app bound to `host:port`. Must be called first. |
| `setDebug` | `setDebug(bool enabled)` | Enable/disable Flask debug mode (auto-reloader). |
| `setTemplateFolder` | `setTemplateFolder(str path)` | Set the folder from which Jinja2 template files are loaded (default: `"templates"`). |

---

## Plain text / HTML routes

All route-registration functions must be called before `run()`.

| Function | Signature | Description |
|----------|-----------|-------------|
| `get` | `get(str path, str response)` | Register a `GET` route returning `response` as HTML/text. |
| `post` | `post(str path, str response)` | Register a `POST` route. |
| `put` | `put(str path, str response)` | Register a `PUT` route. |
| `delete` | `delete(str path, str response)` | Register a `DELETE` route. |
| `patch` | `patch(str path, str response)` | Register a `PATCH` route. |
| `any` | `any(str path, str response)` | Register a route for GET, POST, PUT, DELETE, and PATCH. |
| `getStatus` | `getStatus(str path, str response, int status)` | `GET` route with a custom HTTP status code. |

```c
global.server.get("/hello", "<p>Hello!</p>");
global.server.post("/submit", "<p>Received</p>");
global.server.getStatus("/teapot", "I'm a teapot", 418);
```

---

## JSON API routes

Responses are sent with `Content-Type: application/json`.

| Function | Signature | Description |
|----------|-----------|-------------|
| `jsonGet` | `jsonGet(str path, str jsonStr)` | `GET` route returning a JSON string. |
| `jsonPost` | `jsonPost(str path, str jsonStr)` | `POST` route returning a JSON string. |
| `jsonRoute` | `jsonRoute(str path, str jsonStr)` | `GET` + `POST` route returning JSON. |
| `jsonStatus` | `jsonStatus(str path, str jsonStr, int status)` | JSON route with a custom status code. |

```c
global.server.jsonGet("/api/status", "{\"ok\":true,\"version\":\"1.0\"}");
global.server.jsonStatus("/api/error", "{\"error\":\"not found\"}", 404);
```

---

## Jinja2 template routes

Templates are rendered from files in the template folder, or from an inline string.  
`dataJson` is a JSON object whose keys become template variables (e.g. `'{"title":"Home","user":"Alice"}'`). Pass `"{}"` for no variables.

| Function | Signature | Description |
|----------|-----------|-------------|
| `template` | `template(str path, str templateFile, str dataJson)` | `GET` route rendering a Jinja2 template file. |
| `templatePost` | `templatePost(str path, str templateFile, str dataJson)` | `POST` route rendering a template file. |
| `templateString` | `templateString(str path, str templateStr, str dataJson)` | `GET` route rendering an inline Jinja2 string. |

```c
// render templates/index.html with { title, user } variables
global.server.setTemplateFolder("./templates");
global.server.template("/", "index.html", "{\"title\":\"Home\",\"user\":\"Alice\"}");

// inline template
global.server.templateString("/greet",
    "<h1>Hello, {{ name }}!</h1>",
    "{\"name\":\"World\"}"
);
```

**templates/index.html** example:
```html
<!DOCTYPE html>
<html>
<head><title>{{ title }}</title></head>
<body><h1>Welcome, {{ user }}!</h1></body>
</html>
```

---

## Static files & sites

| Function | Signature | Description |
|----------|-----------|-------------|
| `staticFiles` | `staticFiles(str urlPrefix, str directory)` | Serve files from `directory` under the URL prefix. |
| `staticSite` | `staticSite(str directory)` | Host a complete static site: `/` → `index.html`, all other paths serve matching files. |
| `serveFile` | `serveFile(str path, str filepath)` | Serve a single file at `path`. |

```c
// serve ./public/assets under /assets
global.server.staticFiles("/assets", "./public/assets");

// host a built Vite/React/plain-HTML site from ./dist
global.server.staticSite("./dist");
```

---

## Redirects

| Function | Signature | Description |
|----------|-----------|-------------|
| `redirect` | `redirect(str path, str target)` | Temporary (302) redirect from `path` to `target`. |
| `redirect301` | `redirect301(str path, str target)` | Permanent (301) redirect. |

```c
global.server.redirect("/old", "/new");
global.server.redirect301("/blog", "https://example.com/blog");
```

---

## Error handlers

| Function | Signature | Description |
|----------|-----------|-------------|
| `notFound` | `notFound(str response)` | Custom 404 Not Found response. |
| `serverError` | `serverError(str response)` | Custom 500 Internal Server Error response. |
| `forbidden` | `forbidden(str response)` | Custom 403 Forbidden response. |
| `methodNotAllowed` | `methodNotAllowed(str response)` | Custom 405 Method Not Allowed response. |

```c
global.server.notFound("<h1>404 — Page not found</h1>");
global.server.serverError("<h1>500 — Something went wrong</h1>");
```

---

## CORS

| Function | Signature | Description |
|----------|-----------|-------------|
| `cors` | `cors()` | Add `Access-Control-Allow-Origin: *` to every response. |
| `corsOrigin` | `corsOrigin(str origin)` | Allow only the specified origin. |

```c
global.server.cors();                          // allow all
global.server.corsOrigin("https://app.com");   // specific origin
```

---

## Global response headers & logging

| Function | Signature | Description |
|----------|-----------|-------------|
| `addGlobalHeader` | `addGlobalHeader(str key, str val)` | Append a header to every response. |
| `enableRequestLog` | `enableRequestLog()` | Print `METHOD /path` to stdout before each request. |

```c
global.server.addGlobalHeader("X-Powered-By", "Lynxer");
global.server.enableRequestLog();
```

---

## Request context readers

These functions read data from the **current request** and are intended for use inside `rawPy` blocks in route handler setup, or for introspection after `run()` (advanced use).

| Function | Signature | Description |
|----------|-----------|-------------|
| `getArg` | `getArg(str name)` | URL query-string parameter, or `""`. |
| `getForm` | `getForm(str name)` | POST form field, or `""`. |
| `getBody` | `getBody()` | Raw request body as a string. |
| `getHeader` | `getHeader(str name)` | Request header value, or `""`. |
| `getMethod` | `getMethod()` | HTTP method (`"GET"`, `"POST"`, …). |
| `getUrl` | `getUrl()` | Full request URL. |
| `getPath` | `getPath()` | Request path (no host or query string). |
| `getRemoteAddr` | `getRemoteAddr()` | Client IP address. |
| `getCookie` | `getCookie(str name)` | Cookie value, or `""`. |
| `getContentType` | `getContentType()` | `Content-Type` of the request. |

---

## Server startup

| Function | Signature | Description |
|----------|-----------|-------------|
| `run` | `run()` | Start the server (blocking). |
| `runHTTPS` | `runHTTPS(str cert, str key)` | Start with SSL using PEM certificate and key files. |
| `runSSLAdhoc` | `runSSLAdhoc()` | Start with an auto-generated self-signed cert (requires `pyOpenSSL`). |

```c
global.server.run();
global.server.runHTTPS("cert.pem", "key.pem");
```

---

## Complete example — JSON API + static site

```c
global setup(){ import("server"); }

global main(){
    global.server.init("0.0.0.0", 3000);
    global.server.setDebug(true);
    global.server.cors();
    global.server.enableRequestLog();

    // static site from ./public
    global.server.staticSite("./public");

    // JSON API
    global.server.jsonGet("/api/ping", "{\"pong\":true}");
    global.server.jsonGet("/api/info", "{\"name\":\"MyApp\",\"version\":\"1.0\"}");

    // custom error pages
    global.server.notFound("<h1>404 — Not Found</h1>");
    global.server.serverError("<h1>500 — Server Error</h1>");

    global.server.run();
}
```

---

## Complete example — Jinja2 template site

```c
global setup(){ import("server"); }

global main(){
    global.server.init("127.0.0.1", 5000);
    global.server.setTemplateFolder("./templates");

    global.server.template("/",        "index.html",   "{\"title\":\"Home\"}");
    global.server.template("/about",   "about.html",   "{\"title\":\"About\"}");
    global.server.template("/contact", "contact.html", "{\"title\":\"Contact\"}");

    global.server.redirect("/home", "/");
    global.server.notFound("<h1>Page not found</h1>");

    global.server.run();
}
```
