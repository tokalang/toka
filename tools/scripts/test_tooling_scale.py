#!/usr/bin/env python3

"""Qualify persistent tooling on the checked-in 5K+ pilot project."""

import argparse
import json
import os
from pathlib import Path
import platform
import random
import shutil
import subprocess
import sys
import tempfile
import time

try:
    import resource
except ImportError:
    resource = None

from test_lsp_protocol import LspClient, require, text_document_position


def child_peak_megabytes():
    if resource is None:
        return 0.0
    peak = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    if platform.system() == "Darwin":
        return peak / (1024.0 * 1024.0)
    return peak / 1024.0


def percentile(values, fraction):
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1,
                       int(len(ordered) * fraction + 0.999999) - 1))
    return ordered[index]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    parser.add_argument(
        "--gates", default="tests/tooling/pilot_project/gates.json")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[2]
    build_dir = (root / args.build_dir).resolve()
    suffix = ".exe" if sys.platform == "win32" else ""
    tokac = build_dir / "bin" / ("tokac" + suffix)
    tokalsp = build_dir / "bin" / ("tokalsp" + suffix)
    gates = json.loads((root / args.gates).read_text(encoding="utf-8"))
    fixture = root / "tests/tooling/pilot_project/src"
    require(tokac.is_file() and tokalsp.is_file(),
            "Toka compiler or language server is missing")

    fixture_files = sorted(fixture.glob("*.tk"))
    source_lines = sum(len(path.read_text(encoding="utf-8").splitlines())
                       for path in fixture_files)
    require(len(fixture_files) >= gates["minimum_modules"],
            "pilot module count is below the gate")
    require(source_lines >= gates["minimum_lines"],
            "pilot line count is below the gate")

    env = os.environ.copy()
    env["PATH"] = str(build_dir / "bin") + os.pathsep + env.get("PATH", "")
    env["TOKA_LIB"] = str(root / "lib")
    soak_edits = gates["soak_edits"]
    rng = random.Random(gates["seed"])
    warm_round_trip_ms = []
    warm_analysis_ms = []
    peak_rss_mb = 0.0
    stale_results = 0

    with tempfile.TemporaryDirectory(prefix="toka-tooling-scale-") as temp:
        regenerated = Path(temp) / "regenerated"
        generated = subprocess.run(
            [sys.executable,
             str(root / "tests/tooling/pilot_project/generate.py"),
             str(regenerated)],
            cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        require(generated.returncode == 0,
                "pilot regeneration failed\n" + generated.stderr)
        regenerated_files = sorted(regenerated.glob("*.tk"))
        require([path.name for path in regenerated_files] ==
                [path.name for path in fixture_files] and
                all(path.read_text(encoding="utf-8") ==
                    (fixture / path.name).read_text(encoding="utf-8")
                    for path in regenerated_files),
                "checked-in pilot source differs from its generator")

        workspace = Path(temp) / "telemetry_pipeline"
        shutil.copytree(fixture, workspace)
        main_path = workspace / "main.tk"
        main_uri = main_path.as_uri()
        clean = subprocess.run(
            [str(tokac), "--check-only", str(main_path)], cwd=root, env=env,
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        require(clean.returncode == 0,
                "clean pilot check failed\n" + clean.stdout + clean.stderr)

        client = LspClient([tokalsp], root, env)
        try:
            initialized = client.request(
                "scale-initialize", "initialize",
                {"processId": None, "rootUri": workspace.as_uri(),
                 "capabilities": {}},
            )
            require("capabilities" in initialized["result"],
                    "pilot LSP initialization failed")
            client.notification("initialized", {})

            original = main_path.read_text(encoding="utf-8")
            cold_started = time.perf_counter()
            client.notification("textDocument/didOpen", {
                "textDocument": {"uri": main_uri, "languageId": "toka",
                                 "version": 1, "text": original},
            })
            require(client.diagnostics(main_uri) == [],
                    "pilot produced diagnostics during cold analysis")
            cold_round_trip_ms = (time.perf_counter() - cold_started) * 1000.0
            cold_stats = client.request(
                "scale-cold-stats", "toka/analysisStats", {})["result"]
            require(cold_stats["fresh"] and
                    cold_stats["totalModules"] >= len(fixture_files),
                    "cold analysis did not produce a complete semantic index")
            cold_analysis_ms = cold_stats["elapsedMs"]

            final_text = original
            for edit_index in range(soak_edits):
                token = rng.randrange(1_000_000_000)
                final_text = original.replace(
                    "// Reference entry point for the telemetry scoring pipeline.",
                    "// Reference entry point; soak token %09d." % token,
                    1,
                )
                started = time.perf_counter()
                client.notification("textDocument/didChange", {
                    "textDocument": {"uri": main_uri,
                                     "version": edit_index + 2},
                    "contentChanges": [{"text": final_text}],
                })
                diagnostics = client.diagnostics(main_uri)
                warm_round_trip_ms.append(
                    (time.perf_counter() - started) * 1000.0)
                if diagnostics:
                    stale_results += 1
                stats = client.request(
                    "scale-stats-%d" % edit_index,
                    "toka/analysisStats", {},
                )["result"]
                warm_analysis_ms.append(stats["elapsedMs"])
                expected_revision = edit_index + 2
                if (not stats["fresh"] or
                        stats["revision"] != expected_revision or
                        stats["recheckedModules"] != 1 or
                        str(main_path.resolve()) not in stats["invalidatedModules"]):
                    stale_results += 1

            changed_dependency = workspace / "stage_19.tk"
            dependency_text = changed_dependency.read_text(encoding="utf-8")
            changed_dependency.write_text(
                dependency_text.replace(
                    "return calibrate_19_00(upstream)",
                    "return calibrate_19_00(upstream) + 1"),
                encoding="utf-8",
            )
            final_text = final_text.replace("soak token", "external token", 1)
            client.notification("textDocument/didChange", {
                "textDocument": {"uri": main_uri,
                                 "version": soak_edits + 2},
                "contentChanges": [{"text": final_text}],
            })
            require(client.diagnostics(main_uri) == [],
                    "external dependency change produced stale diagnostics")
            external_stats = client.request(
                "scale-external-stats", "toka/analysisStats", {})["result"]
            require(external_stats["fresh"] and
                    external_stats["recheckedModules"] == 2 and
                    str(changed_dependency.resolve()) in
                    external_stats["invalidatedModules"],
                    "external dependency change did not trigger safe fallback")

            call_line = next(
                index for index, line in enumerate(final_text.splitlines())
                if "return stage_19" in line)
            call_column = final_text.splitlines()[call_line].index("stage_19")
            definition = client.request(
                "scale-definition", "textDocument/definition",
                text_document_position(main_uri, call_line, call_column + 1),
            )["result"]
            require(definition and definition["uri"].endswith("stage_19.tk"),
                    "semantic result was stale after the soak")

            main_path.write_text(final_text, encoding="utf-8")
            shutdown = client.request("scale-shutdown", "shutdown")
            require(shutdown.get("result") is None,
                    "pilot LSP shutdown returned a non-null result")
            client.notification("exit")
            client.process.wait(timeout=5)
            require(client.process.returncode == 0,
                    "language server crashed or exited unsuccessfully")
        except Exception as error:
            if client.process.poll() is None:
                client.fail_with_stderr(str(error))
            stderr = client.process.stderr.read().decode(
                "utf-8", errors="replace")
            raise RuntimeError(str(error) + "\n" + stderr) from error

        final_clean = subprocess.run(
            [str(tokac), "--check-only", str(main_path)], cwd=root, env=env,
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        require(final_clean.returncode == 0,
                "clean/incremental result disagreement\n" +
                final_clean.stdout + final_clean.stderr)
        peak_rss_mb = child_peak_megabytes()

    warm_p95_ms = percentile(warm_round_trip_ms, 0.95)
    warm_analysis_p95_ms = percentile(warm_analysis_ms, 0.95)
    require(stale_results == 0, "soak observed stale diagnostics or statistics")
    require(warm_p95_ms <= gates["maximum_warm_p95_ms"],
            "warm round-trip p95 %.3f ms (analysis %.3f ms) exceeds %.3f ms" %
            (warm_p95_ms, warm_analysis_p95_ms,
             gates["maximum_warm_p95_ms"]))
    require(peak_rss_mb <= gates["maximum_rss_mb"],
            "peak RSS %.3f MiB exceeds %.3f MiB" %
            (peak_rss_mb, gates["maximum_rss_mb"]))

    print(json.dumps({
        "schema": "toka.tooling-scale",
        "version": 1,
        "result": "pass",
        "fixture": {"modules": len(fixture_files), "lines": source_lines},
        "soak": {"edits": soak_edits, "seed": gates["seed"],
                 "staleResults": stale_results, "crashes": 0,
                 "externalInvalidation": True},
        "latencyMs": {
            "coldAnalysis": round(cold_analysis_ms, 3),
            "coldRoundTrip": round(cold_round_trip_ms, 3),
            "warmAnalysisP95": round(warm_analysis_p95_ms, 3),
            "warmRoundTripP95": round(warm_p95_ms, 3),
            "warmRoundTripMax": round(max(warm_round_trip_ms), 3),
        },
        "peakRssMiB": round(peak_rss_mb, 3),
        "machine": {"system": platform.system(),
                    "release": platform.release(),
                    "architecture": platform.machine()},
        "gates": args.gates,
    }, sort_keys=True, separators=(",", ":")))


if __name__ == "__main__":
    main()
