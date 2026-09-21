#!/usr/bin/env python3
"""Wait provenance, declaration-based Copy, and raw-field regression controls."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--case", action="append", default=[])
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    prefix = '''import std/task
fn borrow_view(value: str) -> async str <- value { return value }
'''
    body = '''    auto owner = string::from("local")
    auto view = owner.as_view()
    auto task = borrow_view(view)
'''
    cases = {
        "wait_escape": (prefix + 'fn escape() -> str {\n' + body +
                        '    return task.wait\n}\nfn main() -> i32 { return 0 }\n', "E0455"),
        "wait_bound_escape": (prefix + 'fn escape() -> str {\n' + body +
                              '    auto result = task.wait\n    return result\n}\n'
                              'fn main() -> i32 { return 0 }\n', "E0455"),
        "wait_scoped": (prefix + 'fn main() -> i32 {\n' + body +
                        '    auto result = task.wait\n'
                        '    assert(result.equals("local"), "view content")\n'
                        '    return 0\n}\n', None),
        "direct_escape": (prefix + 'fn escape() -> str {\n' +
                          '    auto owner = string::from("local")\n'
                          '    return owner.as_view()\n}\n'
                          'fn main() -> i32 { return 0 }\n', "E0455"),
        "wait_shadowed_input": (prefix + 'fn escape() -> str {\n' + body +
                                '    {\n        auto view = "static"\n'
                                '        return task.wait\n    }\n}\n'
                                'fn main() -> i32 { return 0 }\n', "E0455"),
        "wait_shadowed_origin": (prefix + 'fn escape(input: str) -> str {\n'
                                 '    auto view = input\n    auto task = borrow_view(view)\n'
                                 '    {\n        auto input = "static"\n'
                                 '        return task.wait\n    }\n}\n'
                                 'fn main() -> i32 { return 0 }\n', "E04658"),
    }
    for name in ("TimerHeap", "Ordinary", "SlabID"):
        cases["copy_" + name] = (f'''shape {name}(value: i32)
impl {name}@Encap {{ fn drop(self#) {{ }} }}
fn main() -> i32 {{
    auto original = {name}(value = 7)
    auto copied = original
    return 0
}}
''', "E04652" if name == "Ordinary" else "E04661")
    cases["empty_storage_mutated"] = (
        (ROOT / "tests/semantics/json_leaf_factories/empty_storage_mutated.tk").read_text(),
        "E04661")
    cases["copy_proven_declaration"] = ('''shape TimerHeap(value: i32)
fn main() -> i32 {
    auto original = TimerHeap(value = 7)
    auto copied = original
    assert(original.value == copied.value, "proved value copy")
    return 0
}
''', None)
    cases["move_resource_once"] = ('''auto drops# = 0:i32
shape TimerHeap(value: i32)
impl TimerHeap@Encap { fn drop(self#) { drops += 1 } }
fn main() -> i32 {
    {
        auto original = TimerHeap(value = 7)
        auto moved = cede original
    }
    assert(drops == 1, "one transferred cleanup")
    return 0
}
''', None)
    for name in ("g10_http_phase1_test", "g10_net_http_server_test"):
        cases[name] = ((ROOT / "tests/pass" / (name + ".tk")).read_text(), None)
    for name in ("g04_anon_records", "g08_noshared", "g08_test"):
        cases[name] = ((ROOT / "tests/pass" / (name + ".tk")).read_text(), None)
    cases["record_mixed_scoped"] = ('''fn main() -> i32 {
    auto owner = string::from("live")
    auto value = (fixed = "static", view = owner.as_view(), number = 7)
    assert(value.fixed.equals("static"), "static field")
    assert(value.view.equals("live"), "dynamic field")
    return 0
}
''', None)
    cases["record_dynamic_escape"] = ('''fn escape() -> str {
    auto owner = string::from("local")
    auto value = (fixed = "static", view = owner.as_view(), number = 7)
    return value.view
}
fn main() -> i32 { return 0 }
''', "E0455")
    cases["record_descriptor_escape"] = ('''fn escape() -> &str {
    auto value = (view = "static", number = 7)
    return &(value.view)
}
fn main() -> i32 { return 0 }
''', "E0455")
    cases["record_shadowed_dynamic"] = ('''fn escape() -> str {
    auto owner = string::from("local")
    auto text = owner.as_view()
    auto forwarded = text
    {
        auto text = "static"
        auto value = (view = forwarded, number = 7)
        return value.view
    }
}
fn main() -> i32 { return 0 }
''', "E0455")
    cases["record_static_field_only"] = ('''fn select() -> str {
    auto owner = string::from("unrelated")
    auto value = (fixed = "static", view = owner.as_view())
    return value.fixed
}
fn main() -> i32 {
    auto result = select()
    assert(result.equals("static"), "only selected static field escapes")
    return 0
}
''', None)
    cases["record_shadowed_static"] = ('''fn select() -> str {
    auto text = "static"
    auto forwarded = text
    {
        auto owner = string::from("unrelated")
        auto text = owner.as_view()
        auto value = (view = forwarded, number = 7)
        return value.view
    }
}
fn main() -> i32 {
    auto result = select()
    assert(result.equals("static"), "original binding identity")
    return 0
}
''', None)
    if args.case:
        assert set(args.case) <= cases.keys(), args.case
        cases = {name: cases[name] for name in args.case}
    with tempfile.TemporaryDirectory(prefix="toka-wait-copy-") as directory:
        work = Path(directory)

        def compile(source, *flags):
            return subprocess.run([str(compiler), str(source), *map(str, flags)],
                                  cwd=ROOT, env=env, text=True, capture_output=True, timeout=90)

        for name, (text, error) in cases.items():
            source = work / (name + ".tk")
            source.write_text(text)
            expected = 1 if error else 0
            normal = compile(source, "--check-only")
            shadow = compile(source, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == expected, (name, normal.stderr, shadow.stderr)
            assert normal.stderr == shadow.stderr, name
            for mode, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (name + suffix)
                result = compile(source, mode, "-o", output)
                assert result.returncode == expected, (name, result.stderr)
                if error:
                    assert f"error[{error}]" in result.stderr and str(source) in result.stderr, result.stderr
                    assert not output.exists(), output
                else:
                    assert output.is_file() and output.stat().st_size > 0, output
            if not error:
                output = work / name
                built = compile(source, "-o", output)
                assert built.returncode == 0, built.stderr
                run = subprocess.run([str(output)], timeout=30, capture_output=True)
                assert run.returncode == 0, (name, run.stderr)
                assert b"skipping" not in run.stdout.lower(), (name, run.stdout)
            print("PASS", name, flush=True)


if __name__ == "__main__":
    main()
