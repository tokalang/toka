#!/usr/bin/env python3

"""ABI and read-only gate for Ephemeral Semantic Diff Preview v1."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command):
    return subprocess.run([str(item) for item in command], cwd=ROOT, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def worktree_status():
    result = run(["git", "status", "--porcelain"])
    require(result.returncode == 0, "cannot inspect worktree status")
    return result.stdout


def preview(script, build_dir, base, candidate):
    result = run([sys.executable, script, "--build-dir", build_dir,
                  "--base", base, "--candidate", candidate])
    if result.returncode != 0:
        raise RuntimeError("preview failed:\n%s%s" % (result.stdout, result.stderr))
    return result.stdout, json.loads(result.stdout)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    script = ROOT / "tools/scripts/semantic_diff_preview.py"
    tokac = ROOT / args.build_dir / "bin" / (
        "tokac.exe" if sys.platform == "win32" else "tokac")
    toka = ROOT / args.build_dir / "bin" / (
        "toka.exe" if sys.platform == "win32" else "toka")
    schema = ROOT / "schemas/toka.semantic-diff-preview.v1.schema.json"
    require(script.is_file() and tokac.is_file() and toka.is_file() and schema.is_file(),
            "semantic diff preview prerequisites are missing")
    schema_doc = json.loads(schema.read_text(encoding="utf-8"))
    require(schema_doc["properties"]["schema"] == {"const": "toka.semantic-diff-preview"},
            "preview schema does not freeze its identity")
    require(schema_doc["properties"]["version"] == {"const": 1},
            "preview schema does not freeze v1")

    with tempfile.TemporaryDirectory(prefix="toka-semantic-preview-") as temp:
        root = Path(temp)
        base_dir = root / "base"
        candidate_dir = root / "candidate"
        base_dir.mkdir()
        candidate_dir.mkdir()
        base = base_dir / "main.tk"
        candidate = candidate_dir / "main.tk"
        base.write_text(
            "pub fn mutate(value: i32) -> i32 {\n"
            "    return value\n"
            "}\n\n"
            "fn main() -> i32 {\n"
            "    return mutate(1)\n"
            "}\n", encoding="utf-8")
        candidate.write_text(
            "pub fn mutate(value#: i32) -> i32 {\n"
            "    value = value + 1\n"
            "    return value\n"
            "}\n\n"
            "fn main() -> i32 {\n"
            "    auto item# = 1\n"
            "    return mutate(item)\n"
            "}\n", encoding="utf-8")
        before = {path: digest(path) for path in (base, candidate)}
        before_worktree = worktree_status()
        first, payload = preview(script, args.build_dir, base, candidate)
        second, repeated = preview(script, args.build_dir, base, candidate)
        require(first == second and payload == repeated,
                "preview output is not deterministic")
        require(payload["schema"] == "toka.semantic-diff-preview" and
                payload["version"] == 1, "preview envelope changed")
        require(set(payload) == {"schema", "version", "inputs", "analyses",
                                 "diagnostics", "public_api", "capabilities",
                                 "unsafe_surface", "evidence", "summary"},
                "preview envelope fields changed")
        require(set(payload["analyses"]["base"]) == {"check", "evidence", "index"},
                "preview analysis status fields changed")
        require(payload["summary"]["read_only"], "preview does not promise read-only execution")
        require({path: digest(path) for path in (base, candidate)} == before,
                "preview modified an input snapshot")
        require(worktree_status() == before_worktree,
                "preview modified the workspace")
        changed = payload["public_api"]["changed"]
        require(any(item["symbol"] == "function:mutate" for item in changed),
                "preview omitted public callable contract change")
        capabilities = payload["capabilities"]["changed"]
        mutate = next((item for item in capabilities
                       if item["symbol"] == "function:mutate"), None)
        require(mutate is not None and
                not mutate["before"]["parameters"][0]["payloadWritable"] and
                mutate["after"]["parameters"][0]["payloadWritable"],
                "preview omitted H/P capability delta")
        manager = run([toka, "preview", "--base", base, "--candidate", candidate])
        require(manager.returncode == 0 and manager.stdout == first,
                "toka preview does not preserve preview output")

        pass_case = ROOT / "tests/semantics/tki_replay/cases/pal_call_001_alias/pass_read_read.tk"
        fail_case = ROOT / "tests/semantics/tki_replay/cases/pal_call_001_alias/fail_mut_read_alias.tk"
        shutil.copytree(pass_case.parent, root / "pass-case")
        shutil.copytree(fail_case.parent, root / "fail-case")
        error_base = root / "pass-case/pass_read_read.tk"
        error_candidate = root / "fail-case/fail_mut_read_alias.tk"
        _, error_payload = preview(script, args.build_dir, error_base,
                                   error_candidate)
        require(error_payload["analyses"]["candidate"]["check"]["available"] and
                not error_payload["analyses"]["candidate"]["check"]["success"],
                "preview did not retain candidate diagnostics")
        require(any(item["code"] == "E0475"
                    for item in error_payload["diagnostics"]["added"]),
                "preview omitted introduced diagnostic")
        require(any(item["rule"] == "PAL-CALL-001" and
                    item["decision"] == "Reject"
                    for item in error_payload["evidence"]["added"]),
                "preview omitted introduced semantic evidence")

    print("Ephemeral Semantic Diff Preview v1 ABI gate PASSED")


if __name__ == "__main__":
    main()
