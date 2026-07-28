#!/usr/bin/env python3

"""Emit a read-only, deterministic semantic diff for two Toka snapshots.

This is deliberately a short-lived preview: it starts fresh compiler processes
for the base and candidate source files and never creates an overlay, object,
interface, or workspace file.  A later persistent overlay session may reuse
the same output contract without changing this command's semantics.
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = "toka.semantic-diff-preview"
VERSION = 1


def canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def snapshot_digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def normalized_file(path, root):
    if not path:
        return ""
    candidate = Path(path).resolve()
    if candidate == root:
        return "$root"
    try:
        return candidate.relative_to(ROOT).as_posix()
    except ValueError:
        return "$external"


def normalized_location(location, root):
    result = json.loads(canonical_json(location))
    result["file"] = normalized_file(result.get("file", ""), root)
    return result


def run_compiler(tokac, source, mode):
    result = subprocess.run(
        [str(tokac), mode, "--check-only", str(source)], cwd=ROOT,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    document = None
    try:
        document = json.loads(result.stdout) if result.stdout else None
    except json.JSONDecodeError:
        pass
    return {"exit_code": result.returncode, "document": document}


def document_matches(document, schema, version):
    return isinstance(document, dict) and document.get("schema") == schema and \
        document.get("version") == version


def analyze_snapshot(tokac, source):
    diagnostics = run_compiler(tokac, source, "--diagnostics-json")
    index = run_compiler(tokac, source, "--semantic-index=json")
    evidence = run_compiler(tokac, source, "--semantic-evidence=json")
    return {
        "root": source,
        "digest": snapshot_digest(source),
        "diagnostics": diagnostics,
        "index": index,
        "evidence": evidence,
    }


def diagnostic_records(analysis):
    document = analysis["diagnostics"]["document"]
    root = analysis["root"]
    if not document_matches(document, "toka.diagnostics", 2):
        return []
    records = []
    for item in document.get("diagnostics", []):
        primary = item.get("primary", {})
        if Path(primary.get("file", "")).resolve() != root:
            continue
        record = {
            "code": item.get("code", ""),
            "message": item.get("message", ""),
            "primary": normalized_location(primary, root),
            "related": [normalized_location(related, root)
                        for related in item.get("related", [])],
            "severity": item.get("severity", ""),
        }
        records.append(record)
    return sorted(records, key=canonical_json)


def symbol_keys(index):
    symbols = {item["id"]: item for item in index.get("symbols", [])}
    cache = {}

    def key(symbol_id):
        if symbol_id in cache:
            return cache[symbol_id]
        symbol = symbols.get(symbol_id)
        if symbol is None:
            return ""
        parent = key(symbol.get("container", ""))
        current = "%s:%s" % (symbol.get("kind", ""), symbol.get("name", ""))
        cache[symbol_id] = current if not parent else parent + "/" + current
        return cache[symbol_id]

    return {symbol_id: key(symbol_id) for symbol_id in symbols}


def public_surface(analysis):
    document = analysis["index"]["document"]
    root = analysis["root"]
    if not document_matches(document, "toka.semantic-index", 1):
        return {}
    keys = symbol_keys(document)
    result = {}
    for symbol in document.get("symbols", []):
        declaration = symbol.get("declaration", {})
        if not symbol.get("public") or Path(declaration.get("file", "")).resolve() != root:
            continue
        identity = keys[symbol["id"]]
        result[identity] = {
            "symbol": identity,
            "kind": symbol.get("kind", ""),
            "name": symbol.get("name", ""),
            "container": keys.get(symbol.get("container", ""), ""),
            "detail": symbol.get("detail", ""),
            "type": symbol.get("type", ""),
            "contract": symbol.get("contract"),
        }
    return result


def capability_projection(contract):
    if not contract:
        return None
    fields = ("morphology", "flow", "payloadWritable", "payloadBlocked",
              "handleRebindable", "handleBlocked", "handleNullable",
              "payloadNullable")
    result = {"kind": contract.get("kind", "")}
    if contract.get("kind") == "callable":
        result["parameters"] = [
            {key: parameter.get(key) for key in ("name", "type") + fields}
            for parameter in contract.get("parameters", [])
        ]
        result["return_dependencies"] = contract.get("return", {}).get(
            "dependencies", [])
    else:
        result.update({key: contract.get(key) for key in fields})
    return result


def unsafe_entries(surface):
    entries = []
    for identity, symbol in surface.items():
        contract = symbol.get("contract") or {}
        candidates = []
        if contract.get("kind") == "callable":
            candidates = [("parameters[%d]" % index, parameter)
                          for index, parameter in enumerate(
                              contract.get("parameters", []))]
        elif contract:
            candidates = [("field", contract)]
        for path, candidate in candidates:
            if candidate.get("morphology") == "raw" or \
                    candidate.get("flow") == "unsafe-raw":
                entries.append({"symbol": identity, "path": path,
                                "type": candidate.get("type", "")})
    return sorted(entries, key=canonical_json)


def evidence_records(analysis):
    document = analysis["evidence"]["document"]
    root = analysis["root"]
    if not document_matches(document, "toka.semantic-evidence", 1):
        return []
    records = []
    for item in document.get("records", []):
        primary = item.get("primary_location", {})
        origin = item.get("origin_location", {})
        primary_file = Path(primary.get("file", "")).resolve()
        origin_file = Path(origin.get("file", "")).resolve()
        if primary_file != root and origin_file != root:
            continue
        record = json.loads(canonical_json(item))
        record["primary_location"] = normalized_location(primary, root)
        record["origin_location"] = normalized_location(origin, root)
        records.append(record)
    return sorted(records, key=canonical_json)


def split_diff(base, candidate):
    base_keys = set(base)
    candidate_keys = set(candidate)
    return {
        "added": [candidate[key] for key in sorted(candidate_keys - base_keys)],
        "removed": [base[key] for key in sorted(base_keys - candidate_keys)],
        "changed": [
            {"symbol": key, "before": base[key], "after": candidate[key]}
            for key in sorted(base_keys & candidate_keys)
            if base[key] != candidate[key]
        ],
    }


def list_diff(base, candidate):
    base_items = {canonical_json(item): item for item in base}
    candidate_items = {canonical_json(item): item for item in candidate}
    return {
        "added": [candidate_items[key] for key in
                  sorted(candidate_items.keys() - base_items.keys())],
        "removed": [base_items[key] for key in
                    sorted(base_items.keys() - candidate_items.keys())],
    }


def capability_diff(base, candidate):
    keys = set(base) | set(candidate)
    result = {"added": [], "removed": [], "changed": []}
    for key in sorted(keys):
        before = capability_projection(base[key]["contract"]) if key in base else None
        after = capability_projection(candidate[key]["contract"]) if key in candidate else None
        if before == after:
            continue
        item = {"symbol": key, "before": before, "after": after}
        if before is None:
            result["added"].append(item)
        elif after is None:
            result["removed"].append(item)
        else:
            result["changed"].append(item)
    return result


def analysis_status(analysis):
    return {
        "check": {
            "available": document_matches(analysis["diagnostics"]["document"],
                                            "toka.diagnostics", 2),
            "exit_code": analysis["diagnostics"]["exit_code"],
            "success": bool(analysis["diagnostics"]["document"] and
                            analysis["diagnostics"]["document"].get("success")),
        },
        "evidence": {
            "available": document_matches(analysis["evidence"]["document"],
                                            "toka.semantic-evidence", 1),
            "exit_code": analysis["evidence"]["exit_code"],
        },
        "index": {
            "available": document_matches(analysis["index"]["document"],
                                            "toka.semantic-index", 1),
            "exit_code": analysis["index"]["exit_code"],
        },
    }


def main():
    parser = argparse.ArgumentParser(
        description="Emit a read-only semantic preview for base and candidate Toka sources.")
    parser.add_argument("--base", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--tokac", type=Path)
    args = parser.parse_args()

    suffix = ".exe" if sys.platform == "win32" else ""
    tokac = args.tokac or ROOT / args.build_dir / "bin" / ("tokac" + suffix)
    tokac = tokac.resolve()
    base = args.base.resolve()
    candidate = args.candidate.resolve()
    require(tokac.is_file(), "tokac binary is missing: %s" % tokac)
    require(base.is_file(), "base source is missing: %s" % base)
    require(candidate.is_file(), "candidate source is missing: %s" % candidate)

    base_analysis = analyze_snapshot(tokac, base)
    candidate_analysis = analyze_snapshot(tokac, candidate)
    base_surface = public_surface(base_analysis)
    candidate_surface = public_surface(candidate_analysis)
    public_api = split_diff(base_surface, candidate_surface)
    diagnostics = list_diff(diagnostic_records(base_analysis),
                            diagnostic_records(candidate_analysis))
    evidence = list_diff(evidence_records(base_analysis),
                         evidence_records(candidate_analysis))
    capabilities = capability_diff(base_surface, candidate_surface)
    unsafe = list_diff(unsafe_entries(base_surface), unsafe_entries(candidate_surface))
    changed_count = sum(len(section.get(key, []))
                        for section in (diagnostics, public_api, capabilities,
                                        unsafe, evidence)
                        for key in ("added", "removed", "changed"))
    result = {
        "schema": SCHEMA,
        "version": VERSION,
        "inputs": {
            "base": {"path": str(base), "sha256": base_analysis["digest"]},
            "candidate": {"path": str(candidate),
                          "sha256": candidate_analysis["digest"]},
        },
        "analyses": {"base": analysis_status(base_analysis),
                     "candidate": analysis_status(candidate_analysis)},
        "diagnostics": diagnostics,
        "public_api": public_api,
        "capabilities": capabilities,
        "unsafe_surface": unsafe,
        "evidence": evidence,
        "summary": {"changed_records": changed_count,
                    "has_changes": changed_count != 0,
                    "read_only": True},
    }
    print(canonical_json(result))


if __name__ == "__main__":
    try:
        main()
    except RuntimeError as error:
        print("semantic diff preview: %s" % error, file=sys.stderr)
        sys.exit(2)
