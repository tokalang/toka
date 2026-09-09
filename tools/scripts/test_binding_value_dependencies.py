#!/usr/bin/env python3
"""Keep canonical owning-string facts separate from borrowed/unknown storage."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/binding_value_dependencies"


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))

    def run(source, *flags):
        return subprocess.run([str(compiler), *flags, str(FIXTURES / source)], cwd=ROOT,
                              env=env, capture_output=True, text=True, timeout=45)

    with tempfile.TemporaryDirectory(prefix="toka-value-dependencies-") as directory:
        for source in ("owning_string.tk", "owning_wrapper.tk"):
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            require(normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr,
                    source + ": " + normal.stderr + shadow.stderr)
            output = Path(directory) / source.removesuffix(".tk")
            built = run(source, "-o", str(output))
            require(built.returncode == 0 and output.is_file(), built.stderr)
            result = subprocess.run([str(output)], cwd=directory, capture_output=True, text=True, timeout=15)
            require(result.returncode == 0, source + ": runtime " + str(result.returncode) + result.stderr)
        for source in ("reject_same_named_string.tk", "reject_drop_only.tk", "reject_borrowed_instance.tk"):
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            require(normal.returncode == shadow.returncode == 1 and normal.stderr == shadow.stderr and
                    "E04662" in normal.stderr and "ElementDependenciesUnproven" in normal.stderr,
                    source + ": missing dependency rejection\n" + normal.stderr)
            for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = Path(directory) / (source + suffix)
                rejected = run(source, flag, "-o", str(output))
                require(rejected.returncode == 1 and not output.exists(), source + ": rejected artifact or crash")
        for source in ("factory_dynamic.tk", "factory_static.tk", "factory_fields.tk", "factory_projected.tk", "factory_nested_scalar.tk"):
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            require(normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr,
                    source + ": " + normal.stderr + shadow.stderr)
            records = [r for r in json.loads(shadow.stdout)["records"]
                       if r["location"]["file"].endswith(source) and r["boundary"] == "initialization"]
            lines = (FIXTURES / source).read_text().splitlines()

            def binding(name):
                line = next(i + 1 for i, text in enumerate(lines) if "auto " + name + " =" in text)
                found = [r for r in records if r["location"]["line"] == line]
                require(len(found) == 1, source + ": missing or repeated binding record " + name)
                record = found[0]
                require(record["plan"]["outcome"] == "Admitted" and record["plan"]["drop"] == "NoLiability",
                        source + ": borrowed record gained cleanup liability " + name)
                return record

            if source == "factory_dynamic.tk":
                direct, factory = binding("direct"), binding("factory")
                require(direct["plan"]["dependency_roots"] == factory["plan"]["dependency_roots"] and
                        len(factory["plan"]["dependency_roots"]) == 1 and
                        "binding:owner" in factory["plan"]["dependency_roots"][0], "factory lost actual owner")
                require("view" in direct["result_field_referents"] and
                        "" in factory["result_field_referents"], "whole-result contract fabricated a field mapping")
            elif source == "factory_static.tk":
                for name in ("direct", "factory"):
                    record = binding(name)
                    require(record.get("static_storage_origins") and not record["plan"]["dependency_roots"] and
                            record["plan"]["value_production"] == "BorrowCapture", "static record became an owning temporary")
                    require(record.get("result_field_static_storage"), "static field witness lost")
            elif source == "factory_fields.tk":
                dynamic, mixed = binding("dynamic"), binding("mixed")
                mapped = dynamic["result_field_referents"]
                require(set(mapped) == {"left", "right"} and
                        all("binding:second" in p for p in mapped["left"]) and
                        all("binding:first" in p for p in mapped["right"]), "swapped factory field correspondence lost")
                require(set(mixed["result_field_referents"]) == {"right"} and
                        set(mixed["result_field_static_storage"]) == {"left"} and
                        len(mixed["plan"]["dependency_roots"]) == 1 and
                        mixed.get("static_storage_origins"), "mixed static/dynamic origins lost")
            elif source == "factory_projected.tk":
                factory, static_factory = binding("factory"), binding("static_factory")
                require(len(factory["plan"]["dependency_roots"]) == 1 and
                        "binding:first" in factory["plan"]["dependency_roots"][0] and
                        all("field:left" not in p for p in factory["result_field_referents"][""]),
                        "formal projection was appended to underlying storage")
                require(not static_factory["plan"]["dependency_roots"] and
                        static_factory.get("static_storage_origins") and
                        static_factory["plan"]["value_production"] == "BorrowCapture",
                        "projected static field lost its independent witness")
            else:
                value = binding("value")
                require(value.get("static_storage_origins") and not value["plan"]["dependency_roots"],
                        "nested scalar factory lost static witness")
            output = Path(directory) / source.removesuffix(".tk")
            built = run(source, "-o", str(output))
            require(built.returncode == 0 and output.is_file(), source + ": " + built.stderr)
            result = subprocess.run([str(output)], cwd=directory, capture_output=True, text=True, timeout=15)
            require(result.returncode == 0, source + ": runtime " + str(result.returncode) + result.stderr)
        for source, diagnostic in {
                "reject_factory_field.tk": "E0454",
                "reject_factory_local.tk": "E0455",
                "reject_factory_descriptor.tk": "E0455",
                "reject_literal_descriptor.tk": "E0455",
                "reject_unknown_factory.tk": "E04661",
                "reject_unmapped_raw_field.tk": "E04661",
                "reject_nested_raw_field.tk": "E04661",
                "reject_nested_raw_array.tk": "E04661",
                "reject_nested_raw_generic.tk": "E04661",
                "reject_nested_raw_enum.tk": "E04661"}.items():
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            require(normal.returncode == shadow.returncode == 1 and normal.stderr == shadow.stderr and
                    diagnostic in normal.stderr, source + ": " + normal.stderr + shadow.stderr)
            for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = Path(directory) / (source + suffix)
                rejected = run(source, flag, "-o", str(output))
                require(rejected.returncode == 1 and not output.exists(), source + ": rejected artifact or crash")
    print("value dependencies: 2 runtime/parity positives, 3 rejection/parity cases, 6 no-artifact checks; no skips")
    print("factory dependencies: 5 runtime/parity/evidence cases, 10 rejection/parity cases, 20 no-artifact checks; no skips")


if __name__ == "__main__":
    main()
