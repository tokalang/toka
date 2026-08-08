#!/usr/bin/env python3

"""Fail closed if default ready-queue policy leaks into ordinary CodeGen."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
POP_READY = "toka_task_pop_ready"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def read(relative_path):
    return (ROOT / relative_path).read_text(encoding="utf-8")


def main():
    boundary = read("docs/async_runtime_contract_boundary.md")
    for required in (
        "Compiler/semantic runtime",
        "Default executor",
        "Test-only observability",
        "Quarantined compiler exception",
        "Future extraction gate",
        "not a stable runtime interface",
    ):
        require(required in boundary,
                "async runtime boundary document is missing: %s" % required)

    codegen_dir = ROOT / "src" / "CodeGen"
    pop_owners = []
    for source in sorted(codegen_dir.glob("*.cpp")):
        count = source.read_text(encoding="utf-8").count(POP_READY)
        if count:
            pop_owners.append((source.name, count))
    require(pop_owners == [("CodeGen_Decl.cpp", 2)],
            "default ready-queue pop escaped the documented async-main "
            "fallback: %r" % (pop_owners,))

    decl = read("src/CodeGen/CodeGen_Decl.cpp")
    require("A program with an async `main` but no std/task import" in decl,
            "async-main default-runner exception lost its scope marker")
    require('"toka_task_pop_ready"' in decl,
            "async-main fallback no longer identifies its queue entry point")

    core_task = read("lib/core/task.tk")
    require(core_task.count("extern fn toka_task_pop_ready") == 1,
            "core task queue entry declaration changed unexpectedly")
    default_runtime = read("lib/sys/toka_rt.c")
    require(default_runtime.count("int toka_task_pop_ready(") == 1,
            "default runtime queue entry definition changed unexpectedly")

    print("Async runtime contract boundary gate PASSED")


if __name__ == "__main__":
    main()
