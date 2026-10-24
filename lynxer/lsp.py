#!/usr/bin/env python3
"""Small stdio Language Server Protocol implementation for Lynxer."""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlparse


def project_root() -> Path:
    configured = os.environ.get("LYNXER_ROOT")
    if configured:
        return Path(configured)
    try:
        return Path(__file__).resolve().parents[1]
    except NameError:
        return Path.cwd()


ROOT = project_root()
DECLARATION = re.compile(
    r"^\s*(?:(global|func|local|lambda|async|class|struct|enum)\s+)?"
    r"([A-Za-z_]\w*)"
)
FUNCTION = re.compile(
    r"^\s*(global|func|local|lambda|async)\s+([A-Za-z_]\w*)\s*\((.*)"
)
VARIABLE = re.compile(
    r"^\s*(?:const\s+)?(?:[A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*="
)
STATIC_DIAGNOSTIC = re.compile(
    r"^(.+):(\d+):(\d+): (error|warning): (.*)$"
)
SOURCE_DIAGNOSTIC = re.compile(r"^lynxer: (.+):(\d+):(\d+): (.*)$")

KEYWORDS = [
    "global", "func", "local", "lambda", "class", "extends", "struct",
    "enum", "setup", "main", "return", "if", "elif", "else", "while",
    "for", "doWhile", "iterate", "forever", "switch", "case", "default",
    "try", "catch", "raise", "break", "continue", "restart", "new",
    "this", "super", "import", "importAs", "export", "const", "shared",
    "type", "of", "await", "exec", "codeblock", "inter", "true", "false",
    "none", "and", "or", "not", "xor", "is", "isnt",
]
TYPES = [
    "any", "int", "float", "num", "char", "str", "bool", "numBool", "bit",
    "byte", "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32",
    "uint64", "float32", "float64", "list", "tuple", "sentinel", "object",
    "codeblock", "function", "functionAddress", "bytes",
]


def executable() -> str | None:
    configured = os.environ.get("LYNXER_EXECUTABLE")
    if configured:
        return configured
    candidate = ROOT / "lynxer" / ("lynxer.exe" if os.name == "nt" else "lynxer")
    if candidate.is_file():
        return str(candidate)
    return shutil.which("lynxer")


def uri_path(uri: str) -> str:
    parsed = urlparse(uri)
    path = unquote(parsed.path)
    if os.name == "nt" and path.startswith("/") and len(path) > 2 and path[2] == ":":
        path = path[1:]
    return path


def position_offset(text: str, line: int, character: int) -> int:
    lines = text.splitlines(keepends=True)
    if not lines:
        return 0
    line = min(max(line, 0), len(lines) - 1)
    return sum(len(item) for item in lines[:line]) + min(character, len(lines[line]))


def word_at(text: str, position: dict[str, int]) -> tuple[str, int, int]:
    offset = position_offset(text, position.get("line", 0), position.get("character", 0))
    start = offset
    end = offset
    while start > 0 and (text[start - 1].isalnum() or text[start - 1] == "_"):
        start -= 1
    while end < len(text) and (text[end].isalnum() or text[end] == "_"):
        end += 1
    return text[start:end], start, end


def declaration_locations(uri: str, text: str) -> list[dict[str, Any]]:
    declarations: list[dict[str, Any]] = []
    for line_number, line in enumerate(text.splitlines()):
        function = FUNCTION.match(line)
        if function:
            kind, name, signature = function.groups()
            start = line.find(name)
            declarations.append({
                "name": name, "kind": kind, "signature": line.strip(),
                "line": line_number, "character": start, "uri": uri,
            })
            continue
        match = DECLARATION.match(line)
        if match and match.group(1) in {"class", "struct", "enum"}:
            kind, name = match.groups()
            declarations.append({
                "name": name, "kind": kind, "signature": line.strip(),
                "line": line_number, "character": line.find(name), "uri": uri,
            })
            continue
        variable = VARIABLE.match(line)
        if variable:
            name = variable.group(1)
            declarations.append({
                "name": name, "kind": "variable", "signature": line.strip(),
                "line": line_number, "character": line.find(name), "uri": uri,
            })
    return declarations


