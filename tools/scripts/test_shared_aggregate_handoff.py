#!/usr/bin/env python3
"""Sema disposition -> evaluation -> aggregate ownership, not a global RC change."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PRELUDE = """import core/option::{Option}
auto drops# = 0:i32
shape Cell(id:i32)
impl Cell@Encap {
    pub id
    fn drop(self#) { drops += 1 }
}
shape Holder(~value:Cell)
fn make() -> ~Cell {
    auto ~value = new Cell(id=42)
    return cede ~value
}
"""


def make_case(destination, source_kind, action):
    morphic = source_kind.endswith("morphic")
    parameter = source_kind.startswith("parameter")
    identity = "'value" if morphic else "~value"
    payload = "'value" if morphic else "value"
    value = identity if action == "copy" else "cede " + identity
    if action == "wrapped":
        value = "unsafe ((cede unsafe " + identity + "):~Cell)"
    ctor = {"enum": "Option<~Cell>::Some(" + value + ")",
            "struct": "Holder(~value=" + value + ")",
            "record": "(value=" + value + ")"}[destination]
    check = "!box.is_some()" if destination == "enum" else "box.value.id != 42"
    body = "{ auto box = " + ctor + "\nif " + check + " || drops != 0 { return 1 } }\n"
    if action == "copy" and not (parameter and morphic):
        body += "if " + payload + ".id != 42 { return 2 }\n"
    body += "if drops != 0 { return 3 }\nreturn 0\n"
    if parameter:
        formal = ("cede " if action != "copy" else "") + ("'value:T" if morphic else "~value:Cell")
        route = "fn route" + ("<'T>" if morphic else "") + "(" + formal + ")->i32 {\n" + body + "}\n"
        arg = "~value" if action == "copy" else "cede ~value"
        exercise = "auto ~value = make()\nauto ~other = ~value\n"
        exercise += "auto result = route" + ("<~Cell>" if morphic else "") + "(" + arg + ")\n"
        if action == "copy" and morphic:
            # Opaque T cannot inspect Cell fields after instantiation. Check
            # the same surviving source in its concrete caller instead.
            exercise += "if value.id != 42 { return 2 }\n"
        exercise += "if result != 0 { return result }\nif other.id != 42 || drops != 0 { return 4 }\nreturn 0\n"
    else:
        route = ""
        exercise = ("auto 'value = make()" if morphic else "auto ~value = make()") + "\n"
        exercise += "auto ~other = ~" + payload + "\n" + body
    return PRELUDE + route + "fn exercise()->i32 {\n" + exercise + "}\n" + """fn main()->i32 {
    auto code = exercise()
    if code != 0 { return code }
    if drops != 1 { return 90 }
    return 0
}
"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))

    def run(source, *flags):
        return subprocess.run([str(compiler), str(source), *map(str, flags)],
                              cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)

    with tempfile.TemporaryDirectory(prefix="toka-shared-handoff-") as directory:
        work = Path(directory)
        for destination in ("enum", "struct", "record"):
            for source_kind in ("local_ordinary", "local_morphic", "parameter_ordinary", "parameter_morphic"):
                for action in ("copy", "move", "wrapped"):
                    name = destination + "_" + source_kind + "_" + action
                    source = work / (name + ".tk")
                    source.write_text(make_case(destination, source_kind, action))
                    normal = run(source, "--check-only")
                    shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
                    assert normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr, (name, normal.stderr, shadow.stderr)
                    json.loads(shadow.stdout)
                    binary = work / name
                    built = run(source, "-o", binary)
                    assert built.returncode == 0, (name, built.stderr)
                    executed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
                    assert executed.returncode == 0, (name, executed.returncode, executed.stderr)
                    ir = work / (name + ".ll")
                    emitted = run(source, "--emit-llvm", "-o", ir)
                    assert emitted.returncode == 0, (name, emitted.stderr)
                    functions = dict(re.findall(r"^define [^\n]*?@([^ (]+)\([^\n]*\)[^\n]*\{(.*?)^}", ir.read_text(), re.M | re.S))
                    exercise = functions["exercise"]
                    if source_kind.startswith("parameter"):
                        targets = re.findall(r"call i32 @([^ (]+)\(", exercise)
                        assert len(targets) == 1, (name, targets)
                        body = functions[targets[0]]
                        expected = 1 if action == "copy" else 0
                    else:
                        body = exercise
                        expected = 2 if action == "copy" else 1  # retained survivor is outside aggregate
                    assert body.count("atomicrmw add") == expected, (name, expected, body[:6000])
                    print("PASS runtime/parity/retain-count: " + name, flush=True)

        rejected = {
            "borrow": "fn main()->i32 { auto ~value=make(); auto &~view=&~value; auto box=Option<~Cell>::Some(cede unsafe ~value); return view.id }\n",
            "parameter": "fn bad(~value:Cell)->i32 { auto box=Option<~Cell>::Some(cede unsafe ~value); return value.id }\nfn main()->i32{return 0}\n",
            "moved": "fn main()->i32 { auto ~value=make(); auto box=Option<~Cell>::Some(cede unsafe ~value); return value.id }\n",
            "permission": "shape Writable(~value#:Cell)\nfn main()->i32 {auto ~value=make(); auto box=Writable(~value=~value);return value.id}\n",
        }
        for name, tail in rejected.items():
            source = work / (name + ".tk")
            source.write_text(PRELUDE + tail)
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == 1 and normal.stderr == shadow.stderr, (name, normal.stderr, shadow.stderr)
            expected = {"borrow": "E0440", "parameter": "E0473", "moved": "E0438", "permission": "E04573"}[name]
            assert expected in normal.stderr and not re.search(r"error\[E01", normal.stderr), (name, normal.stderr)
            if name != "moved":
                assert "E0438" not in normal.stderr and "E0410" not in normal.stderr, (name, normal.stderr)
            for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (name + suffix)
                failed = run(source, flag, "-o", output)
                assert failed.returncode == 1 and not output.exists(), failed.stderr
            print("PASS rejection/rollback/parity/no-artifact: " + name, flush=True)
    print("36 runtime/parity/IR cells; 4 rejection/rollback controls; no skipped cases.")


if __name__ == "__main__":
    main()
