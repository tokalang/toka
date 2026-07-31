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
NATIVE_LIBRARY_RE = re.compile(r"^[A-Za-z0-9_+.-]+$")
NATIVE_FRAMEWORK_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")
NATIVE_RESOURCE_NAME_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_.-]*$")
NATIVE_TARGETS = ("macos", "linux", "windows")


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


def _static_tuple_block(text: str, field: str) -> str | None:
    """Return a parenthesized static manifest field without evaluating Toka."""
    match = re.search(r"\b" + re.escape(field) + r"\s*=\s*\(", text)
    if not match:
        return None
    open_index = text.find("(", match.start())
    depth = 0
    in_string = False
    escaped = False
    index = open_index
    while index < len(text):
        character = text[index]
        if in_string:
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
        elif character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
            if depth == 0:
                return text[open_index + 1 : index]
        index += 1
    raise PackageError("unterminated " + field + " block in package.tk")


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


def _static_string_tuple(block: str, field: str) -> tuple[str, ...]:
    values = _static_tuple_block(block, field)
    if values is None:
        return ()
    decoded: list[str] = []
    for entry in _split_entries(values):
        entry = entry.strip()
        if not (entry.startswith('"') and entry.endswith('"')):
            raise PackageError("native." + field + " must contain only strings")
        decoded.append(_decode_string(entry))
    return tuple(decoded)


def _direct_static_tuple_block(block: str, field: str) -> str | None:
    """Return a tuple field declared directly in a static tuple block."""
    for entry in _split_entries(block):
        match = re.fullmatch(
            re.escape(field) + r"\s*=\s*\((.*)\)\s*", entry,
            flags=re.DOTALL,
        )
        if match:
            return match.group(1)
    return None


def _direct_static_field_names(block: str, context: str) -> set[str]:
    fields: set[str] = set()
    for entry in _split_entries(block):
        match = re.match(r"\s*([A-Za-z_][A-Za-z0-9_]*)\s*=", entry)
        if not match:
            raise PackageError("invalid static field in " + context)
        field = match.group(1)
        if field in fields:
            raise PackageError("duplicate " + context + " field: " + field)
        fields.add(field)
    return fields


def _direct_static_string_tuple(block: str, field: str) -> tuple[str, ...]:
    values = _direct_static_tuple_block(block, field)
    if values is None:
        return ()
    decoded: list[str] = []
    for entry in _split_entries(values):
        entry = entry.strip()
        if not (entry.startswith('"') and entry.endswith('"')):
            raise PackageError("native." + field + " must contain only strings")
        decoded.append(_decode_string(entry))
    return tuple(decoded)


def _static_bool(block: str, field: str, *, default: bool) -> bool:
    match = re.search(r"\b" + re.escape(field) + r"\s*=\s*(true|false)\b", block)
    if not match:
        return default
    return match.group(1) == "true"


def _static_string(block: str, field: str) -> str | None:
    match = re.search(r"\b" + re.escape(field) + r'\s*=\s*("(?:[^"\\]|\\.)*")', block)
    if not match:
        return None
    return _decode_string(match.group(1))


def _static_tuple_entries(block: str, field: str) -> tuple[str, ...]:
    values = _direct_static_tuple_block(block, field)
    if values is None:
        return ()
    entries: list[str] = []
    for entry in _split_entries(values):
        entry = entry.strip()
        if not (entry.startswith("(") and entry.endswith(")")):
            raise PackageError("native." + field + " must contain only static tuples")
        entries.append(entry[1:-1])
    return tuple(entries)


def _native_target_block(native: str, target: str) -> str:
    if target not in NATIVE_TARGETS:
        raise PackageError("unsupported native target: " + target)
    selected = _direct_static_tuple_block(native, target)
    has_target_blocks = any(_direct_static_tuple_block(native, name) is not None
                            for name in NATIVE_TARGETS)
    if selected is None:
        if has_target_blocks:
            raise PackageError("native package does not declare required native support for target: " + target)
        return ""
    return selected


