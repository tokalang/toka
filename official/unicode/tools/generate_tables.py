#!/usr/bin/env python3
"""Generate pinned Unicode 17.0.0 grapheme data and corpus fixtures.

This program never downloads data.  It verifies the vendored source files
against SOURCES.lock.json, then deterministically emits the Toka property
tables and UAX #29 corpus fixture used by the package qualification.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import sys


PACKAGE = Path(__file__).resolve().parents[1]
DATA = PACKAGE / "data" / "17.0.0"
LOCK = DATA / "SOURCES.lock.json"
TABLES = PACKAGE / "lib" / "official" / "generated" / "grapheme_tables.tk"
CORPUS = PACKAGE / "tests" / "grapheme_break_corpus.tk"


GCB_CODES = {
    "Other": "GCB_OTHER",
    "CR": "GCB_CR",
    "LF": "GCB_LF",
    "Control": "GCB_CONTROL",
    "Extend": "GCB_EXTEND",
    "ZWJ": "GCB_ZWJ",
    "Regional_Indicator": "GCB_REGIONAL_INDICATOR",
    "Prepend": "GCB_PREPEND",
    "SpacingMark": "GCB_SPACING_MARK",
    "L": "GCB_L",
    "V": "GCB_V",
    "T": "GCB_T",
    "LV": "GCB_LV",
    "LVT": "GCB_LVT",
}

INCB_CODES = {
    "None": "INCB_NONE",
    "Consonant": "INCB_CONSONANT",
    "Extend": "INCB_EXTEND",
    "Linker": "INCB_LINKER",
}


@dataclass(frozen=True)
class Range:
    start: int
    end: int
    kind: str


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_lock() -> dict[str, object]:
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    if lock.get("unicode_version") != "17.0.0":
        raise ValueError("SOURCES.lock.json must pin Unicode 17.0.0")
    return lock


def verify_sources(lock: dict[str, object]) -> None:
    source_lock = lock.get("sources")
    if not isinstance(source_lock, dict):
        raise ValueError("SOURCES.lock.json is missing sources")
    for name, metadata in source_lock.items():
        if not isinstance(name, str) or not isinstance(metadata, dict):
            raise ValueError("SOURCES.lock.json has an invalid source entry")
        expected = metadata.get("sha256")
        path = DATA / name
        if not isinstance(expected, str) or not path.is_file():
            raise ValueError("missing lock entry or source file: %s" % name)
        actual = digest(path)
        if actual != expected:
            raise ValueError("checksum mismatch for %s: expected %s, got %s" % (name, expected, actual))


def parse_range(value: str) -> tuple[int, int]:
    if ".." in value:
        start, end = value.split("..", 1)
        return int(start, 16), int(end, 16)
    point = int(value, 16)
    return point, point


def data_fields(path: Path):
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [field.strip() for field in line.split(";")]
        if len(fields) >= 2:
            yield fields


def merge_ranges(ranges: list[Range]) -> list[Range]:
    ordered = sorted(ranges, key=lambda item: (item.start, item.end, item.kind))
    merged: list[Range] = []
    for item in ordered:
        if merged and item.kind == merged[-1].kind and item.start <= merged[-1].end + 1:
            previous = merged[-1]
            merged[-1] = Range(previous.start, max(previous.end, item.end), previous.kind)
        else:
            merged.append(item)
    for previous, current in zip(merged, merged[1:]):
        if current.start <= previous.end:
            raise ValueError("overlapping property ranges: %r and %r" % (previous, current))
    return merged


def read_gcb() -> list[Range]:
    ranges: list[Range] = []
    for fields in data_fields(DATA / "GraphemeBreakProperty.txt"):
        property_name = fields[1]
        if property_name not in GCB_CODES:
            raise ValueError("unknown Grapheme_Cluster_Break value: %s" % property_name)
        start, end = parse_range(fields[0])
        ranges.append(Range(start, end, GCB_CODES[property_name]))
    return merge_ranges(ranges)


def read_incb() -> list[Range]:
    ranges: list[Range] = []
    for fields in data_fields(DATA / "DerivedCoreProperties.txt"):
        if len(fields) != 3 or fields[1] != "InCB":
            continue
        property_name = fields[2]
        if property_name not in INCB_CODES or property_name == "None":
            raise ValueError("unknown Indic_Conjunct_Break value: %s" % property_name)
        start, end = parse_range(fields[0])
        ranges.append(Range(start, end, INCB_CODES[property_name]))
    return merge_ranges(ranges)


def read_extended_pictographic() -> list[Range]:
    ranges: list[Range] = []
    for fields in data_fields(DATA / "emoji-data.txt"):
        if fields[1] != "Extended_Pictographic":
            continue
        start, end = parse_range(fields[0])
        ranges.append(Range(start, end, "1"))
    return merge_ranges(ranges)


def render_ranges(name: str, shape: str, ranges: list[Range]) -> str:
    rows = ",\n".join(
        "    %s(start = 0x%X:u32, end = 0x%X:u32, kind = %s)" %
        (shape, item.start, item.end, item.kind)
        for item in ranges
    )
    return (
        "const %s_COUNT: usize = %d:usize\n" % (name, len(ranges)) +
        "const %s: [%s; %d] = [\n%s\n]\n" % (name, shape, len(ranges), rows)
    )


def render_tables(gcb: list[Range], incb: list[Range], pictographic: list[Range]) -> str:
    return """// Generated by official/unicode/tools/generate_tables.py. DO NOT EDIT.
