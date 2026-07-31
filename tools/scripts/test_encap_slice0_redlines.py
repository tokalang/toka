#!/usr/bin/env python3
"""Audit-only expected-result redlines for the proposed @encap epoch.

This script deliberately models the RFC without consulting Sema.  A passing
run proves that the Slice 0 expected results are deterministic; it does not
activate any proposed language rule.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class Coordinate:
    crate: str | None
    path: tuple[str, ...] = ()

    @property
    def known(self) -> bool:
        return self.crate is not None


@dataclass(frozen=True)
class Grant:
    kind: str
    target: Coordinate | None = None


def can_name_field(owner: Coordinate, requester: Coordinate, grants: list[Grant]) -> bool:
    """RFC 8.2, including the required fail-closed identity behaviour."""
    if not owner.known or not requester.known:
        return False
    if owner == requester:
        return True
    for grant in grants:
        if grant.kind == "global":
            return True
        if grant.kind == "crate" and owner.crate == requester.crate:
            return True
        if grant.kind == "path" and grant.target and grant.target.known:
            if (grant.target.crate == requester.crate and
                    requester.path[:len(grant.target.path)] == grant.target.path):
                return True
    return False


def access_redlines() -> None:
    owner = Coordinate("crate-a", ("device",))
    sibling = Coordinate("crate-a", ("other",))
    friend = Coordinate("crate-a", ("build", "internal"))
    descendant = Coordinate("crate-a", ("build", "internal", "codec"))
    substring = Coordinate("crate-a", ("build", "internalized"))
    foreign = Coordinate("crate-b", ("build", "internal", "codec"))
    unknown = Coordinate(None)

    assert can_name_field(owner, owner, [])
    assert can_name_field(owner, foreign, [Grant("global")])
    assert can_name_field(owner, sibling, [Grant("crate")])
    assert not can_name_field(owner, foreign, [Grant("crate")])
    path_grant = Grant("path", friend)
    assert can_name_field(owner, friend, [path_grant])
    assert can_name_field(owner, descendant, [path_grant])
    assert not can_name_field(owner, substring, [path_grant])
    assert not can_name_field(owner, foreign, [path_grant])
    assert not can_name_field(owner, unknown, [Grant("global")])
    assert not can_name_field(unknown, owner, [Grant("global")])

    # Deliberate legacy/shadow disagreements to retain in the migration log.
    disagreements = {
        "legacy pub(crate) cross-crate": (True, False),
        "legacy physical suffix path": (True, False),
        "legacy unknown module identity": (True, False),
    }
    assert all(legacy and not shadow for legacy, shadow in disagreements.values())


@dataclass(frozen=True)
class CopyNode:
    name: str
    fields: tuple[str, ...] = ()
    leaf: str = ""


def copy_proof(nodes: dict[str, CopyNode], root: str) -> tuple[str, tuple[str, ...]]:
    """Small three-state by-value graph model used for redline expectations."""
    state: dict[str, str] = {}
    visiting: list[str] = []

    def visit(name: str) -> tuple[str, tuple[str, ...]]:
        if name in visiting:
            return "layout-error", tuple(visiting[visiting.index(name):] + [name])
        if name in state:
            return state[name], (name,)
        node = nodes[name]
        if node.leaf:
            state[name] = node.leaf
            return node.leaf, (name,)
        visiting.append(name)
        for field in node.fields:
            result, path = visit(field)
            if result != "copy":
                visiting.pop()
                state[name] = result
                return result, path if result == "layout-error" else (name,) + path
        visiting.pop()
        state[name] = "copy"
        return "copy", (name,)

    return visit(root)


def copy_dup_redlines() -> None:
    nodes = {
        "Scalar": CopyNode("Scalar", leaf="copy"),
        "Resource": CopyNode("Resource", leaf="non-copy"),
        "Opaque": CopyNode("Opaque", leaf="unknown"),
        "Point": CopyNode("Point", ("Scalar", "Scalar")),
        "Pair": CopyNode("Pair", ("Scalar", "Resource")),
        "OpaqueBox": CopyNode("OpaqueBox", ("Opaque",)),
        "Recursive": CopyNode("Recursive", ("Recursive",)),
    }
    assert copy_proof(nodes, "Point") == ("copy", ("Point",))
    assert copy_proof(nodes, "Pair") == ("non-copy", ("Pair", "Resource"))
    assert copy_proof(nodes, "OpaqueBox") == ("unknown", ("OpaqueBox", "Opaque"))
    assert copy_proof(nodes, "Recursive") == (
        "layout-error", ("Recursive", "Recursive"))

    def copy_recipe(bound: str) -> str:
        return "all:@Copy" if bound == "@Copy" else "unknown"

    assert copy_recipe("@Copy") == "all:@Copy"
    assert copy_recipe("@Dup") == "unknown"
    assert copy_recipe("") == "unknown"

    def select_dup(copy_witness: bool, user_provider: bool) -> str:
        providers = int(copy_witness) + int(user_provider)
        return "intrinsic" if providers == 1 and copy_witness else (
            "user" if providers == 1 else "none" if providers == 0 else "overlap")

    assert select_dup(True, False) == "intrinsic"
    assert select_dup(False, True) == "user"
    assert select_dup(False, False) == "none"
    assert select_dup(True, True) == "overlap"


def lifecycle_resource_redlines() -> None:
    def custom_drop(fields: tuple[str, ...], live: set[str]) -> list[str]:
        return ["hook"] + ["drop:" + field for field in fields if field in live]

    assert custom_drop(("first", "second"), {"first", "second"}) == [
        "hook", "drop:first", "drop:second"]
    assert custom_drop(("first", "second"), {"second"}) == ["hook", "drop:second"]

    def partial_cede(drop_plan: str, live: set[str], field: str) -> set[str] | None:
        if drop_plan != "structural" or field not in live:
            return None
        return live - {field}

    assert partial_cede("structural", {"left", "right"}, "left") == {"right"}
    assert partial_cede("custom", {"left", "right"}, "left") is None

    def resource_contract(source: str, ownership: str) -> str:
        if source != "validated-ffi" or ownership not in {"owned", "borrowed"}:
            return "none"
        return ownership

    assert resource_contract("raw-pointer", "owned") == "none"
    assert resource_contract("release-call", "owned") == "none"
    assert resource_contract("validated-ffi", "owned") == "owned"
    assert resource_contract("validated-ffi", "borrowed") == "borrowed"


def grant_inventory() -> None:
    """Inventory legacy path grants without treating wildcard grammar as RFC-valid."""
    policy_re = re.compile(r"pub\(([^)]+)\)\s+([^\n]+)")
    grants: list[tuple[Path, str]] = []
    wildcards: list[Path] = []
    for source in sorted((ROOT / "lib").rglob("*.tk")):
        text = source.read_text(encoding="utf-8")
        for target, _fields in policy_re.findall(text):
            grants.append((source.relative_to(ROOT), target))
        if re.search(r"^\s*pub(?:\([^)]+\))?\s+\*", text, re.MULTILINE):
            wildcards.append(source.relative_to(ROOT))

    assert grants
    assert wildcards
    assert len(grants) == 46
    assert {target for _, target in grants} == {
        "build", "build/internal/codec", "core/str", "std"}
    assert len(wildcards) == 9
    assert all(not target.startswith(("/", "..")) for _, target in grants)
    assert (Path("lib/build.tk"), "build/internal/codec") in grants
    assert Path("lib/std/vec.tk") in wildcards


def main() -> int:
    access_redlines()
    copy_dup_redlines()
    lifecycle_resource_redlines()
    grant_inventory()
    print("encap Slice 0 redlines: PASSED")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, KeyError, ValueError) as error:
        print("encap Slice 0 redlines: FAILED: %s" % error, file=sys.stderr)
        raise SystemExit(1)