def _validate_native_manifest(native: str, target: str) -> str:
    allowed = {
        "required", "sources", "libraries", "pkg_config", "frameworks",
        "system_libraries", "ffi_resources", *NATIVE_TARGETS,
    }
    unsupported = _direct_static_field_names(native, "native") - allowed
    if unsupported:
        raise PackageError("unsupported native manifest field: " + sorted(unsupported)[0])
    selected = _native_target_block(native, target)
    target_allowed = {
        "sources", "libraries", "pkg_config", "frameworks",
        "system_libraries",
    }
    unsupported = _direct_static_field_names(selected, "native." + target) - target_allowed
    if unsupported:
        raise PackageError("unsupported native." + target + " field: " + sorted(unsupported)[0])
    return selected


def _native_resource_contracts(native: str, alias: str) -> list[dict[str, object]]:
    resources: list[dict[str, object]] = []
    for entry in _static_tuple_entries(native, "ffi_resources"):
        required_fields = {
            "name", "acquire", "release", "ownership", "nullable",
            "thread_affinity", "send",
        }
        fields = _direct_static_field_names(entry, "native.ffi_resources")
        if fields != required_fields:
            raise PackageError("native.ffi_resources requires exactly the v1 resource fields: " + alias)
        name = _static_string(entry, "name")
        acquire = _static_string(entry, "acquire")
        release = _static_string(entry, "release")
        ownership = _static_string(entry, "ownership")
        affinity = _static_string(entry, "thread_affinity")
        nullable = _static_bool(entry, "nullable", default=False)
        send = _static_bool(entry, "send", default=False)
        if (not name or not NATIVE_RESOURCE_NAME_RE.fullmatch(name) or
                not acquire or not NATIVE_RESOURCE_NAME_RE.fullmatch(acquire) or
                release is None or ownership not in ("owned", "borrowed") or
                affinity not in ("any", "ui")):
            raise PackageError("native.ffi_resources contains an invalid resource contract: " + alias)
        if ownership == "owned" and (release == "none" or
                                      not NATIVE_RESOURCE_NAME_RE.fullmatch(release)):
            raise PackageError("owned native resource requires a release symbol: " + alias)
        if ownership == "borrowed" and release != "none":
            raise PackageError("borrowed native resource must declare release = \"none\": " + alias)
        if affinity == "ui" and send:
            raise PackageError("UI-affine native resource cannot be Send: " + alias)
        resources.append({
            "name": name,
            "acquire": acquire,
            "release": release,
            "ownership": ownership,
            "nullable": nullable,
            "thread_affinity": affinity,
            "send": send,
        })
    return sorted(resources, key=lambda resource: str(resource["name"]))


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
        if kind == "path":
            if archive_hash != "-" or not Path(locator).is_absolute() or resolved != locator:
                raise PackageError("invalid locked path package: " + alias)
        elif kind == "git":
            if archive_hash != "-" or not re.fullmatch(r"[0-9a-f]{40,64}", resolved):
                raise PackageError("invalid locked Git package: " + alias)
        elif archive_hash == "-" or not resolved or resolved == "latest":
            raise PackageError("invalid locked registry package: " + alias)
        entries[alias] = entry
        previous = alias

    for alias, entry in entries.items():
        for dependency in entry.dependencies:
            if dependency == alias or dependency not in entries:
                raise PackageError("invalid locked dependency reference: " + alias + " -> " + dependency)

    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(alias: str) -> None:
        if alias in visiting:
            raise PackageError("package.lock contains a dependency cycle")
        if alias in visited:
            return
        visiting.add(alias)
        for dependency in entries[alias].dependencies:
            visit(dependency)
        visiting.remove(alias)
        visited.add(alias)

    for alias in entries:
        visit(alias)
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
        try:
            directory = os.open(path.parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
        except OSError:
            pass
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
        if cached.is_file() and file_sha256(cached) == digest:
            downloaded.unlink()
        else:
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
        retired: list[tuple[Path, Path]] = []
        try:
            for dependency in parse_manifest(self.manifest):
                self._resolve(dependency)
            for target, source in sorted(self.candidates.items(), key=lambda item: str(item[0])):
                target.parent.mkdir(parents=True, exist_ok=True)
                if target.exists():
                    continue
                os.replace(source, target)
                committed.append(target)
            current_targets = {
                self._install_path(entry)
                for entry in self.entries.values()
                if entry.kind != "path"
            }
            obsolete = sorted(
                {
                    self._install_path(entry)
                    for entry in self.old_lock.values()
                    if entry.kind != "path" and self._install_path(entry) not in current_targets
                },
                key=str,
            )
            retirement = self.transaction / "retired"
            retirement.mkdir()
            for index, target in enumerate(obsolete):
                if not target.exists():
                    continue
                backup = retirement / str(index)
                os.replace(target, backup)
                retired.append((target, backup))
            encoded = encode_lock(self.entries)
            atomic_write(self.lock_path, encoded)
            return self.entries
        except BaseException:
            for target in reversed(committed):
                shutil.rmtree(target, ignore_errors=True)
            for target, backup in reversed(retired):
                target.parent.mkdir(parents=True, exist_ok=True)
                os.replace(backup, target)
            raise
        finally:
            shutil.rmtree(self.transaction, ignore_errors=True)


def package_root(entry: LockEntry, state: Path) -> Path:
    if entry.kind == "path":
        return Path(entry.locator)
    suffix = entry.resolved if entry.kind == "registry" else entry.resolved[:12]
    return state / "packages" / (entry.alias + "-" + suffix)


def native_build_plan(lock_path: Path, state: Path, *, target: str | None = None) -> dict[str, object]:
    """Return the locked native build inputs for required source packages.

    This deliberately accepts only relative `native/*.c` or `native/*.m`
    sources, logical library names, validated macOS framework names, and
    validated system-library names. The build driver obtains library flags
    through pkg-config; package metadata is never treated as a shell fragment.
    """
    if target is None:
        if sys.platform == "darwin":
            target = "macos"
        elif sys.platform.startswith("linux"):
            target = "linux"
        elif sys.platform.startswith("win"):
            target = "windows"
        else:
            raise PackageError("cannot infer native target for this host")
    if target not in NATIVE_TARGETS:
        raise PackageError("unsupported native target: " + target)
    packages: list[dict[str, object]] = []
    for alias, entry in sorted(read_lock(lock_path).items()):
        root = package_root(entry, state).resolve()
        if tree_sha256(root) != entry.content_sha256:
            raise PackageError("package content verification failed: " + alias)
        try:
            manifest = _strip_comments((root / "package.tk").read_text(encoding="utf-8"))
        except OSError as error:
            raise PackageError("cannot read package manifest: " + alias) from error
        native = _static_tuple_block(manifest, "native")
        if native is None or not _static_bool(native, "required", default=False):
            continue
        package_targets = _static_string_tuple(manifest, "targets")
        if package_targets and target not in package_targets:
            raise PackageError("package does not support native target %s: %s" % (target, alias))
        selected = _validate_native_manifest(native, target)
        sources = (_direct_static_string_tuple(native, "sources") +
                   _direct_static_string_tuple(selected, "sources"))
        # `libraries` is the v1 spelling.  Keep it source-compatible while
        # recording the unambiguous pkg-config meaning in the canonical plan.
        pkg_config = (_direct_static_string_tuple(native, "libraries") +
                      _direct_static_string_tuple(native, "pkg_config") +
                      _direct_static_string_tuple(selected, "libraries") +
                      _direct_static_string_tuple(selected, "pkg_config"))
        frameworks = (_direct_static_string_tuple(native, "frameworks") +
                      _direct_static_string_tuple(selected, "frameworks"))
        system_libraries = (_direct_static_string_tuple(native, "system_libraries") +
                            _direct_static_string_tuple(selected, "system_libraries"))
        if not sources and not pkg_config and not frameworks and not system_libraries:
            raise PackageError("required native package has no sources or native dependencies: " + alias)
        absolute_sources: list[str] = []
        for source in sources:
            relative = Path(source)
            if (not source or relative.is_absolute() or ".." in relative.parts or
                    not relative.parts or relative.parts[0] != "native" or
                    relative.suffix not in (".c", ".m")):
                raise PackageError("native.sources must use relative native/*.c or native/*.m paths: " + alias)
            if target != "macos" and relative.suffix == ".m":
                raise PackageError("Objective-C native source requires macos target: " + alias)
            resolved = (root / relative).resolve()
            try:
                resolved.relative_to(root)
            except ValueError as error:
                raise PackageError("native source escapes package root: " + alias) from error
            if not resolved.is_file() or resolved.is_symlink():
                raise PackageError("native source is not a regular file: " + str(relative))
            absolute_sources.append(str(resolved))
        for library in pkg_config:
            if not NATIVE_LIBRARY_RE.fullmatch(library):
                raise PackageError("native pkg-config library name is invalid: " + alias)
        for framework in frameworks:
            if target != "macos":
                raise PackageError("native frameworks are supported only on macos: " + alias)
            if not NATIVE_FRAMEWORK_RE.fullmatch(framework):
                raise PackageError("native framework name is invalid: " + alias)
        for library in system_libraries:
            if not NATIVE_LIBRARY_RE.fullmatch(library):
                raise PackageError("native system library name is invalid: " + alias)
        packages.append({
            "alias": alias,
            "root": str(root),
            "sources": sorted(absolute_sources),
            "pkg_config": sorted(set(pkg_config)),
            "frameworks": sorted(set(frameworks)),
            "system_libraries": sorted(set(system_libraries)),
            "ffi_resources": _native_resource_contracts(native, alias),
        })
    return {"schema": "toka.native-package-plan-v2", "packages": packages,
            "target": target, "version": 2}


def compiler_mappings(lock_path: Path, state: Path) -> list[str]:
    mappings: list[str] = []
    for alias, entry in sorted(read_lock(lock_path).items()):
        root = package_root(entry, state)
        if tree_sha256(root) != entry.content_sha256:
            raise PackageError("package content verification failed: " + alias)
        legacy_module = root / "lib" / alias / "mod.tk"
        if legacy_module.is_file():
            mappings.append(alias + "=" + str(legacy_module))
            continue

        # Official packages use their published import identity as the entry
        # path. Keep the existing lock alias for resolution, but map the
        # compiler's exact `official/name` import to the declared convention.
        official_module = root / "lib" / "official" / (alias + ".tk")
        if official_module.is_file():
            mappings.append("official/" + alias + "=" + str(official_module))
            continue

        raise PackageError(
            "package module is missing: expected "
            + str(legacy_module)
            + " or "
            + str(official_module)
        )
    return mappings


def compiler_node_mappings(lock_path: Path) -> list[str]:
    """Return import-prefix to opaque locked-package-node mappings.

    The compiler receives these separately from paths so a relocated package
    installation cannot change its shadow crate identity.
    """
    mappings: list[str] = []
    for alias, entry in sorted(read_lock(lock_path).items()):
        payload = "\0".join(("toka.package-node.v1", entry.kind, entry.locator,
                               entry.resolved, entry.archive_sha256,
                               entry.content_sha256, ",".join(entry.dependencies)))
        node_id = "pkg-v1-" + hashlib.sha256(payload.encode("utf-8")).hexdigest()
        mappings.append(alias + "=" + node_id)
        mappings.append("official/" + alias + "=" + node_id)
    return mappings


def workspace_node(manifest_path: Path, lock_path: Path) -> str:
    if not manifest_path.is_file():
        raise PackageError("workspace identity requires package.tk")
    digest = hashlib.sha256()
    digest.update(b"toka.workspace-node.v1\0")
    digest.update(manifest_path.read_bytes())
    digest.update(b"\0")
    if lock_path.is_file():
        digest.update(lock_path.read_bytes())
    return "workspace-v1-" + digest.hexdigest()


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

    nodes = subparsers.add_parser("compiler-node-mappings")
    nodes.add_argument("--lock", default="package.lock")

    workspace = subparsers.add_parser("workspace-node")
    workspace.add_argument("--manifest", default="package.tk")
    workspace.add_argument("--lock", default="package.lock")

    native_plan = subparsers.add_parser("native-build-plan")
    native_plan.add_argument("--lock", default="package.lock")
    native_plan.add_argument("--state", default=".toka")
    native_plan.add_argument("--target", choices=NATIVE_TARGETS)

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
        elif args.command == "compiler-node-mappings":
            for mapping in compiler_node_mappings(Path(args.lock)):
                print(mapping)
        elif args.command == "workspace-node":
            print(workspace_node(Path(args.manifest), Path(args.lock)))
        elif args.command == "native-build-plan":
            print(json.dumps(native_build_plan(Path(args.lock), Path(args.state), target=args.target),
                             sort_keys=True, separators=(",", ":")))
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
