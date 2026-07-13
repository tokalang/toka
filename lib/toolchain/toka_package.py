#!/usr/bin/env python3
"""Deterministic source-package resolver used by the Toka CLI."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.error
import urllib.request

from toka_safe_extract import ExtractionError, safe_extract


LOCK_HEADER = "toka-lock-v1"
MAX_DOWNLOAD_SIZE = 256 * 1024 * 1024
ALIAS_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_-]*$")
GIT_RE = re.compile(
    r'^Git\(\s*"([^"]+)"\s*(?:,\s*(commit|tag|branch)\s*=\s*"([^"]+)")?\s*\)$'
)


class PackageError(RuntimeError):
    pass


@dataclass(frozen=True)
class Dependency:
    alias: str
    kind: str
    locator: str
    selector: str

    def fingerprint(self) -> tuple[str, str, str]:
        return self.kind, self.locator, self.selector


@dataclass
class LockEntry:
    alias: str
    kind: str
    locator: str
    resolved: str
    archive_sha256: str
    content_sha256: str
    dependencies: list[str]

    def line(self) -> str:
        dependencies = ",".join(sorted(self.dependencies)) or "-"
        fields = (
            "package",
            self.alias,
            self.kind,
            self.locator,
            self.resolved,
            self.archive_sha256,
            self.content_sha256,
            dependencies,
        )
        for field in fields:
            _validate_lock_field(field)
        return "\t".join(fields)


def _validate_lock_field(value: str) -> None:
    if not value or any(character in value for character in ("\t", "\n", "\r", "\x00")):
        raise PackageError("lock field contains forbidden characters")


def _strip_comments(text: str) -> str:
    output: list[str] = []
    in_string = False
    escaped = False
    index = 0
    while index < len(text):
        character = text[index]
        if in_string:
            output.append(character)
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                in_string = False
            index += 1
            continue
        if character == '"':
            in_string = True
            output.append(character)
            index += 1
            continue
        if character == "/" and index + 1 < len(text) and text[index + 1] == "/":
            while index < len(text) and text[index] != "\n":
                index += 1
            continue
        output.append(character)
        index += 1
    if in_string:
        raise PackageError("unterminated string in package.tk")
    return "".join(output)


def _dependency_block(text: str) -> tuple[int, int, str]:
    match = re.search(r"\bdependencies\s*=\s*\(", text)
    if not match:
        return -1, -1, ""
    open_index = text.find("(", match.start())
    depth = 0
    in_string = False
    escaped = False
    in_comment = False
    index = open_index
    while index < len(text):
        character = text[index]
        if in_comment:
            if character == "\n":
                in_comment = False
            index += 1
            continue
        if in_string:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                in_string = False
            index += 1
            continue
        if character == "/" and index + 1 < len(text) and text[index + 1] == "/":
            in_comment = True
            index += 2
            continue
        if character == '"':
            in_string = True
        elif character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
            if depth == 0:
                return open_index + 1, index, text[open_index + 1 : index]
        index += 1
    raise PackageError("unterminated dependencies block in package.tk")


def _split_entries(block: str) -> list[str]:
    entries: list[str] = []
    start = 0
    depth = 0
    in_string = False
    escaped = False
    for index, character in enumerate(block):
        if in_string:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                in_string = False
            continue
        if character == '"':
            in_string = True
        elif character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
            if depth < 0:
                raise PackageError("invalid dependency expression")
        elif character == "," and depth == 0:
            item = block[start:index].strip()
            if item:
                entries.append(item)
            start = index + 1
    item = block[start:].strip()
    if item:
        entries.append(item)
    return entries


def _decode_string(value: str) -> str:
    try:
        decoded = json.loads(value)
    except json.JSONDecodeError as error:
        raise PackageError("invalid dependency string: " + value) from error
    if not isinstance(decoded, str):
        raise PackageError("dependency value must be a string")
    return decoded


def _git_locator(url: str, ref_kind: str, selector: str) -> str:
    if any(character in url for character in ("\t", "\n", "\r", "\x00")):
        raise PackageError("invalid Git locator")
    return "%s#%s=%s" % (url, ref_kind, urllib.parse.quote(selector, safe="._/-"))


def _parse_dependency(alias: str, expression: str, base: Path) -> Dependency:
    if not ALIAS_RE.fullmatch(alias):
        raise PackageError("invalid dependency alias: " + alias)
    expression = expression.strip()
    git = GIT_RE.fullmatch(expression)
    if git:
        url, ref_kind, selector = git.groups()
        ref_kind = ref_kind or "head"
        selector = selector or "HEAD"
        return Dependency(alias, "git", _git_locator(url, ref_kind, selector), selector)

    if not (expression.startswith('"') and expression.endswith('"')):
        raise PackageError("dependency must be a string or Git(...): " + alias)
    value = _decode_string(expression)
    if value.startswith(".") or value.startswith("/"):
        path = Path(value)
        if not path.is_absolute():
            path = base / path
        return Dependency(alias, "path", str(path.resolve()), "-")

    if "/" in value and ":" in value:
        url, selector = value.rsplit(":", 1)
        if not selector:
            raise PackageError("Git dependency has an empty ref: " + alias)
        if "://" not in url:
            url = "https://" + url
        return Dependency(alias, "git", _git_locator(url, "tag", selector), selector)

    package_name = alias
    selector = value
    if ":" in value:
        package_name, selector = value.rsplit(":", 1)
    if not selector:
        raise PackageError("registry dependency has an empty version: " + alias)
    if selector != "latest":
        selector = selector.lstrip("v")
    return Dependency(alias, "registry", package_name, selector)


def parse_manifest(path: Path) -> list[Dependency]:
    try:
        original = path.read_text(encoding="utf-8")
    except OSError as error:
        raise PackageError("cannot read manifest: " + str(path)) from error
    text = _strip_comments(original)
    _, _, block = _dependency_block(text)
    if not block:
        return []
    dependencies: list[Dependency] = []
    seen: set[str] = set()
    for item in _split_entries(block):
        if "=" not in item:
            raise PackageError("invalid dependency entry: " + item)
        alias, expression = item.split("=", 1)
        alias = alias.strip()
        if alias in seen:
            raise PackageError("duplicate dependency alias: " + alias)
        seen.add(alias)
        dependencies.append(_parse_dependency(alias, expression, path.parent))
    return dependencies


def tree_sha256(root: Path) -> str:
    root = root.resolve()
    if not root.is_dir():
        raise PackageError("package root is not a directory: " + str(root))
    digest = hashlib.sha256()
    records: list[tuple[str, Path]] = []
    for directory, names, files in os.walk(root, topdown=True, followlinks=False):
        directory_path = Path(directory)
        names[:] = sorted(
            name for name in names
            if not (directory_path == root and name in (".git", ".toka"))
        )
        for name in names:
            path = directory_path / name
            if path.is_symlink():
                raise PackageError("package contains a symbolic link: " + str(path))
        for name in sorted(files):
            path = directory_path / name
            relative = path.relative_to(root).as_posix()
            if relative == "package.lock":
                continue
            if path.is_symlink() or not path.is_file():
                raise PackageError("package contains a non-regular file: " + relative)
            records.append((relative, path))

    for relative, path in sorted(records):
        encoded = relative.encode("utf-8")
        size = path.stat().st_size
        digest.update(len(encoded).to_bytes(8, "big"))
        digest.update(encoded)
        digest.update(size.to_bytes(8, "big"))
        with path.open("rb") as source:
            while True:
                chunk = source.read(1024 * 1024)
                if not chunk:
                    break
                digest.update(chunk)
    return digest.hexdigest()


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while True:
            chunk = source.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def read_lock(path: Path) -> dict[str, LockEntry]:
    if not path.is_file():
        return {}
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines or lines[0] != LOCK_HEADER:
        raise PackageError("unsupported or malformed package.lock")
    entries: dict[str, LockEntry] = {}
    previous = ""
    for line in lines[1:]:
        fields = line.split("\t")
        if len(fields) != 8 or fields[0] != "package":
            raise PackageError("malformed package.lock entry")
        _, alias, kind, locator, resolved, archive_hash, content_hash, dependencies = fields
        if not ALIAS_RE.fullmatch(alias) or kind not in ("path", "git", "registry"):
            raise PackageError("invalid package.lock entry")
        if alias in entries or (previous and alias <= previous):
            raise PackageError("package.lock entries are duplicate or unsorted")
        if not re.fullmatch(r"[0-9a-f]{64}", content_hash):
            raise PackageError("invalid package content hash")
        if archive_hash != "-" and not re.fullmatch(r"[0-9a-f]{64}", archive_hash):
            raise PackageError("invalid package archive hash")
        deps = [] if dependencies == "-" else dependencies.split(",")
        if deps != sorted(set(deps)) or any(not ALIAS_RE.fullmatch(dep) for dep in deps):
            raise PackageError("invalid locked dependency list")
        entry = LockEntry(alias, kind, locator, resolved, archive_hash, content_hash, deps)
        entry.line()
        entries[alias] = entry
        previous = alias
    return entries


def encode_lock(entries: dict[str, LockEntry]) -> str:
    lines = [LOCK_HEADER]
    lines.extend(entries[alias].line() for alias in sorted(entries))
    return "\n".join(lines) + "\n"


def atomic_write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as output:
            output.write(content)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise


def _run(command: list[str], cwd: Path | None = None) -> str:
    environment = os.environ.copy()
    environment["GIT_TERMINAL_PROMPT"] = "0"
    try:
        result = subprocess.run(
            command,
            cwd=str(cwd) if cwd else None,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=120,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise PackageError("failed to execute %s: %s" % (command[0], error)) from error
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise PackageError("command failed (%s): %s" % (command[0], detail))
    return result.stdout.strip()


def _download(url: str, destination: Path) -> None:
    request = urllib.request.Request(url, headers={"User-Agent": "toka-package/1"})
    try:
        with urllib.request.urlopen(request, timeout=30) as response, destination.open("xb") as output:
            total = 0
            while True:
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                total += len(chunk)
                if total > MAX_DOWNLOAD_SIZE:
                    raise PackageError("package download size limit exceeded")
                output.write(chunk)
    except (OSError, urllib.error.URLError) as error:
        raise PackageError("package download failed: " + str(error)) from error


def _version_key(value: str) -> tuple[object, ...]:
    parts: list[object] = []
    for part in re.split(r"[.-]", value.lstrip("v")):
        parts.append((0, int(part)) if part.isdigit() else (1, part))
    return tuple(parts)


class Resolver:
    def __init__(
        self,
        manifest: Path,
        lock_path: Path,
        state: Path,
        *,
        offline: bool,
        refresh: bool,
    ) -> None:
        self.manifest = manifest.resolve()
        self.project = self.manifest.parent
        self.lock_path = lock_path.resolve()
        self.state = state.resolve()
        self.offline = offline
        self.refresh = refresh
        self.old_lock = read_lock(self.lock_path)
        self.entries: dict[str, LockEntry] = {}
        self.requests: dict[str, tuple[str, str, str]] = {}
        self.active: list[str] = []
        self.roots: dict[str, Path] = {}
        self.candidates: dict[Path, Path] = {}
        self.transaction = Path()

    def _install_path(self, entry: LockEntry) -> Path:
        if entry.kind == "path":
            return Path(entry.locator)
        suffix = entry.resolved if entry.kind == "registry" else entry.resolved[:12]
        return self.state / "packages" / (entry.alias + "-" + suffix)

    def _locked_for(self, dependency: Dependency) -> LockEntry | None:
        if self.refresh:
            return None
        entry = self.old_lock.get(dependency.alias)
        if not entry or entry.kind != dependency.kind or entry.locator != dependency.locator:
            return None
        if dependency.kind == "registry" and dependency.selector != "latest":
            if entry.resolved != dependency.selector.lstrip("v"):
                return None
        return entry

    def _registry_version(self, dependency: Dependency) -> str:
        if dependency.selector != "latest":
            return dependency.selector.lstrip("v")
        if self.offline:
            raise PackageError("offline resolution requires a locked version: " + dependency.alias)
        base = os.environ.get("TOKA_REGISTRY_URL", "http://localhost:8080").rstrip("/")
        catalog_url = base + "/catalog.json"
        temporary = self.transaction / (dependency.alias + ".catalog.json")
        _download(catalog_url, temporary)
        try:
            data = json.loads(temporary.read_text(encoding="utf-8"))
            versions = [
                str(item["version"]).lstrip("v")
                for item in data.get("packages", [])
                if item.get("name") == dependency.locator and item.get("version")
            ]
        except (OSError, ValueError, TypeError, KeyError) as error:
            raise PackageError("registry catalog is malformed") from error
        if not versions:
            raise PackageError("registry package has no resolvable version: " + dependency.locator)
        return max(versions, key=_version_key)

    def _registry_archive(self, dependency: Dependency, version: str, locked: LockEntry | None) -> tuple[Path, str]:
        cache = self.state / "cache" / "archives"
        cache.mkdir(parents=True, exist_ok=True)
        if locked and locked.archive_sha256 != "-":
            cached = cache / (locked.archive_sha256 + ".tar.gz")
            if cached.is_file() and file_sha256(cached) == locked.archive_sha256:
                return cached, locked.archive_sha256
            if self.offline:
                raise PackageError("offline archive is missing or corrupt: " + dependency.alias)

        if self.offline:
            raise PackageError("offline archive is not locked: " + dependency.alias)
        safe_name = dependency.locator.rsplit("/", 1)[-1]
        base = os.environ.get("TOKA_REGISTRY_URL", "http://localhost:8080").rstrip("/")
        url = "%s/%s-%s.tar.gz" % (base, safe_name, version)
        downloaded = self.transaction / (dependency.alias + ".download.tar.gz")
        _download(url, downloaded)
        digest = file_sha256(downloaded)
        if locked and digest != locked.archive_sha256:
            raise PackageError("downloaded archive does not match package.lock: " + dependency.alias)
        cached = cache / (digest + ".tar.gz")
        if not cached.exists():
            os.replace(downloaded, cached)
        return cached, digest

    def _materialize_registry(self, dependency: Dependency, locked: LockEntry | None) -> tuple[LockEntry, Path]:
        version = locked.resolved if locked else self._registry_version(dependency)
        placeholder = LockEntry(dependency.alias, "registry", dependency.locator, version, "-", "0" * 64, [])
        target = self._install_path(placeholder)
        if locked and target.is_dir():
            actual = tree_sha256(target)
            if actual != locked.content_sha256:
                raise PackageError("installed package does not match package.lock: " + dependency.alias)
            return locked, target

        archive, archive_hash = self._registry_archive(dependency, version, locked)
        extraction = self.transaction / (dependency.alias + ".extract")
        safe_extract(archive, extraction)
        root = extraction
        children = list(extraction.iterdir())
        if not (extraction / "package.tk").is_file() and len(children) == 1 and children[0].is_dir():
            root = children[0]
        if not (root / "package.tk").is_file():
            raise PackageError("package archive has no package.tk: " + dependency.alias)
        content_hash = tree_sha256(root)
        if locked and content_hash != locked.content_sha256:
            raise PackageError("extracted package does not match package.lock: " + dependency.alias)
        entry = LockEntry(dependency.alias, "registry", dependency.locator, version, archive_hash, content_hash, [])
        if target.exists():
            if not target.is_dir() or tree_sha256(target) != content_hash:
                raise PackageError("package target already exists with different content: " + str(target))
            return entry, target
        self.candidates[target] = root
        return entry, root

    def _split_git_locator(self, locator: str) -> tuple[str, str, str]:
        if "#" not in locator or "=" not in locator.rsplit("#", 1)[1]:
            raise PackageError("malformed Git locator")
        url, encoded = locator.rsplit("#", 1)
        ref_kind, selector = encoded.split("=", 1)
        return url, ref_kind, urllib.parse.unquote(selector)

    def _materialize_git(self, dependency: Dependency, locked: LockEntry | None) -> tuple[LockEntry, Path]:
        url, ref_kind, selector = self._split_git_locator(dependency.locator)
        if locked:
            target = self._install_path(locked)
            if target.is_dir():
                actual = tree_sha256(target)
                if actual != locked.content_sha256:
                    raise PackageError("installed Git package does not match package.lock: " + dependency.alias)
                return locked, target
            if self.offline:
                raise PackageError("offline Git package is missing: " + dependency.alias)
        elif self.offline:
            raise PackageError("offline Git dependency is not locked: " + dependency.alias)

        checkout = self.transaction / (dependency.alias + ".git")
        if locked or ref_kind == "commit":
            revision = locked.resolved if locked else selector
            checkout.mkdir()
            _run(["git", "init", "--quiet"], checkout)
            _run(["git", "remote", "add", "origin", url], checkout)
            _run(["git", "fetch", "--quiet", "--depth", "1", "origin", revision], checkout)
            _run(["git", "checkout", "--quiet", "--detach", "FETCH_HEAD"], checkout)
        else:
            command = ["git", "clone", "--quiet", "--depth", "1"]
            if ref_kind != "head":
                command.extend(["--branch", selector])
            command.extend([url, str(checkout)])
            _run(command)
        resolved = _run(["git", "rev-parse", "HEAD"], checkout)
        if not re.fullmatch(r"[0-9a-fA-F]{40,64}", resolved):
            raise PackageError("Git did not resolve an immutable commit")
        resolved = resolved.lower()
        if locked and resolved != locked.resolved:
            raise PackageError("Git checkout does not match package.lock: " + dependency.alias)
        shutil.rmtree(checkout / ".git", ignore_errors=True)
        if not (checkout / "package.tk").is_file():
            raise PackageError("Git package has no package.tk: " + dependency.alias)
        content_hash = tree_sha256(checkout)
        if locked and content_hash != locked.content_sha256:
            raise PackageError("Git content does not match package.lock: " + dependency.alias)
        entry = LockEntry(dependency.alias, "git", dependency.locator, resolved, "-", content_hash, [])
        target = self._install_path(entry)
        if target.exists():
            if not target.is_dir() or tree_sha256(target) != content_hash:
                raise PackageError("Git target already exists with different content: " + str(target))
            return entry, target
        self.candidates[target] = checkout
        return entry, checkout

    def _materialize(self, dependency: Dependency) -> tuple[LockEntry, Path]:
        locked = self._locked_for(dependency)
        if dependency.kind == "path":
            root = Path(dependency.locator)
            if not (root / "package.tk").is_file():
                raise PackageError("path package has no package.tk: " + dependency.alias)
            digest = tree_sha256(root)
            entry = LockEntry(dependency.alias, "path", dependency.locator, dependency.locator, "-", digest, [])
            return entry, root
        if dependency.kind == "git":
            return self._materialize_git(dependency, locked)
        return self._materialize_registry(dependency, locked)

    def _resolve(self, dependency: Dependency) -> None:
        if dependency.alias in self.active:
            start = self.active.index(dependency.alias)
            chain = self.active[start:] + [dependency.alias]
            raise PackageError("dependency cycle: " + " -> ".join(chain))
        fingerprint = dependency.fingerprint()
        if dependency.alias in self.requests:
            if self.requests[dependency.alias] != fingerprint:
                raise PackageError("dependency conflict for alias: " + dependency.alias)
            return
        self.requests[dependency.alias] = fingerprint
        self.active.append(dependency.alias)
        entry, root = self._materialize(dependency)
        children = parse_manifest(root / "package.tk")
        for child in children:
            self._resolve(child)
        entry.dependencies = sorted(child.alias for child in children)
        self.entries[dependency.alias] = entry
        self.roots[dependency.alias] = root
        self.active.pop()

    def run(self) -> dict[str, LockEntry]:
        self.state.mkdir(parents=True, exist_ok=True)
        staging = self.state / "staging"
        staging.mkdir(parents=True, exist_ok=True)
        transaction_path = tempfile.mkdtemp(prefix="resolve-", dir=staging)
        self.transaction = Path(transaction_path)
        committed: list[Path] = []
        try:
            for dependency in parse_manifest(self.manifest):
                self._resolve(dependency)
            for target, source in sorted(self.candidates.items(), key=lambda item: str(item[0])):
                target.parent.mkdir(parents=True, exist_ok=True)
                if target.exists():
                    continue
                os.replace(source, target)
                committed.append(target)
            encoded = encode_lock(self.entries)
            atomic_write(self.lock_path, encoded)
            return self.entries
        except BaseException:
            for target in reversed(committed):
                shutil.rmtree(target, ignore_errors=True)
            raise
        finally:
            shutil.rmtree(self.transaction, ignore_errors=True)


def package_root(entry: LockEntry, state: Path) -> Path:
    if entry.kind == "path":
        return Path(entry.locator)
    suffix = entry.resolved if entry.kind == "registry" else entry.resolved[:12]
    return state / "packages" / (entry.alias + "-" + suffix)


def compiler_mappings(lock_path: Path, state: Path) -> list[str]:
    mappings: list[str] = []
    for alias, entry in sorted(read_lock(lock_path).items()):
        root = package_root(entry, state)
        if tree_sha256(root) != entry.content_sha256:
            raise PackageError("package content verification failed: " + alias)
        module = root / "lib" / alias / "mod.tk"
        if not module.is_file():
            raise PackageError("package module is missing: " + str(module))
        mappings.append(alias + "=" + str(module))
    return mappings


def remove_dependency(manifest: Path, alias: str) -> bool:
    if not ALIAS_RE.fullmatch(alias):
        raise PackageError("invalid dependency alias")
    original = manifest.read_text(encoding="utf-8")
    start, end, block = _dependency_block(original)
    if start < 0:
        raise PackageError("manifest has no dependencies block")
    kept: list[str] = []
    removed = False
    for item in _split_entries(block):
        name = item.split("=", 1)[0].strip() if "=" in item else ""
        if name == alias:
            removed = True
        else:
            kept.append(item)
    if not removed:
        return False
    indentation = "        "
    replacement = "\n" + "".join(indentation + item + ",\n" for item in kept) + "    "
    updated = original[:start] + replacement + original[end:]
    atomic_write(manifest, updated)
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    fetch = subparsers.add_parser("fetch")
    fetch.add_argument("--manifest", default="package.tk")
    fetch.add_argument("--lock", default="package.lock")
    fetch.add_argument("--state", default=".toka")
    fetch.add_argument("--offline", action="store_true")
    fetch.add_argument("--refresh", action="store_true")

    mappings = subparsers.add_parser("compiler-mappings")
    mappings.add_argument("--lock", default="package.lock")
    mappings.add_argument("--state", default=".toka")

    digest = subparsers.add_parser("hash-tree")
    digest.add_argument("path")

    extract = subparsers.add_parser("safe-extract")
    extract.add_argument("archive")
    extract.add_argument("destination")

    remove = subparsers.add_parser("remove")
    remove.add_argument("alias")
    remove.add_argument("--manifest", default="package.tk")
    remove.add_argument("--lock", default="package.lock")
    remove.add_argument("--state", default=".toka")

    args = parser.parse_args()
    try:
        if args.command == "fetch":
            resolver = Resolver(
                Path(args.manifest), Path(args.lock), Path(args.state),
                offline=args.offline, refresh=args.refresh,
            )
            entries = resolver.run()
            print("resolved %d package(s)" % len(entries))
        elif args.command == "compiler-mappings":
            for mapping in compiler_mappings(Path(args.lock), Path(args.state)):
                print(mapping)
        elif args.command == "hash-tree":
            print(tree_sha256(Path(args.path)))
        elif args.command == "safe-extract":
            safe_extract(Path(args.archive), Path(args.destination))
        elif args.command == "remove":
            manifest = Path(args.manifest)
            original = manifest.read_bytes()
            if not remove_dependency(manifest, args.alias):
                raise PackageError("dependency not found: " + args.alias)
            try:
                Resolver(manifest, Path(args.lock), Path(args.state), offline=False, refresh=False).run()
            except BaseException:
                manifest.write_bytes(original)
                raise
        return 0
    except (ExtractionError, PackageError, OSError) as error:
        sys.stderr.write("package error: %s\n" % error)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