// Source: Unicode 17.0.0, UAX #29 revision 47.

import core/types::{Char32, usize}

pub const GCB_OTHER: i32 = 0:i32
pub const GCB_CR: i32 = 1:i32
pub const GCB_LF: i32 = 2:i32
pub const GCB_CONTROL: i32 = 3:i32
pub const GCB_EXTEND: i32 = 4:i32
pub const GCB_ZWJ: i32 = 5:i32
pub const GCB_REGIONAL_INDICATOR: i32 = 6:i32
pub const GCB_PREPEND: i32 = 7:i32
pub const GCB_SPACING_MARK: i32 = 8:i32
pub const GCB_L: i32 = 9:i32
pub const GCB_V: i32 = 10:i32
pub const GCB_T: i32 = 11:i32
pub const GCB_LV: i32 = 12:i32
pub const GCB_LVT: i32 = 13:i32

pub const INCB_NONE: i32 = 0:i32
pub const INCB_CONSONANT: i32 = 1:i32
pub const INCB_EXTEND: i32 = 2:i32
pub const INCB_LINKER: i32 = 3:i32

shape GcbRange(start: Char32, end: Char32, kind: i32)
shape IncbRange(start: Char32, end: Char32, kind: i32)
shape CodepointRange(start: Char32, end: Char32, kind: i32)

""" + render_ranges("GCB_RANGES", "GcbRange", gcb) + "\n" + render_ranges("INCB_RANGES", "IncbRange", incb) + "\n" + render_ranges("EXTENDED_PICTOGRAPHIC_RANGES", "CodepointRange", pictographic) + """
pub fn grapheme_break_property(cp: Char32) -> i32 {
    auto low#: usize = 0:usize
    auto high#: usize = GCB_RANGES_COUNT
    loop low < high {
        auto middle = low + ((high - low) / 2:usize)
        auto range = GCB_RANGES[middle]
        if cp < range.start {
            high = middle
        } else if cp > range.end {
            low = middle + 1:usize
        } else {
            return range.kind
        }
    }
    return GCB_OTHER
}

pub fn indic_conjunct_break_property(cp: Char32) -> i32 {
    auto low#: usize = 0:usize
    auto high#: usize = INCB_RANGES_COUNT
    loop low < high {
        auto middle = low + ((high - low) / 2:usize)
        auto range = INCB_RANGES[middle]
        if cp < range.start {
            high = middle
        } else if cp > range.end {
            low = middle + 1:usize
        } else {
            return range.kind
        }
    }
    return INCB_NONE
}

