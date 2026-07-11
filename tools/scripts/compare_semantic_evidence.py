#!/usr/bin/env python3
import argparse
import json
import os
import sys


def load_records(path, consumer_name):
    with open(path, "r", encoding="utf-8") as handle:
        document = json.load(handle)
    if document.get("schema") != "toka.semantic-evidence":
        raise ValueError(f"{path}: unexpected semantic evidence schema")
    if document.get("version") != 1:
        raise ValueError(f"{path}: unsupported semantic evidence version")

    records = []
    for record in document.get("records", []):
        primary_file = record.get("primary_location", {}).get("file", "")
        if os.path.basename(primary_file) != consumer_name:
            continue
        primary = record.get("primary_location", {})
        records.append(
            (
                record.get("rule", ""),
                record.get("operation", ""),
                record.get("decision", ""),
                record.get("reason", ""),
                record.get("subject", ""),
                record.get("origin", ""),
                primary.get("line", 0),
                primary.get("column", 0),
            )
        )
    return sorted(records)


def expected_rules(consumer_path):
    rules = set()
    with open(consumer_path, "r", encoding="utf-8") as handle:
        for line in handle:
            marker = "EXPECT_EVIDENCE:"
            if marker not in line:
                continue
            rules.update(line.split(marker, 1)[1].strip().split())
    return rules


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source_evidence")
    parser.add_argument("interface_evidence")
    parser.add_argument("consumer")
    args = parser.parse_args()

    consumer_name = os.path.basename(args.consumer)
    try:
        source = load_records(args.source_evidence, consumer_name)
        interface = load_records(args.interface_evidence, consumer_name)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"semantic evidence error: {exc}", file=sys.stderr)
        return 1

    if source != interface:
        print("source/interface semantic evidence mismatch", file=sys.stderr)
        print(f"source:    {source}", file=sys.stderr)
        print(f"interface: {interface}", file=sys.stderr)
        return 1

    required = expected_rules(args.consumer)
    present = {record[0] for record in source}
    missing = sorted(required - present)
    if missing:
        print(
            "missing required semantic evidence: " + ", ".join(missing),
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
