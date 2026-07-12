#!/usr/bin/env python3
import argparse
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor, as_completed


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
TOKAC = os.path.abspath(os.environ.get(
    "TOKAC", os.path.join(ROOT, "build/bin/tokac")))
LLVM_OBJDUMP = os.environ.get("LLVM_OBJDUMP", "llvm-objdump")
FIXTURE = os.path.join(
    ROOT, "tests/semantics/memory_summary/source_summary.tk")
BENCHMARK = os.path.join(
    ROOT, "tests/semantics/memory_summary/nocapture_benefit_benchmark.tk")
FLAG = "--experimental-memory-contracts=nocapture"
LEVELS = ("O1", "O2", "O3", "Os", "Oz")


def run(command):
    result = subprocess.run(command, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        raise RuntimeError("command failed: %s\n%s" %
                           (" ".join(command), result.stderr))
    return result


def compile_ir(source, level, experimental, output):
    command = [TOKAC, "-" + level]
    if experimental:
        command.append(FLAG)
    command.extend(["--emit-llvm", source, "-o", output])
    run(command)


def compile_object(source, level, experimental, output):
    command = [TOKAC, "-" + level]
    if experimental:
        command.append(FLAG)
    command.extend(["-c", source, "-o", output])
    run(command)


def compile_executable(source, level, experimental, output):
    command = [TOKAC, "-" + level]
    if experimental:
        command.append(FLAG)
    command.extend([source, "-o", output])
    run(command)


def signature(ir, function):
    match = re.search(r"^define .*@" + re.escape(function) +
                      r"\([^\n]*$", ir, re.MULTILINE)
    if not match:
        raise RuntimeError("missing IR definition for " + function)
    return match.group(0)


def prove_preoptimization_emission(work):
    default_path = os.path.join(work, "preopt-default.ll")
    experimental_path = os.path.join(work, "preopt-experimental.ll")
    compile_ir(FIXTURE, "O0", False, default_path)
    compile_ir(FIXTURE, "O0", True, experimental_path)
    with open(default_path, encoding="utf-8") as stream:
        default_ir = stream.read()
    with open(experimental_path, encoding="utf-8") as stream:
        experimental_ir = stream.read()
    functions = ("ms_read", "ms_write", "ms_generic_M_SummaryData")
    default_active = any(
        " nocapture " in signature(default_ir, function)
        for function in functions)
    experimental_active = all(
        " nocapture " in signature(experimental_ir, function)
        for function in functions)
    if default_active or not experimental_active:
        raise RuntimeError("experimental nocapture activation proof failed")
    triple = re.search(r'^target triple = "([^"]+)"$', default_ir,
                       re.MULTILINE)
    if not triple:
        raise RuntimeError("target triple not found in audit IR")
    return {
        "default_nocapture": default_active,
        "experimental_nocapture": experimental_active,
        "fixture": os.path.relpath(FIXTURE, ROOT),
        "functions": list(functions),
        "target_triple": triple.group(1),
    }


def normalized_disassembly(path):
    result = run([
        LLVM_OBJDUMP, "--disassemble", "--no-leading-addr",
        "--no-show-raw-insn", path,
    ])
    lines = []
    for line in result.stdout.splitlines():
        if "file format" in line:
            continue
        lines.append(line.rstrip())
    return "\n".join(lines).strip()


def compare_case(index, source, level, work):
    stem = "%05d-%s" % (index, level)
    default_ir = os.path.join(work, stem + "-default.ll")
    experimental_ir = os.path.join(work, stem + "-experimental.ll")
    compile_ir(source, level, False, default_ir)
    compile_ir(source, level, True, experimental_ir)
    with open(default_ir, "rb") as stream:
        default_bytes = stream.read()
    with open(experimental_ir, "rb") as stream:
        experimental_bytes = stream.read()
    relative_source = os.path.relpath(source, ROOT)
    if default_bytes == experimental_bytes:
        return relative_source, level, True, None

    default_object = os.path.join(work, stem + "-default.o")
    experimental_object = os.path.join(work, stem + "-experimental.o")
    compile_object(source, level, False, default_object)
    compile_object(source, level, True, experimental_object)
    machine_equal = (normalized_disassembly(default_object) ==
                     normalized_disassembly(experimental_object))
    machine = {
        "default_object_size": os.path.getsize(default_object),
        "experimental_object_size": os.path.getsize(experimental_object),
        "machine_code_identical": machine_equal,
        "source": relative_source,
    }
    return relative_source, level, False, machine


def sources_for_scope(full):
    if not full:
        return [FIXTURE]
    sources = []
    pass_root = os.path.join(ROOT, "tests/pass")
    for directory, _, files in os.walk(pass_root):
        for name in files:
            if name.endswith(".tk"):
                sources.append(os.path.join(directory, name))
    return sorted(sources)


def coroutine_frame_size(ir):
    function = re.search(
        r"define [^\n]*ws_accept_async\([^\n]*\).*?\n}", ir,
        re.DOTALL)
    if not function:
        raise RuntimeError("ws_accept_async coroutine wrapper not found")
    allocation = re.search(r"malloc\(i64 ([0-9]+)\)", function.group(0))
    if not allocation:
        raise RuntimeError("ws_accept_async frame allocation not found")
    return int(allocation.group(1))


def timed_run(path):
    started = time.perf_counter_ns()
    result = run([path])
    elapsed = time.perf_counter_ns() - started
    if result.stdout or result.stderr:
        raise RuntimeError("benchmark produced unexpected output")
    return elapsed


def benchmark_machine_delta(work, iterations):
    default_ir_path = os.path.join(work, "benchmark-default.ll")
    experimental_ir_path = os.path.join(work, "benchmark-experimental.ll")
    default_executable = os.path.join(work, "benchmark-default")
    experimental_executable = os.path.join(work, "benchmark-experimental")
    compile_ir(BENCHMARK, "O2", False, default_ir_path)
    compile_ir(BENCHMARK, "O2", True, experimental_ir_path)
    compile_executable(BENCHMARK, "O2", False, default_executable)
    compile_executable(BENCHMARK, "O2", True, experimental_executable)
    with open(default_ir_path, encoding="utf-8") as stream:
        default_frame = coroutine_frame_size(stream.read())
    with open(experimental_ir_path, encoding="utf-8") as stream:
        experimental_frame = coroutine_frame_size(stream.read())
    if experimental_frame >= default_frame:
        raise RuntimeError("targeted benchmark did not reduce coroutine frame")

    timed_run(default_executable)
    timed_run(experimental_executable)
    samples = {"default": [], "experimental": []}
    binaries = (("default", default_executable),
                ("experimental", experimental_executable))
    for index in range(iterations):
        order = binaries if index % 2 == 0 else tuple(reversed(binaries))
        for name, path in order:
            samples[name].append(timed_run(path))
    default_median = int(statistics.median(samples["default"]))
    experimental_median = int(statistics.median(samples["experimental"]))
    relative_change = ((experimental_median / default_median) - 1.0) * 100.0
    threshold = -2.0
    stable_improvement = relative_change <= threshold
    return {
        "default_frame_bytes": default_frame,
        "default_median_ns": default_median,
        "experimental_frame_bytes": experimental_frame,
        "experimental_median_ns": experimental_median,
        "fixture": os.path.relpath(BENCHMARK, ROOT),
        "iterations": iterations,
        "optimization_level": "O2",
        "relative_change_percent": round(relative_change, 3),
        "stable_improvement": stable_improvement,
        "stable_improvement_threshold_percent": threshold,
    }


def audit(full, work):
    sources = sources_for_scope(full)
    comparisons = []
    workers = int(os.environ.get("CORES", min(os.cpu_count() or 4, 8)))
    with ThreadPoolExecutor(max_workers=workers) as executor:
        futures = []
        for index, source in enumerate(sources):
            for level in LEVELS:
                futures.append(executor.submit(
                    compare_case, index, source, level, work))
        for future in as_completed(futures):
            comparisons.append(future.result())

    results = []
    machine_differences = []
    ir_difference_count = 0
    for level in LEVELS:
        level_results = sorted(
            (entry for entry in comparisons if entry[1] == level),
            key=lambda entry: entry[0])
        ir_different = [
            source for source, _, identical, _ in level_results
            if not identical]
        machine_checked = [
            machine for _, _, _, machine in level_results
            if machine is not None]
        level_machine_differences = [
            entry for entry in machine_checked
            if not entry["machine_code_identical"]]
        ir_difference_count += len(ir_different)
        machine_differences.extend(
            dict(entry, optimization_level=level)
            for entry in level_machine_differences)
        results.append({
            "ir_different": ir_different,
            "ir_identical_count": len(level_results) - len(ir_different),
            "machine_code_checked": machine_checked,
            "optimization_level": level,
        })

    if machine_differences:
        decision = "BenchmarkRequired"
        reason = "MachineCodeDeltaRequiresTargetedBenchmark"
    elif ir_difference_count:
        decision = "KeepExperimental"
        reason = "NoMachineCodeDelta"
    else:
        decision = "KeepExperimental"
        reason = "NoOptimizedIRDelta"
    return {
        "case_count": len(sources),
        "decision": decision,
        "optimization_levels": list(LEVELS),
        "preoptimization": prove_preoptimization_emission(work),
        "reason": reason,
        "results": results,
        "schema": "toka.nocapture-benefit-audit",
        "scope": "full" if full else "focused",
        "version": 1,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--full", action="store_true")
    parser.add_argument("--benchmark", action="store_true")
    parser.add_argument("--benchmark-iterations", type=int, default=7)
    parser.add_argument("--output")
    arguments = parser.parse_args()
    if arguments.benchmark and not arguments.full:
        raise RuntimeError("--benchmark requires --full")
    if arguments.benchmark_iterations < 3:
        raise RuntimeError("--benchmark-iterations must be at least 3")
    if not os.path.isfile(TOKAC):
        raise RuntimeError("tokac not found: " + TOKAC)
    if shutil.which(LLVM_OBJDUMP) is None:
        raise RuntimeError("llvm-objdump not found: " + LLVM_OBJDUMP)
    with tempfile.TemporaryDirectory(
            prefix="toka_nocapture_audit_") as work:
        document = audit(arguments.full, work)
        if arguments.benchmark and document["decision"] == "BenchmarkRequired":
            measurement = benchmark_machine_delta(
                work, arguments.benchmark_iterations)
            document["runtime_benchmark"] = measurement
            document["decision"] = "KeepExperimental"
            if measurement["stable_improvement"]:
                document["reason"] = "StableBenefitRequiresSeparateDefaultAudit"
            else:
                document["reason"] = "NoStableRuntimeBenefit"
    encoded = json.dumps(document, indent=2, sort_keys=True) + "\n"
    if arguments.output:
        with open(arguments.output, "w", encoding="utf-8") as stream:
            stream.write(encoded)
    else:
        sys.stdout.write(encoded)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print("Nocapture benefit audit FAILED: %s" % error, file=sys.stderr)
        sys.exit(1)
