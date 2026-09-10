#!/usr/bin/env python3
"""Complete managed-handle replacement through a qualified guard reference."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/semantics/std_thread_handoff/sync_managed_slot_replace.tk"
PREFIX = """import std/sync::{Mutex, RwMutex}
import std/thread::thread_spawn
auto drops# = 0:i32
shape Token(value: i32)
impl Token@Encap { pub value
fn drop(self#) { drops += 1 }
}
impl Token@Send {}
impl Token@Sync {}
"""
START = """auto ^first = new Token(value = 7)
auto ~mutex = Mutex<^Token>::make_shared(cede ^first)
auto held = mutex.lock().unwrap()
auto &^#slot = held.borrow_mut()
auto ^incoming = new Token(value = 9)
"""
READS = "\nauto after_source = incoming.value\nauto after_slot = slot.value\nreturn 0\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    with tempfile.TemporaryDirectory(prefix="toka-managed-slot-") as directory:
        work = Path(directory)
        runtime = work / "toka_rt.o"
        subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-pthread", "-c",
                        str(ROOT / "lib/sys/toka_rt.c"), "-o", str(runtime)], check=True)

        def compile(source, *flags):
            return subprocess.run([str(compiler), "--workspace-node", "managed-slot-test", "--workspace-root",
                                   str(ROOT if source.is_relative_to(ROOT) else work), str(source), *map(str, flags)],
                                  cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)

        def materialize(name, body):
            source = work / (name + ".tk")
            source.write_text(PREFIX + body)
            return source

        cases = {"complete": SOURCE}
        cases["rhs-order"] = materialize("rhs-order", """
auto prepared# = false
shape Ordered(value: i32)
impl Ordered@Encap { pub value
fn drop(self#) {
if self.value == 7 { assert(prepared, "old cleanup preceded RHS preparation") }
drops += 1
}
}
impl Ordered@Send {}
fn prepare() -> ^Ordered {
prepared = true
return new Ordered(value = 9)
}
fn main() -> i32 {
{
auto ^first = new Ordered(value = 7)
auto ~mutex = Mutex<^Ordered>::make_shared(cede ^first)
auto held = mutex.lock().unwrap()
auto &^#slot = held.borrow_mut()
^slot = prepare()
if drops != 1 || slot.value != 9 { return 1 }
}
return drops - 2
}
""")
        cases["shared-copy"] = materialize("shared-copy", """
fn main() -> i32 {
{
auto ~source = new Token(value = 7)
auto ~initial = ~source
auto ~mutex = Mutex<~Token>::make_shared(cede ~initial)
{
auto held = mutex.lock().unwrap()
auto &~#slot = held.borrow_mut()
~slot = ~source
if drops != 0 || source.value != 7 || slot.value != 7 { return 1 }
}
}
return drops - 1
}
""")
        repeat_start = """auto ~first = new Token(value = 4)
auto ~mutex = Mutex<~Token>::make_shared(cede ~first)
auto held = mutex.lock().unwrap()
auto &~#slot = held.borrow_mut()
auto ~incoming = new Token(value = 5)
~slot = ~incoming
"""
        for name, extra in (("shared-once", ""), ("shared-twice", "~slot = ~incoming\n"),
                            ("shared-branch-repeat", "if incoming.value == 5 { ~slot = ~incoming }\n~slot = ~incoming\n")):
            cases[name] = materialize(name, "fn main() -> i32 {\n{\n" + repeat_start + extra +
                "if drops != 1 || slot.value != 5 || incoming.value != 5 { return 1 }\n}\nreturn drops - 2\n}\n")
        cases["shared-mixed-repeat"] = materialize("shared-mixed-repeat", "fn main() -> i32 {\n{\n" + repeat_start + """
~slot = ~incoming
auto ~next = new Token(value = 6)
~slot = cede ~next
if incoming.value != 5 || slot.value != 6 || drops != 1 { return 1 }
~slot = ~incoming
if incoming.value != 5 || slot.value != 5 || drops != 2 { return 2 }
}
return drops - 3
}
""")
        for hat in ("^", "~"):
            cases["thread-" + hat] = materialize("thread-" + ("unique" if hat == "^" else "shared"), """
