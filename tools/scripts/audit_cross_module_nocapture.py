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


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
TOKAC = os.path.abspath(os.environ.get(
    "TOKAC", os.path.join(ROOT, "build/bin/tokac")))
LLVM_OBJDUMP = os.environ.get("LLVM_OBJDUMP", "llvm-objdump")
CASE = os.path.join(
    ROOT, "tests/semantics/memory_summary/cross_module_nocapture")
PROVIDER = os.path.join(CASE, "provider.tk")
CONSUMER = os.path.join(CASE, "consumer.tk")
FLAG = "--experimental-memory-contracts=nocapture"
CONTRACT = "nocapture"
SCHEMA = "toka.cross-module-nocapture-audit"
LEVELS = ("O1", "O2", "O3", "Os", "Oz")


def run(command, env=None):
    result = subprocess.run(command, cwd=ROOT, env=env,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        raise RuntimeError("command failed: %s\n%s" %
                           (" ".join(command), result.stderr))
    return result


def fnv1a(value):
    result = 14695981039346656037
    for byte in value.encode():
        result ^= byte
        result = (result * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % result


def cache_environment(build_dir):
    result = os.environ.copy()
    result["TOKA_BUILD_DIR"] = build_dir
    result["TOKA_USE_LIB_CACHE"] = "1"
    return result


def prepare_provider(work):
    build_dir = os.path.join(work, "build")
    os.makedirs(os.path.join(build_dir, "objects"))
    os.makedirs(os.path.join(build_dir, "interfaces"))
    stem = fnv1a(os.path.realpath(PROVIDER))
    object_path = os.path.join(build_dir, "objects", stem + ".o")
    env = cache_environment(build_dir)
    run([TOKAC, "-O2", "-c", PROVIDER, "-o", object_path], env)
    return build_dir, object_path, env


def compile_consumer(level, experimental, output, object_path, env,
                     emit_llvm=False, compile_only=False):
    command = [TOKAC, "-" + level]
    if experimental:
        command.append(FLAG)
    if emit_llvm:
        command.append("--emit-llvm")
    elif compile_only:
        command.append("-c")
    command.extend([CONSUMER, object_path, "-o", output])
    run(command, env)


def normalized_disassembly(path):
    result = run([
        LLVM_OBJDUMP, "--disassemble", "--no-leading-addr",
        "--no-show-raw-insn", path,
    ])
    return "\n".join(
        line.rstrip() for line in result.stdout.splitlines()
        if "file format" not in line).strip()


def contract(document, suffix):
    matches = [entry for entry in document["records"]
               if entry["function"].endswith(suffix) and
               entry["contract"] == CONTRACT]
    if len(matches) != 1:
        raise RuntimeError("expected one contract for " + suffix)
    return matches[0]


def prove_activation(work, object_path, env):
    default = run([
        TOKAC, "--dump-memory-contracts=json", "-c", CONSUMER,
        object_path, "-o", os.path.join(work, "default-contract.o")], env)
    experimental = run([
        TOKAC, FLAG, "--dump-memory-contracts=json", "-c", CONSUMER,
        object_path, "-o", os.path.join(work, "experimental-contract.o")],
        env)
    default_record = contract(json.loads(default.stdout), "read_payload")
    experimental_record = contract(
        json.loads(experimental.stdout), "read_payload")
    if default_record["emitted"] or \
            default_record["reason"] != "SignatureOnly":
        raise RuntimeError("default compilation consumed trusted evidence")
    if experimental_record["decision"] != "Candidate" or \
            experimental_record["reason"] != "ProvenByTrustedCache" or \
            not experimental_record["emitted"]:
        raise RuntimeError("experimental cache contract was not emitted")
    return {
        "default_emitted": default_record["emitted"],
        "experimental_emitted": experimental_record["emitted"],
        "experimental_reason": experimental_record["reason"],
        "function": "read_payload",
    }


def static_audit(work, object_path, env):
    results = []
    machine_delta_count = 0
    target_triple = None
    for level in LEVELS:
        default_ir = os.path.join(work, level + "-default.ll")
        experimental_ir = os.path.join(work, level + "-experimental.ll")
        compile_consumer(
            level, False, default_ir, object_path, env, emit_llvm=True)
        compile_consumer(
            level, True, experimental_ir, object_path, env, emit_llvm=True)
        with open(default_ir, encoding="utf-8") as stream:
            default_text = stream.read()
        with open(experimental_ir, encoding="utf-8") as stream:
            experimental_text = stream.read()
        if target_triple is None:
            match = re.search(r'^target triple = "([^"]+)"$', default_text,
                              re.MULTILINE)
            if not match:
                raise RuntimeError("target triple missing from audit IR")
            target_triple = match.group(1)
        ir_identical = default_text == experimental_text
        machine_identical = True
        default_size = None
        experimental_size = None
        if not ir_identical:
            default_object = os.path.join(work, level + "-default.o")
            experimental_object = os.path.join(
                work, level + "-experimental.o")
            compile_consumer(
                level, False, default_object, object_path, env,
                compile_only=True)
            compile_consumer(
                level, True, experimental_object, object_path, env,
                compile_only=True)
            default_size = os.path.getsize(default_object)
            experimental_size = os.path.getsize(experimental_object)
            machine_identical = (normalized_disassembly(default_object) ==
                                 normalized_disassembly(experimental_object))
            if not machine_identical:
                machine_delta_count += 1
        results.append({
            "default_object_size": default_size,
            "experimental_object_size": experimental_size,
            "ir_identical": ir_identical,
            "machine_code_identical": machine_identical,
            "optimization_level": level,
        })
    if machine_delta_count == 0:
        raise RuntimeError("cross-module fixture produced no machine-code delta")
    return target_triple, results


def timed_run(path):
    started = time.perf_counter_ns()
    result = run([path])
    if result.stdout or result.stderr:
        raise RuntimeError("benchmark produced unexpected output")
    return time.perf_counter_ns() - started


def benchmark(work, object_path, env, iterations):
    default_executable = os.path.join(work, "benchmark-default")
    experimental_executable = os.path.join(work, "benchmark-experimental")
    compile_consumer(
        "O2", False, default_executable, object_path, env)
    compile_consumer(
        "O2", True, experimental_executable, object_path, env)
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
    change = ((experimental_median / default_median) - 1.0) * 100.0
    threshold = -2.0
    return {
        "default_median_ns": default_median,
        "experimental_median_ns": experimental_median,
        "iterations": iterations,
        "optimization_level": "O2",
        "relative_change_percent": round(change, 3),
        "stable_improvement": change <= threshold,
        "stable_improvement_threshold_percent": threshold,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--benchmark", action="store_true")
    parser.add_argument("--benchmark-iterations", type=int, default=7)
    parser.add_argument("--output")
    arguments = parser.parse_args()
    if arguments.benchmark_iterations < 3:
        raise RuntimeError("--benchmark-iterations must be at least 3")
    if not os.path.isfile(TOKAC):
        raise RuntimeError("tokac not found: " + TOKAC)
    if shutil.which(LLVM_OBJDUMP) is None:
        raise RuntimeError("llvm-objdump not found: " + LLVM_OBJDUMP)
    with tempfile.TemporaryDirectory(
            prefix="toka_cross_module_nocapture_") as work:
        _, object_path, env = prepare_provider(work)
        activation = prove_activation(work, object_path, env)
        target_triple, results = static_audit(work, object_path, env)
        document = {
            "activation": activation,
            "decision": "BenchmarkRequired",
            "optimization_levels": list(LEVELS),
            "reason": "CrossModuleMachineCodeDelta",
            "results": results,
            "schema": SCHEMA,
            "target_triple": target_triple,
            "version": 1,
        }
        if arguments.benchmark:
            measurement = benchmark(
                work, object_path, env, arguments.benchmark_iterations)
            document["runtime_benchmark"] = measurement
            document["decision"] = "KeepExperimental"
            document["reason"] = (
                "StableBenefitRequiresSeparateDefaultAudit"
                if measurement["stable_improvement"]
                else "NoStableRuntimeBenefit")
    encoded = json.dumps(document, indent=2, sort_keys=True) + "\n"
    if arguments.output:
        with open(arguments.output, "w", encoding="utf-8") as stream:
            stream.write(encoded)
    else:
        sys.stdout.write(encoded)


if __name__ == "__main__":
    try:
        main()
    except (KeyError, OSError, RuntimeError, ValueError) as error:
        print("Cross-module nocapture audit FAILED: %s" % error,
              file=sys.stderr)
        sys.exit(1)
