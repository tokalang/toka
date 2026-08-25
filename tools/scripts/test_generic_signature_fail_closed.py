#!/usr/bin/env python3
"""Prove rejected generic method signatures cannot emit interface/object files."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOKAC = Path(os.environ.get("TOKAC", REPOSITORY_ROOT / "build/bin/tokac")).resolve()
FIXTURES = (
    REPOSITORY_ROOT / "tests/fail/generic_impl_unknown_nested_type.tk",
    REPOSITORY_ROOT / "tests/fail/generic_trait_unknown_nested_type.tk",
)


def main() -> int:
    if not TOKAC.is_file():
        print(f"FAIL: tokac binary does not exist: {TOKAC}")
        return 1

    with tempfile.TemporaryDirectory(prefix="toka-generic-signature-") as temp:
        temp_root = Path(temp)
        for fixture in FIXTURES:
            case_root = temp_root / fixture.stem
            case_root.mkdir()
            source = case_root / "provider.tk"
            shutil.copy2(fixture, source)
            output = case_root / "provider.o"
            result = subprocess.run(
                [
                    str(TOKAC),
                    "-c",
                    str(source),
                    "-I",
                    str(REPOSITORY_ROOT / "lib"),
                    "-I",
                    str(REPOSITORY_ROOT),
                    "-o",
                    str(output),
                ],
                cwd=REPOSITORY_ROOT,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            if result.returncode == 0 or "error[E0463]" not in result.stdout:
                print(f"FAIL: {fixture.name} did not fail with E0463")
                print(result.stdout)
                return 1
            emitted = sorted(
                path.name
                for path in case_root.iterdir()
                if path.name != source.name
            )
            if emitted:
                print(
                    f"FAIL: {fixture.name} emitted artifacts after semantic "
                    f"rejection: {emitted}"
                )
                return 1

    print("PASS: generic impl/trait signature rejection emitted no TKI/object")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
