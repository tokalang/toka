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

    expr = read("src/CodeGen/CodeGen_Expr.cpp")
    require("toka_task_get_current_coro_frame" not in expr and
            "llvm::Intrinsic::coro_promise" not in expr,
            "await/wait result observation escaped the TCB promise accessor")
    require("toka_tcb_get_promise" in expr and
            "toka_tcb_get_promise" in decl,
            "async result observation no longer uses the TCB promise accessor")

    core_task = read("lib/core/task.tk")
    require(core_task.count("extern fn toka_task_pop_ready") == 1,
            "core task queue entry declaration changed unexpectedly")
    require(core_task.count("extern fn toka_task_get_current_coro_frame") == 1,
            "core task current-frame entry declaration changed unexpectedly")
    for legacy_entry in (
        "extern fn toka_task_try_schedule(",
        "extern fn toka_tcb_get_wait_token(",
        "extern fn toka_tcb_get_wait_token_with_instance(",
        "extern fn toka_task_prepare_suspend(",
        "extern fn toka_wait_registry_allocate(",
        "extern fn toka_wait_registry_allocate_pair(",
        "extern fn toka_wait_registry_allocate_nway(",
        "extern fn toka_task_retain(",
    ):
        require(legacy_entry not in core_task,
                f"core task still exposes fail-closed legacy runtime entry: {legacy_entry}")
    for path in ("lib/std/async.tk", "lib/std/task.tk", "lib/std/net.tk"):
        source = read(path)
        require("toka_task_prepare_suspend_token" in source and
                "toka_task_prepare_suspend(" not in source,
                f"{path} bypasses the checked task-instance token path")
        require("toka_wait_registry_allocate_token" in source or
                "toka_wait_registry_allocate_pair_token" in source,
                f"{path} bypasses token-bound wait registration")
    default_runtime = read("lib/sys/toka_rt.c")
    require(default_runtime.count("int toka_task_pop_ready(") == 1,
            "default runtime queue entry definition changed unexpectedly")

    print("Async runtime contract boundary gate PASSED")


if __name__ == "__main__":
    main()
