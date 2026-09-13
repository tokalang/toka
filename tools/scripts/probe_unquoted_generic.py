#!/usr/bin/env python3
"""Bounded design probe, NOT a qualification gate or a syntax migration.

Compare proposed whole-value spelling with existing morphic spelling. Preserve
every diagnostic; a rejection is an implementation observation, not a language
design verdict. Programs are checked only, never run after partial validation.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def source(operation, kind, quoted):
    q = "'" if quoted else ""
    slot = f"Slot<{q}T>"
    setup = f"auto slot# = {slot}(value = cede {q}value)\n"
    actions = {
        'binding': '',
        'member': f"auto {q}local = (cede slot.{q}value):{q}T\n",
        'index': "auto slots = [cede slot]\nauto local = cede slots[0]\n",
        'passing': f"inspect<{q}T>(slot)\n",
        'return': "return cede slot\n",
        'borrow': "auto &view = &slot\n",
        'replacement': f"slot = {slot}(value = cede {q}next)\n",
    }
    replacement = operation == 'replacement'
    result = f" -> {slot}" if operation == 'return' else ''
    second = f", cede {q}next:T" if replacement else ''
    declarations = (f"shape Slot<{q}T>({q}value:T)\n"
                    f"fn inspect<{q}T>(slot:{slot}) {{}}\n"
                    f"fn exercise<{q}T>(cede {q}value:T{second}){result} {{\n"
                    + setup + actions[operation] + "}\n")
    if kind == 'i32':
        values = 'auto first = 1:i32\nauto second = 2:i32\n'
        actuals = 'cede first' + (', cede second' if replacement else '')
    else:
        hat = kind[0]
        values = (f'auto {hat}first = new Cell(value = 1)\n'
                  f'auto {hat}second = new Cell(value = 2)\n')
        actuals = f'cede {hat}first' + (f', cede {hat}second' if replacement else '')
    return ('shape Cell(value:i32)\n' + declarations +
            f'fn main() -> i32 {{\n{values}exercise<{kind}>({actuals})\nreturn 0\n}}\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    rows = []
    for operation in ('binding', 'member', 'index', 'passing', 'return', 'borrow', 'replacement'):
        for kind in ('i32', '^Cell', '~Cell'):
            for quoted in (False, True):
                name = f"{operation}_{kind.replace('^', 'unique_').replace('~', 'shared_')}_{'quoted' if quoted else 'plain'}"
                path = args.output_dir / (name + '.tk')
                path.write_text(source(operation, kind, quoted))
                result = subprocess.run([
                    str(args.build_dir / 'bin/tokac'), '--workspace-node', 'generic-design-probe',
                    '--workspace-root', str(args.output_dir), str(path), '--check-only'],
                    env=env, cwd=ROOT, capture_output=True, text=True, timeout=60)
                (args.output_dir / (name + '.stderr')).write_text(result.stderr)
                row = dict(operation=operation, type=kind, quoted=quoted,
                           returncode=result.returncode,
                           errors=re.findall(r'error\[(E\d+)\]', result.stderr))
                rows.append(row)
                print(json.dumps(row), flush=True)
    (args.output_dir / 'results.json').write_text(json.dumps(rows, indent=2) + '\n')


if __name__ == '__main__':
    main()