class LynxerLanguageServer:
    def __init__(self) -> None:
        self.documents: dict[str, dict[str, Any]] = {}
        self.shutdown_requested = False
        self.docs_root = ROOT / "docs"
        self.binary = executable()

    def send(self, payload: dict[str, Any]) -> None:
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        sys.stdout.buffer.write(f"Content-Length: {len(body)}\r\n\r\n".encode())
        sys.stdout.buffer.write(body)
        sys.stdout.buffer.flush()

    def respond(self, request_id: Any, result: Any) -> None:
        self.send({"jsonrpc": "2.0", "id": request_id, "result": result})

    def notify(self, method: str, params: dict[str, Any]) -> None:
        self.send({"jsonrpc": "2.0", "method": method, "params": params})

    def diagnostics(self, uri: str) -> list[dict[str, Any]]:
        document = self.documents.get(uri)
        if document is None:
            return []
        if self.binary is None:
            return [self.make_diagnostic(
                1, 1, "warning",
                "Lynxer executable not found; set LYNXER_EXECUTABLE to enable diagnostics",
            )]
        text = document["text"]
        diagnostics: list[dict[str, Any]] = []
        try:
            original_path = uri_path(uri)
            temp_directory = Path(original_path).parent if original_path else None
            if temp_directory is not None and not temp_directory.is_dir():
                temp_directory = None
            with tempfile.NamedTemporaryFile(
                mode="w", suffix=".lynx", encoding="utf-8", newline="\n",
                delete=False, dir=temp_directory,
            ) as source:
                source.write(text)
                source_path = source.name
            try:
                result = subprocess.run(
                    [self.binary, "--lint", source_path],
                    capture_output=True, text=True, timeout=15, check=False,
                )
            finally:
                os.unlink(source_path)
        except (OSError, subprocess.TimeoutExpired) as error:
            return [{
                "range": {"start": {"line": 0, "character": 0},
                          "end": {"line": 0, "character": 1}},
                "severity": 2, "source": "lynxer-lsp",
                "message": f"Could not run Lynxer static analysis: {error}",
            }]

        output = (result.stderr + "\n" + result.stdout).splitlines()
        for line in output:
            match = STATIC_DIAGNOSTIC.match(line)
            if match:
                _, line_number, column, severity, message = match.groups()
                diagnostics.append(self.make_diagnostic(
                    int(line_number), int(column), severity, message
                ))
                continue
            match = SOURCE_DIAGNOSTIC.match(line)
            if match:
                _, line_number, column, message = match.groups()
                diagnostics.append(self.make_diagnostic(
                    int(line_number), int(column), "error", message
                ))
        if result.returncode != 0 and not diagnostics:
            message = (result.stderr or result.stdout).strip()
            if message:
                diagnostics.append(self.make_diagnostic(1, 1, "error", message))
        return diagnostics

    @staticmethod
    def make_diagnostic(line: int, column: int, severity: str,
                        message: str) -> dict[str, Any]:
        start_line = max(0, line - 1)
        start_column = max(0, column - 1)
        return {
            "range": {
                "start": {"line": start_line, "character": start_column},
                "end": {"line": start_line, "character": start_column + 1},
            },
            "severity": 1 if severity == "error" else 2,
            "source": "lynxer",
            "message": message,
        }

    def publish_diagnostics(self, uri: str) -> None:
        document = self.documents.get(uri, {})
        self.notify("textDocument/publishDiagnostics", {
            "uri": uri,
            "version": document.get("version"),
            "diagnostics": self.diagnostics(uri),
        })

    def all_declarations(self) -> list[dict[str, Any]]:
        result: list[dict[str, Any]] = []
        for uri, document in self.documents.items():
            result.extend(declaration_locations(uri, document["text"]))
        return result

    def completion(self, uri: str, position: dict[str, int]) -> dict[str, Any]:
        document = self.documents.get(uri, {"text": ""})
        text = document["text"]
        offset = position_offset(text, position.get("line", 0),
                                 position.get("character", 0))
        before = text[:offset]
        prefix_match = re.search(r"[A-Za-z_]\w*$", before)
        prefix = prefix_match.group(0) if prefix_match else ""
        items: dict[str, dict[str, Any]] = {}
        for word in KEYWORDS + TYPES:
            if word.startswith(prefix):
                items[word] = {"label": word, "kind": 14, "detail": "Lynxer keyword/type"}
        for declaration in self.all_declarations():
            name = declaration["name"]
            if name.startswith(prefix):
                kind = 3 if declaration["kind"] in {
                    "global", "func", "local", "lambda", "async"
                } else (7 if declaration["kind"] in {"class", "struct", "enum"} else 6)
                items[name] = {
                    "label": name, "kind": kind,
                    "detail": declaration["signature"],
                    "data": {"name": name},
                }
        for name in self.builtin_names():
            if name.startswith(prefix):
                items.setdefault(name, {
                    "label": name, "kind": 3, "detail": "Lynxer built-in",
                    "data": {"name": name},
                })
        start_character = position.get("character", 0) - len(prefix)
        for item in items.values():
            item["textEdit"] = {
                "range": {
                    "start": {"line": position.get("line", 0),
                              "character": max(0, start_character)},
                    "end": {"line": position.get("line", 0),
                            "character": position.get("character", 0)},
                },
                "newText": item["label"],
            }
        return {"isIncomplete": False, "items": list(items.values())}

    def builtin_names(self) -> list[str]:
        page = self.docs_root / "builtins.md"
        if not page.is_file():
            return []
        names: set[str] = set()
        for match in re.finditer(r"`([A-Za-z_]\w*)\(", page.read_text(encoding="utf-8")):
            names.add(match.group(1))
        return sorted(names)

    def definition(self, name: str) -> list[dict[str, Any]]:
        matches = []
        for declaration in self.all_declarations():
            if declaration["name"] != name:
                continue
            line = declaration["line"]
            column = declaration["character"]
            matches.append({
                "uri": declaration["uri"],
                "range": {"start": {"line": line, "character": column},
                          "end": {"line": line, "character": column + len(name)}},
            })
        return matches

    def references(self, name: str) -> list[dict[str, Any]]:
        references = []
        for uri, document in self.documents.items():
            for line_number, line in enumerate(document["text"].splitlines()):
                for match in re.finditer(rf"\b{re.escape(name)}\b", line):
                    references.append({
                        "uri": uri,
                        "range": {"start": {"line": line_number,
                                             "character": match.start()},
                                  "end": {"line": line_number,
                                           "character": match.end()}},
                    })
        return references

    def documentation(self, name: str) -> str:
        for page_name in ("functions.md", "classes.md", "types.md", "builtins.md"):
            page = self.docs_root / page_name
            if not page.is_file():
                continue
            lines = page.read_text(encoding="utf-8").splitlines()
            for index, line in enumerate(lines):
                if f"`{name}`" not in line and not re.search(
                    rf"\b{re.escape(name)}\s*\(", line
                ):
                    continue
                heading = index
                while heading > 0 and not lines[heading].startswith("#"):
                    heading -= 1
                excerpt = lines[heading:min(len(lines), heading + 12)]
                return "\n".join(excerpt).strip()
        return ""

    def hover(self, uri: str, position: dict[str, int]) -> dict[str, Any] | None:
        document = self.documents.get(uri)
        if document is None:
            return None
        name, _, _ = word_at(document["text"], position)
        if not name:
            return None
        declaration = next((item for item in self.all_declarations()
                            if item["name"] == name), None)
        content = f"`{name}`"
        if declaration:
            content = f"```lynx\n{declaration['signature']}\n```"
        hint = self.documentation(name)
        if hint:
            content += "\n\n" + hint
        elif name in self.builtin_names():
            content += "\n\nLynxer built-in function."
        return {"contents": {"kind": "markdown", "value": content}}

    def document_symbols(self, uri: str) -> list[dict[str, Any]]:
        document = self.documents.get(uri)
        if document is None:
            return []
        symbols = []
        kinds = {"global": 12, "func": 12, "local": 12, "lambda": 12,
                 "async": 12, "class": 5, "struct": 23, "enum": 10,
                 "variable": 13}
        lines = document["text"].splitlines()
        for declaration in declaration_locations(uri, document["text"]):
            line = declaration["line"]
            column = declaration["character"]
            name = declaration["name"]
            symbols.append({
                "name": name, "kind": kinds.get(declaration["kind"], 13),
                "detail": declaration["signature"],
                "range": {"start": {"line": line, "character": 0},
                          "end": {"line": line,
                                   "character": len(lines[line])}},
                "selectionRange": {
                    "start": {"line": line, "character": column},
                    "end": {"line": line, "character": column + len(name)},
                },
            })
        return symbols

    def handle(self, message: dict[str, Any]) -> None:
        method = message.get("method")
        params = message.get("params", {})
        request_id = message.get("id")
        if method == "initialize":
            root_uri = params.get("rootUri") or params.get("rootPath")
            if root_uri:
                candidate = uri_path(root_uri) if root_uri.startswith("file:") else root_uri
                docs = Path(candidate) / "docs"
                if docs.is_dir():
                    self.docs_root = docs
            self.respond(request_id, {
                "capabilities": {
                    "textDocumentSync": 1,
                    "completionProvider": {
                        "resolveProvider": True,
                        "triggerCharacters": [".", " "],
                    },
                    "definitionProvider": True,
                    "referencesProvider": True,
                    "hoverProvider": True,
                    "documentSymbolProvider": True,
                    "workspaceSymbolProvider": True,
                },
                "serverInfo": {"name": "lynxer-lsp", "version": "0.1.0"},
            })
            return
        if method == "initialized":
            return
        if method == "shutdown":
            self.shutdown_requested = True
            self.respond(request_id, None)
            return
        if method == "exit":
            return
        if method == "textDocument/didOpen":
            item = params["textDocument"]
            self.documents[item["uri"]] = {
                "text": item["text"], "version": item.get("version", 0)
            }
            self.publish_diagnostics(item["uri"])
            return
        if method == "textDocument/didChange":
            item = params["textDocument"]
            changes = params.get("contentChanges", [])
            if item["uri"] in self.documents and changes:
                self.documents[item["uri"]]["text"] = changes[-1]["text"]
                self.documents[item["uri"]]["version"] = item.get("version")
                self.publish_diagnostics(item["uri"])
            return
        if method == "textDocument/didSave":
            uri = params["textDocument"]["uri"]
            if "text" in params and uri in self.documents:
                self.documents[uri]["text"] = params["text"]
            self.publish_diagnostics(uri)
            return
        if method == "textDocument/didClose":
            uri = params["textDocument"]["uri"]
            self.documents.pop(uri, None)
            self.notify("textDocument/publishDiagnostics", {"uri": uri, "diagnostics": []})
            return
        if method == "textDocument/completion":
            uri = params["textDocument"]["uri"]
            self.respond(request_id, self.completion(uri, params["position"]))
            return
        if method == "completionItem/resolve":
            item = params
            name = item.get("data", {}).get("name", item.get("label", ""))
            detail = self.documentation(name)
            if detail:
                item["documentation"] = {"kind": "markdown", "value": detail}
            self.respond(request_id, item)
            return
        if method == "textDocument/hover":
            result = self.hover(params["textDocument"]["uri"], params["position"])
            self.respond(request_id, result)
            return
        if method == "textDocument/definition":
            uri = params["textDocument"]["uri"]
            name, _, _ = word_at(self.documents.get(uri, {}).get("text", ""),
                                 params["position"])
            matches = self.definition(name)
            self.respond(request_id, matches[0] if len(matches) == 1 else matches)
            return
        if method == "textDocument/references":
            uri = params["textDocument"]["uri"]
            name, _, _ = word_at(self.documents.get(uri, {}).get("text", ""),
                                 params["position"])
            self.respond(request_id, self.references(name))
            return
        if method == "textDocument/documentSymbol":
            self.respond(request_id,
                         self.document_symbols(params["textDocument"]["uri"]))
            return
        if method == "workspace/symbol":
            query = params.get("query", "").lower()
            symbols = []
            for declaration in self.all_declarations():
                if query and query not in declaration["name"].lower():
                    continue
                line = declaration["line"]
                column = declaration["character"]
                name = declaration["name"]
                symbols.append({
                    "name": name, "kind": 12, "containerName": declaration["kind"],
                    "location": {
                        "uri": declaration["uri"],
                        "range": {"start": {"line": line, "character": column},
                                  "end": {"line": line,
                                           "character": column + len(name)}},
                    },
                })
            self.respond(request_id, symbols)
            return
        if request_id is not None:
            self.send({"jsonrpc": "2.0", "id": request_id,
                       "error": {"code": -32601, "message": f"Method not found: {method}"}})


def read_message() -> dict[str, Any] | None:
    headers: dict[str, str] = {}
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None
        if line in (b"\r\n", b"\n"):
            break
        key, separator, value = line.decode("ascii", errors="replace").partition(":")
        if separator:
            headers[key.lower()] = value.strip()
    length = int(headers.get("content-length", "0"))
    if length <= 0:
        return None
    body = sys.stdin.buffer.read(length)
    if len(body) != length:
        return None
    return json.loads(body.decode("utf-8"))


def main() -> int:
    server = LynxerLanguageServer()
    while True:
        message = read_message()
        if message is None:
            return 0
        if message.get("method") == "exit":
            return 0
        server.handle(message)


if __name__ == "__main__":
    raise SystemExit(main())
