#!/usr/bin/env python3
"""Sustained qualification workload for Toka's native incremental builder."""

from __future__ import annotations

import argparse
import json
import os
import random
import shutil
import subprocess
import sys
from pathlib import Path


SCHEMA = "toka.native-build-reference"


class QualificationError(RuntimeError):
    pass


def run(
    argv: list[str],
    *,
    cwd: Path,
    env: dict[str, str],
    expect_success: bool = True,
    timeout: int = 120,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        argv,
        cwd=cwd,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
    )
    if expect_success and result.returncode != 0:
        command = " ".join(argv)
        raise QualificationError(
            f"command failed ({result.returncode}): {command}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if not expect_success and result.returncode == 0:
        raise QualificationError(f"command unexpectedly succeeded: {' '.join(argv)}")
    return result


def module_source(index: int, module_count: int, value: int) -> str:
    children = [child for child in (index * 2 + 1, index * 2 + 2) if child < module_count]
    imports = "".join(
        f"import ./m{child:02d}::{{value_{child:02d}}}\n" for child in children
    )
    terms = [f"value_{child:02d}()" for child in children]
    terms.append(str(value))
    return f"{imports}\npub fn value_{index:02d}() -> i32 {{\n    return {' + '.join(terms)}\n}}\n"


def write_workspace(work: Path, module_count: int) -> dict[int, int]:
    modules = work / "src" / "modules"
    modules.mkdir(parents=True, exist_ok=True)
    values = {index: index + 1 for index in range(module_count)}
    for index, value in values.items():
        (modules / f"m{index:02d}.tk").write_text(
            module_source(index, module_count, value), encoding="utf-8"
        )

    (work / "src" / "main.tk").write_text(
        "import std/io::{println}\n"
        "import ./modules/m00::{value_00}\n\n"
        "fn main() -> i32 {\n"
        '    println("reference-value: {}", value_00())\n'
        "    return 0\n"
        "}\n",
        encoding="utf-8",
    )
    (work / "build.tk").write_text(
        "import build::{Executable, run_build}\n\n"
        "fn main() -> i32 {\n"
        f'    auto app# = Executable::make(c"native_build_reference", c"src/main.tk")\n'
        "    return run_build(app)\n"
        "}\n",
        encoding="utf-8",
    )
    (work / "package.tk").write_text(
        'package(name = "native_build_reference", version = "0.1.0")\n',
        encoding="utf-8",
    )
    return values


def canonical_plan(plan: dict[str, object], work: Path) -> dict[str, object]:
    prefix = str(work.resolve())

    def normalize(value: object) -> object:
        if isinstance(value, str):
            return value.replace(prefix, "$WORK")
        if isinstance(value, list):
            return [normalize(item) for item in value]
        if isinstance(value, dict):
            return {str(normalize(key)): normalize(item) for key, item in sorted(value.items())}
        return value

    normalized = normalize(plan)
    assert isinstance(normalized, dict)
    return normalized


def parse_plan(result: subprocess.CompletedProcess[str], label: str) -> dict[str, object]:
    try:
        value = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise QualificationError(
            f"{label} did not emit JSON: {error}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        ) from error
    if not isinstance(value, dict):
        raise QualificationError(f"{label} emitted a non-object plan")
    return value


class NativeBuildQualification:
    def __init__(self, root: Path, work: Path, module_count: int, seed: int) -> None:
        self.root = root
        self.work = work
        self.module_count = module_count
        self.seed = seed
        self.tokac = root / "build" / "bin" / "tokac"
        self.toka = root / "build" / "bin" / "toka"
        self.driver = root / "tools" / "scripts" / "toka_build.py"
        self.manifest = work / "manifest.json"
        self.executable = work / "target" / "debug" / "native_build_reference"
        self.env = dict(os.environ)
        self.env.update({"TOKA_LIB": str(root / "lib"), "TOKAC": str(self.tokac)})

    def python_plan(self) -> dict[str, object]:
        result = run(
            [
                sys.executable,
                str(self.driver),
                "--plan",
                "-m",
                str(self.manifest),
                "--tokac",
                str(self.tokac),
                "--compiler-args",
                "-o target/debug/native_build_reference",
                "src/main.tk",
            ],
            cwd=self.work,
            env=self.env,
        )
        return parse_plan(result, "python planner")

    def native_plan(self) -> dict[str, object]:
        result = run(
            [str(self.toka), "build", "--plan", "-m", str(self.manifest)],
            cwd=self.work,
            env=self.env,
        )
        return parse_plan(result, "native planner")

    def compare_plans(self) -> dict[str, object]:
        python_plan = canonical_plan(self.python_plan(), self.work)
        native_plan = canonical_plan(self.native_plan(), self.work)
        if python_plan != native_plan:
            raise QualificationError(
                "native and Python plans differ:\n"
                f"python={json.dumps(python_plan, sort_keys=True)}\n"
                f"native={json.dumps(native_plan, sort_keys=True)}"
            )
        return native_plan

    def build(self, expect_success: bool = True) -> subprocess.CompletedProcess[str]:
        return run(
            [str(self.toka), "build", "-m", str(self.manifest)],
            cwd=self.work,
            env=self.env,
            expect_success=expect_success,
        )

    def output(self) -> str:
        if not self.executable.is_file():
            raise QualificationError("native builder did not produce the reference executable")
        return run([str(self.executable)], cwd=self.work, env=self.env).stdout

    def module_path(self, index: int) -> Path:
        return self.work / "src" / "modules" / f"m{index:02d}.tk"


def assert_dirty(plan: dict[str, object], suffix: str, reason: str | None = None) -> None:
    if plan.get("status") != "dirty":
        raise QualificationError(f"expected dirty plan for {suffix}: {plan}")
    modules = plan.get("dirty_modules")
    if not isinstance(modules, dict):
        raise QualificationError("dirty plan has no dirty_modules object")
    match = next((item for path, item in modules.items() if str(path).endswith(suffix)), None)
    if not isinstance(match, dict):
        raise QualificationError(f"dirty plan did not contain {suffix}")
    if reason is not None and match.get("reason") != reason:
        raise QualificationError(f"expected {suffix} reason {reason}, got {match}")


def qualify(args: argparse.Namespace) -> dict[str, object]:
    root = Path(__file__).resolve().parents[2]
    work = (root / args.work_root).resolve()
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    values = write_workspace(work, args.modules)
    qualification = NativeBuildQualification(root, work, args.modules, args.seed)
    rng = random.Random(args.seed)

    initial = qualification.compare_plans()
    if initial.get("status") != "dirty":
        raise QualificationError("first native build plan was not dirty")
    qualification.build()
    baseline_output = qualification.output()
    if qualification.compare_plans().get("status") != "clean":
        raise QualificationError("no-op plan was not clean")

    committed_builds = 0
    restore_checks = 0
    leaves = list(range(args.modules // 2, args.modules))
    for cycle in range(args.cycles):
        index = rng.choice(leaves)
        previous = values[index]
        candidate = previous + cycle + 17
        qualification.module_path(index).write_text(
            module_source(index, args.modules, candidate), encoding="utf-8"
        )
        plan = qualification.compare_plans()
        assert_dirty(plan, f"/m{index:02d}.tk", "hash changed")
        assert_dirty(plan, "/main.tk", "dependency changed")

        if cycle % 10 == 0:
            values[index] = candidate
            qualification.build()
            qualification.output()
            committed_builds += 1
        else:
            qualification.module_path(index).write_text(
                module_source(index, args.modules, previous), encoding="utf-8"
            )
            if qualification.compare_plans().get("status") != "clean":
                raise QualificationError(f"cycle {cycle} did not recover to a clean plan")
            restore_checks += 1

    qualification.executable.unlink()
    missing = qualification.compare_plans()
    assert_dirty(missing, "/main.tk", "missing output")
    qualification.build()

    manifest_data = json.loads(qualification.manifest.read_text(encoding="utf-8"))
    for module in manifest_data["modules"].values():
        module["compiler_version"] = "99.0.reference"
    qualification.manifest.write_text(
        json.dumps(manifest_data, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    version_plan = qualification.compare_plans()
    assert_dirty(version_plan, "/main.tk", "version/target changed")
    qualification.build()

    graph_index = leaves[-1]
    graph_path = qualification.module_path(graph_index)
    graph_source = graph_path.read_text(encoding="utf-8")
    graph_path.write_text(
        graph_source
        + "\npub fn qualification_marker() -> i32 {\n"
        + "    return 100\n"
        + "}\n",
        encoding="utf-8",
    )
    interface_plan = qualification.compare_plans()
    assert_dirty(interface_plan, f"/m{graph_index:02d}.tk", "hash changed")
    assert_dirty(interface_plan, "/main.tk", "dependency changed")
    qualification.build()
    graph_path.write_text(graph_source, encoding="utf-8")
    qualification.build()

    extra_path = graph_path.parent / "extra.tk"
    extra_path.write_text(
        "pub fn extra_value() -> i32 {\n    return 701\n}\n", encoding="utf-8"
    )
    graph_path.write_text(
        "import ./extra::{extra_value}\n\n"
        f"pub fn value_{graph_index:02d}() -> i32 {{\n"
        f"    return extra_value() + {values[graph_index]}\n"
        "}\n",
        encoding="utf-8",
    )
    addition_plan = qualification.compare_plans()
    assert_dirty(addition_plan, "/extra.tk", "new module")
    assert_dirty(addition_plan, f"/m{graph_index:02d}.tk", "hash changed")
    qualification.build()

    extra_source = extra_path.read_text(encoding="utf-8")
    extra_path.unlink()
    qualification.build(expect_success=False)
    extra_path.write_text(extra_source, encoding="utf-8")
    qualification.build()

    graph_path.write_text(graph_source, encoding="utf-8")
    extra_path.unlink()
    removal_plan = qualification.compare_plans()
    assert_dirty(removal_plan, f"/m{graph_index:02d}.tk", "hash changed")
    qualification.build()
    current_manifest = json.loads(qualification.manifest.read_text(encoding="utf-8"))
    if any(path.endswith("/extra.tk") for path in current_manifest["modules"]):
        raise QualificationError("removed module remained in the persisted manifest")

    graph_path.write_text(
        "import ./m00::{value_00}\n\n"
        f"pub fn value_{graph_index:02d}() -> i32 {{\n"
        f"    return value_00() + {values[graph_index]}\n"
        "}\n",
        encoding="utf-8",
    )
    cycle_plan = qualification.compare_plans()
    assert_dirty(cycle_plan, f"/m{graph_index:02d}.tk", "hash changed")
    assert_dirty(cycle_plan, "/main.tk", "dependency changed")
    qualification.build()
    graph_path.write_text(graph_source, encoding="utf-8")
    qualification.build()

    failure_index = leaves[0]
    failure_path = qualification.module_path(failure_index)
    valid_source = failure_path.read_text(encoding="utf-8")
    failure_path.write_text("pub fn broken( -> i32 {\n", encoding="utf-8")
    qualification.build(expect_success=False)
    failure_path.write_text(valid_source, encoding="utf-8")
    qualification.build()

    incremental_output = qualification.output()
    qualification.manifest.unlink()
    shutil.rmtree(qualification.executable.parent)
    qualification.build()
    clean_output = qualification.output()
    if incremental_output != clean_output:
        raise QualificationError(
            f"incremental and clean outputs differ: {incremental_output!r} != {clean_output!r}"
        )

    report: dict[str, object] = {
        "cycles": args.cycles,
        "module_count": args.modules,
        "result": "pass",
        "schema": SCHEMA,
        "seed": args.seed,
        "stages": {
            "clean_rebuild_equivalence": "pass",
            "compile_failure_recovery": "pass",
            "dependency_cycle_handling": "pass",
            "first_build": "pass",
            "module_add_remove": "pass",
            "missing_output_recovery": "pass",
            "missing_source_recovery": "pass",
            "mutation_commits": committed_builds,
            "mutation_restores": restore_checks,
            "native_python_plan_equivalence": "pass",
            "no_op_rebuild": "pass",
            "public_interface_change": "pass",
            "version_mismatch_recovery": "pass",
        },
        "version": 1,
    }
    report_path = (root / args.report).resolve()
    report_path.parent.mkdir(parents=True, exist_ok=True)
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    report_path.write_text(encoded, encoding="utf-8")
    if report_path.read_text(encoding="utf-8") != encoded:
        raise QualificationError("report serialization was not deterministic")
    if not args.keep_work:
        shutil.rmtree(work)
    return report


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cycles", type=int, default=100)
    parser.add_argument("--modules", type=int, default=31)
    parser.add_argument("--seed", type=int, default=100098)
    parser.add_argument("--work-root", default="tmp/native_build_qualification")
    parser.add_argument("--report", default="tmp/native_build_reference_report.json")
    parser.add_argument("--keep-work", action="store_true")
    args = parser.parse_args()
    if args.cycles < 1:
        parser.error("--cycles must be positive")
    if not 30 <= args.modules <= 100:
        parser.error("--modules must be between 30 and 100")
    return args


def main() -> int:
    try:
        report = qualify(parse_args())
    except (QualificationError, OSError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
