#!/usr/bin/env python3
"""End-to-end JSON-RPC test for the stdio Lynxer language server."""

from __future__ import annotations

import argparse
import json
import os
import queue
import subprocess
import sys
import tempfile
import threading
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
DOCUMENT = """global setup(){}

global addOne(int value) -> int {
    return value + 1;
}

global apply(function operation, int value) -> int {
    return operation(value);
}

global main(){
    int unused = 1;
    int broken = "text";
    function increment = addOne;
    println(apply(increment, 4));
    println(broken);
}
"""


class Client:
    def __init__(self, process: subprocess.Popen[bytes]) -> None:
        self.process = process
        self.next_id = 1
        self.messages: queue.Queue[dict[str, Any] | None] = queue.Queue()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self) -> None:
        assert self.process.stdout is not None
        stream = self.process.stdout
        while True:
            headers: dict[str, str] = {}
            while True:
                line = stream.readline()
                if not line:
                    self.messages.put(None)
                    return
                if line in (b"\r\n", b"\n"):
                    break
                key, _, value = line.decode("ascii").partition(":")
                headers[key.strip().lower()] = value.strip()
            length = int(headers.get("content-length", "0"))
            body = stream.read(length)
            if len(body) != length:
                self.messages.put(None)
                return
            self.messages.put(json.loads(body))

    def send(self, payload: dict[str, Any]) -> None:
        assert self.process.stdin is not None
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self.process.stdin.write(
            f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body
        )
        self.process.stdin.flush()

    def request(self, method: str, params: dict[str, Any]) -> Any:
        request_id = self.next_id
        self.next_id += 1
        self.send({
            "jsonrpc": "2.0", "id": request_id,
            "method": method, "params": params,
        })
        while True:
            try:
                message = self.messages.get(timeout=15)
            except queue.Empty:
                raise SystemExit(
                    f"timed out waiting for {method} response") from None
            if message is None:
                raise SystemExit(f"language server exited during {method}")
            if message.get("id") != request_id:
                continue
            if "error" in message:
                raise SystemExit(f"{method} failed: {message['error']}")
            return message.get("result")

    def notification(self, method: str, params: dict[str, Any]) -> None:
        self.send({"jsonrpc": "2.0", "method": method, "params": params})

    def wait_notification(self, method: str, uri: str) -> dict[str, Any]:
        while True:
            try:
                message = self.messages.get(timeout=15)
            except queue.Empty:
                raise SystemExit(
                    f"timed out waiting for {method} notification") from None
            if message is None:
                raise SystemExit(f"language server exited while waiting for {method}")
            if message.get("method") != method:
                continue
            if message.get("params", {}).get("uri") == uri:
                return message


def check(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"LSP test failed: {message}")


def position_of(text: str, line_text: str, needle: str, occurrence: int = 1) -> dict[str, int]:
    line = next(index for index, value in enumerate(text.splitlines())
                if line_text in value)
    column = -1
    start = 0
    for _ in range(occurrence):
        column = text.splitlines()[line].index(needle, start)
        start = column + len(needle)
    return {"line": line, "character": column + len(needle)}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lynxer", default=str(ROOT / "lynxer" / "lynxer"))
    args = parser.parse_args()

    binary = Path(args.lynxer).resolve()
    check(binary.is_file(), f"lynxer executable not found: {binary}")

    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".lynx", delete=False, encoding="utf-8"
    ) as source:
        source.write(DOCUMENT)
        source_path = Path(source.name)
    uri = source_path.as_uri()

    environment = os.environ.copy()
    environment["LYNXER_EXECUTABLE"] = str(binary)
    process = subprocess.Popen(
        [sys.executable, str(ROOT / "lynxer" / "lsp.py")],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        cwd=ROOT, env=environment,
    )
    client = Client(process)

    try:
        initialized = client.request("initialize", {
            "processId": os.getpid(), "rootUri": None,
            "capabilities": {},
        })
        check(initialized["serverInfo"]["name"] == "lynxer-lsp",
              "unexpected serverInfo")
        capabilities = initialized["capabilities"]
        for capability in ("completionProvider", "definitionProvider",
                           "referencesProvider", "hoverProvider",
                           "documentSymbolProvider", "workspaceSymbolProvider"):
            check(capability in capabilities, f"missing {capability}")
        client.notification("initialized", {})

        client.notification("textDocument/didOpen", {
            "textDocument": {
                "uri": uri, "languageId": "lynxer",
                "version": 1, "text": DOCUMENT,
            },
        })
        published = client.wait_notification(
            "textDocument/publishDiagnostics", uri)
        diagnostics = published["params"]["diagnostics"]
        messages = [item["message"] for item in diagnostics]
        check(len(diagnostics) == 2,
              f"expected 2 diagnostics, received {messages}")
        check(any("unused variable 'unused'" in item for item in messages),
              f"missing unused-variable diagnostic: {messages}")
        check(any("initializer for 'broken'" in item for item in messages),
              f"missing type diagnostic: {messages}")
        check(any(item["severity"] == 2 for item in diagnostics),
              "unused variable should be a warning")
        check(any(item["severity"] == 1 for item in diagnostics),
              "type mismatch should be an error")

        completion = client.request("textDocument/completion", {
            "textDocument": {"uri": uri},
            "position": position_of(DOCUMENT, "println(apply", "pr"),
        })
        labels = [item["label"] for item in completion["items"]]
        check("println" in labels,
              f"completion did not include println: {labels[:10]}")

        hover = client.request("textDocument/hover", {
            "textDocument": {"uri": uri},
            "position": position_of(DOCUMENT, "int broken", "broken"),
        })
        check(hover is not None and "int broken" in hover["contents"]["value"],
              f"hover did not describe the declaration: {hover}")

        definition = client.request("textDocument/definition", {
            "textDocument": {"uri": uri},
            "position": position_of(DOCUMENT, "function increment", "increment"),
        })
        check(definition["range"]["start"]["line"] == 13,
              f"definition returned the wrong line: {definition}")

        references = client.request("textDocument/references", {
            "textDocument": {"uri": uri},
            "position": position_of(DOCUMENT, "function increment", "addOne"),
            "context": {"includeDeclaration": True},
        })
        check(len(references) >= 2,
              f"expected declaration and usage references: {references}")

        symbols = client.request("textDocument/documentSymbol", {
            "textDocument": {"uri": uri},
        })
        names = [symbol["name"] for symbol in symbols]
        for expected in ("setup", "addOne", "apply", "main", "increment"):
            check(expected in names, f"document symbol missing {expected}: {names}")

        workspace = client.request("workspace/symbol", {"query": "app"})
        check(any(symbol["name"] == "apply" for symbol in workspace),
              f"workspace symbol did not find apply: {workspace}")

        client.request("shutdown", None)
        client.notification("exit", None)
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            raise SystemExit("language server did not exit after shutdown")
        check(process.returncode == 0,
              f"language server exited {process.returncode}")
    finally:
        source_path.unlink(missing_ok=True)
        if process.poll() is None:
            process.kill()

    print("LSP test passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