pub fn is_extended_pictographic(cp: Char32) -> bool {
    auto low#: usize = 0:usize
    auto high#: usize = EXTENDED_PICTOGRAPHIC_RANGES_COUNT
    loop low < high {
        auto middle = low + ((high - low) / 2:usize)
        auto range = EXTENDED_PICTOGRAPHIC_RANGES[middle]
        if cp < range.start {
            high = middle
        } else if cp > range.end {
            low = middle + 1:usize
        } else {
            return true
        }
    }
    return false
}
"""


def utf8_len(codepoint: int) -> int:
    if codepoint <= 0x7F:
        return 1
    if codepoint <= 0x7FF:
        return 2
    if codepoint <= 0xFFFF:
        return 3
    return 4


def read_corpus() -> list[tuple[list[int], list[int], list[int]]]:
    cases: list[tuple[list[int], list[int], list[int]]] = []
    for raw in (DATA / "GraphemeBreakTest.txt").read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        scalars: list[int] = []
        boundaries: list[int] = []
        non_boundaries: list[int] = []
        for token in line.split():
            if token == "÷":
                boundaries.append(len(scalars))
            elif token == "×":
                non_boundaries.append(len(scalars))
            else:
                scalars.append(int(token, 16))
        if not boundaries or boundaries[0] != 0 or boundaries[-1] != len(scalars):
            raise ValueError("malformed GraphemeBreakTest case: %s" % raw)
        cases.append((scalars, boundaries, non_boundaries))
    if not cases:
        raise ValueError("GraphemeBreakTest.txt contains no cases")
    return cases


def byte_offsets(scalars: list[int]) -> list[int]:
    offsets = [0]
    for scalar in scalars:
        offsets.append(offsets[-1] + utf8_len(scalar))
    return offsets


def render_case(case_id: int, scalars: list[int], boundaries: list[int], non_boundaries: list[int]) -> str:
    offsets = byte_offsets(scalars)
    body = ["fn corpus_case_%d() -> bool {" % case_id, "    auto text# = string::from(\"\")"]
    for scalar in scalars:
        body.append("    text#.push_codepoint(0x%X:Char32)" % scalar)
    body.extend([
        "    auto count_result = grapheme_count(text.as_str())",
        "    if count_result.is_err() { return false }",
        "    if count_result.unwrap() != %d:usize { return false }" % (len(boundaries) - 1),
    ])
    for index, scalar_index in enumerate(boundaries):
        body.extend([
            "    auto offset_result_%d = grapheme_byte_offset(text.as_str(), %d:usize)" % (index, index),
            "    if offset_result_%d.is_err() { return false }" % index,
            "    auto offset_%d = offset_result_%d.unwrap()" % (index, index),
            "    match cede offset_%d {" % index,
            "        auto Option<usize>::Some('value) => { if 'value != %d:usize { return false } }" % offsets[scalar_index],
            "        Option<usize>::None => return false",
            "    }",
            "    auto index_result_%d = grapheme_index_at_byte_offset(text.as_str(), %d:usize)" % (index, offsets[scalar_index]),
            "    if index_result_%d.is_err() { return false }" % index,
            "    auto recovered_%d = index_result_%d.unwrap()" % (index, index),
            "    match cede recovered_%d {" % index,
            "        auto Option<usize>::Some('value) => { if 'value != %d:usize { return false } }" % index,
            "        Option<usize>::None => return false",
            "    }",
        ])
    for index, scalar_index in enumerate(non_boundaries):
        offset = offsets[scalar_index]
        body.extend([
            "    auto non_boundary_result_%d = grapheme_index_at_byte_offset(text.as_str(), %d:usize)" % (index, offset),
            "    if non_boundary_result_%d.is_err() { return false }" % index,
            "    if non_boundary_result_%d.unwrap().is_some() { return false }" % index,
        ])
    body.extend(["    return true", "}", ""])
    return "\n".join(body)


def render_corpus(cases: list[tuple[list[int], list[int], list[int]]]) -> str:
    output = [
        "// Generated by official/unicode/tools/generate_tables.py. DO NOT EDIT.",
        "// Source: Unicode 17.0.0 GraphemeBreakTest.txt.",
        "",
        "import official/unicode::{grapheme_count, grapheme_byte_offset, grapheme_index_at_byte_offset}",
        "import core/string::{string}",
        "import core/types::{Char32}",
        "",
    ]
    for case_id, (scalars, boundaries, non_boundaries) in enumerate(cases):
        output.append(render_case(case_id, scalars, boundaries, non_boundaries))
    output.append("fn main() -> i32 {")
    for case_id in range(len(cases)):
        output.append("    if !corpus_case_%d() { return %d }" % (case_id, case_id + 1))
    output.extend(["    return 0", "}", ""])
    return "\n".join(output)


def write_or_check(path: Path, content: str, check: bool) -> None:
    if check:
        if not path.is_file() or path.read_text(encoding="utf-8") != content:
            raise ValueError("generated output is stale: %s" % path.relative_to(PACKAGE))
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify sources and generated output without writing")
    arguments = parser.parse_args()
    try:
        lock = load_lock()
        verify_sources(lock)
        write_or_check(TABLES, render_tables(read_gcb(), read_incb(), read_extended_pictographic()), arguments.check)
        write_or_check(CORPUS, render_corpus(read_corpus()), arguments.check)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print("FAIL: %s" % error, file=sys.stderr)
        return 1
    print("unicode-17.0.0 tables and corpus are %s" % ("current" if arguments.check else "generated"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
