#!/usr/bin/env python3

"""Deterministic evaluation of Toka's machine-facing coding interfaces."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def apply_edit(source, edit):
    lines = source.splitlines(keepends=True)
    start = edit["range"]["start"]
    end = edit["range"]["end"]
    require(start["line"] == end["line"], "evaluation edits must be single-line")
    line = lines[start["line"]]
    lines[start["line"]] = (
        line[:start["character"]] + edit["newText"] +
        line[end["character"]:]
    )
    return "".join(lines)


def normalize_machine_output(output, path_aliases):
    """Remove host-specific absolute paths before enforcing payload budgets."""
    normalized = output
    for path, alias in sorted(path_aliases.items(), key=lambda item: len(item[0]), reverse=True):
        normalized = normalized.replace(path, alias)
    return normalized


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    parser.add_argument(
        "--baseline", default="tests/tooling/ai_eval/baseline.json")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[2]
    suffix = ".exe" if sys.platform == "win32" else ""
    tokac = (root / args.build_dir / "bin" / ("tokac" + suffix)).resolve()
    toka = (root / args.build_dir / "bin" / ("toka" + suffix)).resolve()
    baseline_path = (root / args.baseline).resolve()
    require(tokac.is_file() and toka.is_file(),
            "Toka SDK binaries are missing; build the SDK first")
    require(baseline_path.is_file(), "AI coding baseline is missing")
    baseline = json.loads(baseline_path.read_text(encoding="utf-8"))
    require(baseline.get("schema") == "toka.ai-coding-baseline" and
            baseline.get("version") == 2 and
            baseline.get("cost_metric") == "normalized-json-output-bytes-v1",
            "AI coding baseline must declare the normalized output-byte metric")

    cost = {"tool_calls": 0, "repair_rounds": 0,
            "input_bytes": 0, "normalized_output_bytes": 0}
    path_aliases = {str(root.resolve()): "<repo>"}

    def run(command, expected=0, source_bytes=0):
        result = subprocess.run(
            [str(part) for part in command], cwd=root, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        cost["tool_calls"] += 1
        cost["input_bytes"] += source_bytes
        normalized = normalize_machine_output(result.stdout, path_aliases)
        cost["normalized_output_bytes"] += len(normalized.encode("utf-8"))
        if result.returncode != expected:
            raise RuntimeError(
                "expected exit %d, got %d: %s\n%s%s" %
                (expected, result.returncode, " ".join(map(str, command)),
                 result.stdout, result.stderr)
            )
        return result

    clean = root / "tests/tooling/semantic_workspace/main.tk"
    clean_source = clean.read_text(encoding="utf-8")
    compile_successes = int(
        run([tokac, "--check-only", clean],
            source_bytes=len(clean_source.encode("utf-8"))).returncode == 0)

    repair_cases = [
        (root / "tests/tooling/diagnostics/deprecated_var.tk", "E01244"),
        (root / "tests/fail/kebab_subtraction_ambiguity.tk", "E01246"),
    ]
    diagnostic_successes = 0
    repair_successes = 0
    precise_edits = 0
    with tempfile.TemporaryDirectory(prefix="toka-ai-eval-") as temp:
        path_aliases[str(Path(temp).resolve())] = "<temporary>"
        for index, (source_path, expected_code) in enumerate(repair_cases):
            source = source_path.read_text(encoding="utf-8")
            document = json.loads(run(
                [tokac, "--diagnostics-json", "--check-only", source_path],
                expected=1, source_bytes=len(source.encode("utf-8")),
            ).stdout)
            matches = [item for item in document["diagnostics"]
                       if item["code"] == expected_code]
            diagnostic_successes += int(len(matches) == 1)
            require(len(matches) == 1 and len(matches[0]["fixes"]) == 1,
                    "expected one unambiguous fix for " + expected_code)
            edits = matches[0]["fixes"][0]["edits"]
            precise_edits += int(len(edits) == 1 and
                                 edits[0]["file"] == str(source_path.resolve()))
            repaired = apply_edit(source, edits[0])
            repaired_path = Path(temp) / ("repair_%d.tk" % index)
            repaired_path.write_text(repaired, encoding="utf-8")
            cost["repair_rounds"] += 1
            repair_successes += int(run(
                [tokac, "--check-only", repaired_path],
                source_bytes=len(repaired.encode("utf-8")),
            ).returncode == 0)

    ownership = root / "tests/fail/borrow_move.tk"
    ownership_source = ownership.read_text(encoding="utf-8")
    ownership_doc = json.loads(run(
        [tokac, "--diagnostics-json", "--check-only", ownership],
        expected=1, source_bytes=len(ownership_source.encode("utf-8")),
    ).stdout)
    move = next((item for item in ownership_doc["diagnostics"]
                 if item["code"] == "E0440"), None)
    diagnostic_successes += int(bool(move and move["related"]))

    evidence_doc = json.loads(run(
        [toka, "evidence", "--json", ownership],
        expected=1, source_bytes=len(ownership_source.encode("utf-8")),
    ).stdout)
    evidence_successes = int(
        evidence_doc.get("schema") == "toka.semantic-evidence" and
        evidence_doc.get("version") == 1 and
        any(record.get("rule") == "PAL-BORROW-002" and
            record.get("decision") == "Reject" and
            record.get("reason") == "ActiveSharedBorrow" and
            record.get("origin_location", {}).get("file")
            for record in evidence_doc.get("records", []))
    )

    context_result = json.loads(run([
        tokac, "--semantic-context=json", clean,
        "--query-file", clean, "--line", "13", "--character", "12",
    ], source_bytes=len(clean_source.encode("utf-8"))).stdout)
    context_successes = int(
        context_result["result"]["symbol"]["name"] == "identity" and
        len(context_result["result"]["visibleSymbols"]) <= 20)

    rates = {
        "compile_success": compile_successes / 1,
        "diagnostic_success": diagnostic_successes / 3,
        "repair_success": repair_successes / len(repair_cases),
        "edit_precision": precise_edits / len(repair_cases),
        "semantic_context_success": context_successes / 1,
        "semantic_evidence_success": evidence_successes / 1,
    }
    for name, minimum in baseline["minimum_rates"].items():
        require(rates[name] >= minimum,
                "%s regressed: %.3f < %.3f" % (name, rates[name], minimum))
    for name, maximum in baseline["maximum_cost"].items():
        require(cost[name] <= maximum,
                "%s regressed: %d > %d" % (name, cost[name], maximum))

    print(json.dumps({
        "schema": "toka.ai-coding-evaluation",
        "version": 2,
        "tasks": 6,
        "rates": rates,
        "cost": cost,
        "baseline": str(baseline_path.relative_to(root)),
        "result": "pass",
    }, sort_keys=True, separators=(",", ":")))


if __name__ == "__main__":
    main()
