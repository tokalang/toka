#!/usr/bin/env python3

"""Keep the AI evaluation's byte budget independent of host checkout paths."""

import importlib.util
import copy
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
EVALUATION = ROOT / "tools/scripts/evaluate_ai_coding.py"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def load_evaluation_module():
    spec = importlib.util.spec_from_file_location("toka_ai_coding_evaluation", EVALUATION)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    evaluation = load_evaluation_module()
    payload_a = '{"file":"/short/repo/tests/fail/example.tk"}\n'
    payload_b = '{"file":"/very/long/host/dependent/checkout/repo/tests/fail/example.tk"}\n'
    normalized_a = evaluation.normalize_machine_output(
        payload_a, {"/short/repo": "<repo>"})
    normalized_b = evaluation.normalize_machine_output(
        payload_b, {"/very/long/host/dependent/checkout/repo": "<repo>"})
    require(normalized_a == normalized_b,
            "path normalization must make equivalent JSON payloads cost-identical")
    require("/short/repo" not in normalized_a and "<repo>" in normalized_a,
            "path normalization must remove the host checkout path")
    source = ROOT / "tests/fail/borrow_move.tk"
    view = {"schema":"toka.semantic-evidence-view", "version":1, "result":"failed",
            "exit_code":1, "errors":[], "compiler":{"exit_code":1},
            "analysis":{"scope":"full", "result":"failed", "exit_code":1,
                        "records_total":1, "records_emitted":1},
            "records":[{"rule":"PAL-BORROW-002", "decision":"Reject",
                        "reason":"ActiveSharedBorrow",
                        "origin_location":{"file":str(source.resolve())},
                        "source":{"origin":"unknown"}}]}
    require(evaluation.evidence_rejection_success(view, source),
            "current evidence-view rejection was not accepted")
    invalid_views = []
    for key, value in (("schema", "toka.semantic-evidence"), ("result", "configuration_error"),
                       ("exit_code", 2), ("compiler", None), ("records", [])):
        invalid = copy.deepcopy(view); invalid[key] = value; invalid_views.append(invalid)
    for key, value in (("scope", "filtered"), ("result", "not_started"),
                       ("exit_code", None), ("records_total", 0)):
        invalid = copy.deepcopy(view); invalid["analysis"][key] = value; invalid_views.append(invalid)
    for key, value in (("rule", "unrelated"), ("decision", "Accept"),
                       ("reason", "unrelated"), ("origin_location", {}), ("source", {})):
        invalid = copy.deepcopy(view); invalid["records"][0][key] = value; invalid_views.append(invalid)
    for invalid in invalid_views:
        require(not evaluation.evidence_rejection_success(invalid, source),
                "incomplete or substituted manager evidence was accepted")
    baseline = json.loads((ROOT / "tests/tooling/ai_eval/baseline.json").read_text(encoding="utf-8"))
    require(baseline.get("version") == 2 and
            baseline.get("cost_metric") == "normalized-json-output-bytes-v1",
            "baseline must name the normalized output-byte metric")
    print("AI coding evaluation normalization and evidence consumer controls PASSED")


if __name__ == "__main__":
    main()