fn main() -> i32 {
{
auto HATfirst = new Token(value = 7)
auto ~mutex = Mutex<HATToken>::make_shared(cede HATfirst)
auto ~worker = ~mutex
auto callback = { [cede ~worker] =>
auto held = worker.lock().unwrap()
auto &HAT#slot = held.borrow_mut()
auto HATincoming = new Token(value = 9)
HATslot = cede HATincoming
return 0
}:dyn fn() -> i32
auto created = thread_spawn<i32>(cede callback)
auto handle# = created.unwrap()
handle#.join().unwrap()
if drops != 1 { return 1 }
auto held = mutex.lock().unwrap()
auto &HATslot = held.borrow_mut()
if slot.value != 9 { return 2 }
}
return drops - 2
}
""".replace("HAT", hat))
        for name, source in cases.items():
            normal = compile(source, "--check-only")
            shadow = compile(source, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr, (name, normal.stderr, shadow.stderr)
            binary = work / ("run-" + name.replace("^", "u").replace("~", "s"))
            built = compile(source, runtime, "-o", binary)
            assert built.returncode == 0 and binary.exists(), (name, built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            assert ran.returncode == 0, (name, ran.returncode, ran.stdout, ran.stderr)
            print("PASS runtime/parity: " + name, flush=True)

        negatives = {
            "readonly-binding": (START.replace("&^#slot", "&^slot") + "^slot = cede ^incoming" + READS, ("E04572",)),
            "readonly-guard": (START.replace("Mutex<", "RwMutex<").replace("mutex.lock()", "mutex.read_lock()")
                               .replace("held.borrow_mut()", "held.borrow()") + "^slot = cede ^incoming" + READS, ("E04572", "E04573")),
            "live-borrow": (START + "auto &view = &slot\n^slot = cede ^incoming\nauto after_borrow = view.value" + READS,
                            ("E0442",)),
            "overlap": (START + "^slot = cede ^slot" + READS, ("E04615", "E04646", "E04583", "E04663", "E04661")),
            "wrong-morphology": (START + "~slot = cede ^incoming" + READS, ("E0408", "E04572", "E04573")),
            "readonly-pointee": (START + "slot.value = 11" + READS, ("E04573", "E04572", "E0424")),
            "bad-rhs": (START + "^slot = require_number(cede ^incoming)" + READS, ("E0408", "E04509", "E04571")),
            "repeat-readonly-pointee": (repeat_start + "~slot = ~incoming\nslot.value = 8\n"
                "auto after_slot = slot.value\nauto after_source = incoming.value\nreturn 0\n", ("E04573",)),
            "repeat-wrong-morphology": (repeat_start + "^slot = cede ~incoming\n"
                "auto after_slot = slot.value\nauto after_source = incoming.value\nreturn 0\n", ("E0408", "E04572")),
        }
        for name, (body, reasons) in negatives.items():
            source = materialize(name, "fn require_number(cede amount: i32) -> ^Token {\ncede amount\nreturn new Token(value = 0) }\n"
                                 + "fn main() -> i32 {\n" + body + "}\n")
            normal = compile(source, "--check-only")
            shadow = compile(source, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == 1 and normal.stderr == shadow.stderr, (name, normal.stderr, shadow.stderr)
            assert any(reason in normal.stderr for reason in reasons), (name, normal.stderr)
            if name == "live-borrow":
                assert "conflicting borrow originates here" in normal.stderr, normal.stderr
            assert "E0438" not in normal.stderr and "E0410" not in normal.stderr, (name, normal.stderr)
            for mode, extension in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (name + extension)
                result = compile(source, mode, "-o", output)
                assert result.returncode == 1 and not output.exists(), (name, result.stderr)
            print("PASS reject/rollback: " + name, flush=True)
        for fault in ("slot-missing", "slot-type", "slot-destination", "slot-kind", "slot-reference", "slot-source"):
            for mode, extension in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (fault + extension)
                result = compile(SOURCE, mode, "-o", output, "--native-sync-witness-fault=" + fault)
                assert result.returncode == 1 and "E0701" in result.stderr and not output.exists(), result.stderr
        print(f"managed-slot: {len(cases)} runtime/parity, {len(negatives)} reject/rollback, 12 no-artifact faults; no skips")


if __name__ == "__main__":
    main()
