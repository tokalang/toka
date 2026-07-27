#!/usr/bin/env python3

import argparse
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading


class LspClient:
    def __init__(self, command, cwd, env):
        self.process = subprocess.Popen(
            [str(part) for part in command],
            cwd=str(cwd),
            env=env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.messages = queue.Queue()
        self.reader = threading.Thread(target=self._read_messages, daemon=True)
        self.reader.start()

    def _read_messages(self):
        try:
            while True:
                headers = {}
                while True:
                    line = self.process.stdout.readline()
                    if not line:
                        return
                    if line in (b"\n", b"\r\n"):
                        break
                    name, value = line.decode("ascii").split(":", 1)
                    headers[name.lower()] = value.strip()
                length = int(headers["content-length"])
                body = self.process.stdout.read(length)
                if len(body) != length:
                    return
                self.messages.put(json.loads(body.decode("utf-8")))
        except Exception as error:
            self.messages.put(error)

    def send(self, message):
        body = json.dumps(message, separators=(",", ":")).encode("utf-8")
        frame = b"Content-Length: %d\r\n\r\n" % len(body) + body
        self.process.stdin.write(frame)
        self.process.stdin.flush()

    def receive(self, predicate, timeout=15):
        while True:
            message = self.messages.get(timeout=timeout)
            if isinstance(message, Exception):
                raise message
            if predicate(message):
                return message

    def request(self, request_id, method, params=None):
        message = {"jsonrpc": "2.0", "id": request_id, "method": method}
        if params is not None:
            message["params"] = params
        self.send(message)
        return self.receive(lambda item: item.get("id") == request_id)

    def notification(self, method, params=None):
        message = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        self.send(message)

    def diagnostics(self, uri, timeout=15):
        return self.receive(
            lambda item: item.get("method") == "textDocument/publishDiagnostics"
            and item.get("params", {}).get("uri") == uri,
            timeout=timeout,
        )["params"]["diagnostics"]

    def fail_with_stderr(self, message):
        self.process.terminate()
        _, stderr = self.process.communicate(timeout=5)
        raise RuntimeError(message + "\n" + stderr.decode("utf-8", errors="replace"))


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def text_document_position(uri, line, character):
    return {
        "textDocument": {"uri": uri},
        "position": {"line": line, "character": character},
    }


def utf16_column(text, character):
    return len(text[:character].encode("utf-16-le")) // 2


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[2]
    build_dir = (root / args.build_dir).resolve()
    suffix = ".exe" if sys.platform == "win32" else ""
    tokalsp = build_dir / "bin" / ("tokalsp" + suffix)
    require(tokalsp.is_file(), "tokalsp binary is missing")

    env = os.environ.copy()
    env["PATH"] = str(build_dir / "bin") + os.pathsep + env.get("PATH", "")
    env["TOKA_LIB"] = str(root / "lib")

    with tempfile.TemporaryDirectory(prefix="tokalsp-protocol-") as temp:
        uri = (Path(temp) / "main.tk").as_uri()
        client = LspClient([tokalsp], root, env)
        try:
            initialized = client.request(
                "initialize-1",
                "initialize",
                {"processId": None, "rootUri": Path(temp).as_uri(), "capabilities": {}},
            )
            capabilities = initialized["result"]["capabilities"]
            for capability in (
                "textDocumentSync",
                "hoverProvider",
                "definitionProvider",
                "referencesProvider",
                "completionProvider",
                "renameProvider",
                "signatureHelpProvider",
                "documentSymbolProvider",
                "workspaceSymbolProvider",
                "documentFormattingProvider",
            ):
                require(capability in capabilities, "missing LSP capability: " + capability)
            client.notification("initialized", {})

            invalid = "fn main() -> i32 { return missing_name }\n"
            client.notification(
                "textDocument/didOpen",
                {
                    "textDocument": {
                        "uri": uri,
                        "languageId": "toka",
                        "version": 1,
                        "text": invalid,
                    }
                },
            )
            require(client.diagnostics(uri), "didOpen did not publish compiler diagnostics")

            valid_lines = [
                "fn bump(x: i32) -> i32 {",
                "    auto y = x + 1",
                "    return y",
                "}",
                "fn main() -> i32 { return bump(1) - 2 }",
            ]
            valid = "\n".join(valid_lines) + "\n"
            client.notification(
                "textDocument/didChange",
                {
                    "textDocument": {"uri": uri, "version": 2},
                    "contentChanges": [{"text": valid}],
                },
            )
            require(client.diagnostics(uri) == [], "valid document retained diagnostics")

            bump_use = valid_lines[4].index("bump")
            definition = client.request(
                2,
                "textDocument/definition",
                text_document_position(uri, 4, bump_use + 1),
            )["result"]
            require(definition["range"]["start"]["line"] == 0,
                    "definition did not resolve to bump declaration")

            y_use = valid_lines[2].index("y")
            hover = client.request(
                3,
                "textDocument/hover",
                text_document_position(uri, 2, y_use),
            )["result"]
            require("variable y: i32" in hover["contents"]["value"],
                    "hover did not show the resolved local type")

            references_params = text_document_position(uri, 2, y_use)
            references_params["context"] = {"includeDeclaration": True}
            references = client.request(
                4, "textDocument/references", references_params
            )["result"]
            require(len(references) == 2, "references did not find declaration and use")

            completion = client.request(
                5,
                "textDocument/completion",
                text_document_position(uri, 1, valid_lines[1].index("y")),
            )["result"]
            labels = {item["label"] for item in completion["items"]}
            require({"fn", "bump", "y"}.issubset(labels),
                    "completion list is missing keywords or document symbols")

            rename_params = text_document_position(uri, 2, y_use)
            rename_params["newName"] = "result"
            rename = client.request(6, "textDocument/rename", rename_params)["result"]
            edits = rename["changes"][uri]
            require(len(edits) == 2 and all(edit["newText"] == "result" for edit in edits),
                    "rename did not edit all symbol occurrences")

            library = Path(temp) / "lib.tk"
            library_text = (
                "/// Increment a value.\n"
                "pub fn bump(x: i32) -> i32 { return x + 1 }\n"
            )
            library.write_text(library_text, encoding="utf-8")
            library_uri = library.as_uri()
            cross_lines = [
                "import ./lib::{bump}",
                "",
                "fn main() -> i32 {",
                "    assert(true, \"😀\"); return bump(1) - 2",
                "}",
            ]
            cross_text = "\n".join(cross_lines) + "\n"
            client.notification(
                "textDocument/didChange",
                {
                    "textDocument": {"uri": uri, "version": 3},
                    "contentChanges": [{"text": cross_text}],
                },
            )
            require(client.diagnostics(uri) == [],
                    "cross-module document produced diagnostics")
            client.notification(
                "textDocument/didOpen",
                {
                    "textDocument": {
                        "uri": library_uri,
                        "languageId": "toka",
                        "version": 1,
                        "text": library_text,
                    }
                },
            )
            require(client.diagnostics(library_uri) == [],
                    "library overlay produced diagnostics")

            bump_use = cross_lines[3].index("bump")
            bump_utf16 = utf16_column(cross_lines[3], bump_use)
            definition = client.request(
                7,
                "textDocument/definition",
                text_document_position(uri, 3, bump_utf16 + 1),
            )["result"]
            require(definition["uri"] == library_uri and
                    definition["range"]["start"]["line"] == 1,
                    "semantic definition did not cross the module boundary")

            hover = client.request(
                8,
                "textDocument/hover",
                text_document_position(uri, 3, bump_utf16 + 1),
            )["result"]
            require("fn bump(x: i32) -> i32" in hover["contents"]["value"] and
                    "Increment a value." in hover["contents"]["value"],
                    "cross-module hover is missing signature or documentation")

            reference_params = text_document_position(uri, 3, bump_utf16 + 1)
            reference_params["context"] = {"includeDeclaration": True}
            references = client.request(
                9, "textDocument/references", reference_params
            )["result"]
            require(len(references) == 2 and
                    {item["uri"] for item in references} == {uri, library_uri},
                    "semantic references did not cross the module boundary")

            signature = client.request(
                10,
                "textDocument/signatureHelp",
                text_document_position(uri, 3, bump_utf16 + len("bump(")),
            )["result"]
            require(signature["activeParameter"] == 0 and
                    "bump(x: i32)" in signature["signatures"][0]["label"],
                    "signature help did not use the resolved callable")

            document_symbols = client.request(
                11, "textDocument/documentSymbol", {"textDocument": {"uri": uri}}
            )["result"]
            require("main" in {item["name"] for item in document_symbols},
                    "document symbols are incomplete")
            workspace_symbols = client.request(
                12, "workspace/symbol", {"query": "bump"}
            )["result"]
            require(any(item["name"] == "bump" for item in workspace_symbols),
                    "workspace symbols are incomplete")

            semantic_rename = text_document_position(uri, 3, bump_utf16 + 1)
            semantic_rename["newName"] = "increment"
            workspace_edit = client.request(
                13, "textDocument/rename", semantic_rename
            )["result"]
            require(set(workspace_edit["changes"]) == {uri, library_uri},
                    "semantic rename did not produce cross-module edits")

            changed_library = library_text.replace("x + 1", "x + 2")
            client.notification(
                "textDocument/didChange",
                {
                    "textDocument": {"uri": library_uri, "version": 2},
                    "contentChanges": [{"text": changed_library}],
                },
            )
            require(client.diagnostics(library_uri) == [],
                    "updated library overlay produced diagnostics")
            stats = client.request(14, "toka/analysisStats", {})["result"]
            require(stats["fresh"] and stats["recheckedModules"] == 2 and
                    stats["reusedModules"] > 0,
                    "reverse-dependency invalidation did not recheck exactly the library and root")

            unformatted = "import ./lib::{bump}\nfn main()->i32{return bump(1) - 3}\n"
            client.notification(
                "textDocument/didChange",
                {
                    "textDocument": {"uri": uri, "version": 4},
                    "contentChanges": [{"text": unformatted}],
                },
            )
            unformatted_diagnostics = client.diagnostics(uri)
            require(unformatted_diagnostics == [],
                    "unformatted valid document produced diagnostics: " +
                    json.dumps(unformatted_diagnostics, sort_keys=True))
            formatting = client.request(
                15, "textDocument/formatting",
                {"textDocument": {"uri": uri}, "options": {"tabSize": 4}},
            )["result"]
            require(len(formatting) == 1 and "fn main() -> i32" in formatting[0]["newText"],
                    "document formatting did not return a full-document edit")

            unknown = client.request(16, "toka/notARealMethod", {})
            require(unknown["error"]["code"] == -32601,
                    "unknown request did not return Method not found")

            shutdown = client.request(17, "shutdown")
            require(shutdown.get("result") is None, "shutdown result must be null")
            client.notification("exit")
            client.process.wait(timeout=5)
            require(client.process.returncode == 0, "tokalsp did not exit cleanly")
        except Exception as error:
            if client.process.poll() is None:
                client.fail_with_stderr(str(error))
            stderr = client.process.stderr.read().decode("utf-8", errors="replace")
            raise RuntimeError(str(error) + "\n" + stderr) from error

    print(json.dumps({
        "checks": [
            "initialize", "diagnostics", "full-sync", "definition", "hover",
            "references", "completion", "rename", "cross-module-definition",
            "semantic-hover", "cross-module-references", "utf16-positions",
            "signature-help",
            "document-symbols", "workspace-symbols", "cross-module-rename",
            "incremental-invalidation", "formatting", "method-errors",
            "shutdown-exit",
        ],
        "count": 20,
        "result": "pass",
        "schema": "toka.lsp-protocol",
        "version": 1,
    }, sort_keys=True, separators=(",", ":")))


if __name__ == "__main__":
    main()
