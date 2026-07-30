#!/usr/bin/env python3
"""Source-checkout entry point for the Toka incremental build driver.

Release archives replace this small loader with the versioned driver itself.
Keeping the source tree entry point here lets `TOKA_LIB=/path/to/toka/lib`
work from an arbitrary consumer directory as well.
"""

from __future__ import annotations

import runpy
from pathlib import Path


SOURCE_DRIVER = Path(__file__).resolve().parents[2] / "tools" / "scripts" / "toka_build.py"

if not SOURCE_DRIVER.is_file():
    raise SystemExit("Toka source build driver is unavailable")

runpy.run_path(str(SOURCE_DRIVER), run_name="__main__")
