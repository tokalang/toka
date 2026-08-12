#!/usr/bin/env python3

"""Keep the AI evaluation's byte budget independent of host checkout paths."""

import importlib.util
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
    baseline = json.loads((ROOT / "tests/tooling/ai_eval/baseline.json").read_text(encoding="utf-8"))
    require(baseline.get("version") == 2 and
            baseline.get("cost_metric") == "normalized-json-output-bytes-v1",
            "baseline must name the normalized output-byte metric")
    print("AI coding evaluation path-normalization contract PASSED")


if __name__ == "__main__":
    main()
