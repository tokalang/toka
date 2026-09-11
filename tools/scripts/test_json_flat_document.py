#!/usr/bin/env python3
"""Qualify the library-only flat representation; this is not JSON recovery."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    prototype = (ROOT / "tests/semantics/json_flat_document/prototype.tk").read_text()
    with tempfile.TemporaryDirectory(prefix="toka-json-flat-") as directory:
        work = Path(directory)

        def compile(source, *flags):
            return subprocess.run([str(compiler), str(source), *map(str, flags)],
                                  cwd=ROOT, env=env, text=True, capture_output=True, timeout=90)

        def check(name, text, diagnostic=None):
            source = work / (name + ".tk")
            source.write_text(text)
            normal = compile(source, "--check-only")
            shadow = compile(source, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == (1 if diagnostic else 0), (name, normal.stderr, shadow.stderr)
            assert normal.stderr == shadow.stderr, (name, normal.stderr, shadow.stderr)
            json.loads(shadow.stdout)
            output = work / name
            built = compile(source, "-o", output)
            if diagnostic:
                assert built.returncode == 1 and diagnostic in built.stderr, (name, built.stderr)
                assert not output.exists(), name
                ir = work / (name + ".ll")
                built = compile(source, "--emit-llvm", "-o", ir)
                assert built.returncode == 1 and not ir.exists(), (name, built.stderr)
            else:
                assert built.returncode == 0, (name, built.stderr)
                ran = subprocess.run([str(output)], capture_output=True, text=True, timeout=15)
                assert ran.returncode == 0, (name, ran.returncode, ran.stderr)
            print("PASS " + name, flush=True)

        check("construction_move_return_cleanup", prototype)
        check("input_owner_can_die", prototype.replace("fn build() -> Document", "fn build_from(input: str) -> Document")
              .replace('string::from("hello")', 'string::from(input)') + """
fn build() -> Document {
    auto input = string::from("hello")
    auto document = build_from(input.as_str())
    return cede document
}
""")
        check("growth_preserves_offsets", prototype.replace("auto text = string::from", "auto text# = string::from")
              .replace("    auto document = Document", """
    auto i# = 0:usize
    loop i < 100 {
        nodes#.push(Node(kind = 0, first = 0, next = 0, start = 0, count = 0))
        text#.push_str("!")
        i += 1
    }
    auto document = Document""").replace("nodes.len() == 2", "nodes.len() == 102"))
        factory = prototype.replace("        auto original = build()",
              "        auto attempt = try_build(true)\n        auto original = attempt.unwrap()")
        factory = factory.replace("    return 0\n}", """
    auto failure = try_build(false)
    if !failure.is_err() || destroyed != 3 { return 4 }
    return 0
}""")
        factory += """
fn try_build(ok: bool) -> Result<Document, i32> {
    auto document = build()
    if !ok {
        auto result = Result<Document, i32>::Err(7)
        return cede result
    }
    auto result = Result<Document, i32>::Ok(cede document)
    return cede result
}
"""
        check("complete_value_factory", factory)
        check("use_after_move", prototype.replace("        auto root = moved.nodes.get(0)",
              "        auto root = original.nodes.get(0)"), "E0438")
        check("view_escape", prototype + """
fn escaped() -> str {
    auto document = build()
    return document.text.as_str()
}
""", "E0455")
        check("descriptor_escape", prototype + """
fn escaped_descriptor() -> &str {
    auto document = build()
    auto view = document.text.as_str()
    return &view
}
""", "E0455")
    print("Flat representation probe passed; no parser or recursive-container proof added.")


if __name__ == "__main__":
    main()
