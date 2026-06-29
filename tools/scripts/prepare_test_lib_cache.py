#!/usr/bin/env python3
import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
OBJECT_CACHE_DENYLIST = {
    # These modules currently replay exported generic bodies that touch private
    # implementation fields from another cached module. Keep them source-backed
    # until interface replay carries the original module permission context.
    "std/btreemap.tk",
    "std/btreeset.tk",
    "std/deque.tk",
    "std/hashmap.tk",
    "std/hashset.tk",
    "std/slab.tk",
    # Top-level codegen symbols are not module-qualified yet, so these two both
    # export _exists and cannot safely coexist as separate archive members.
    "std/fs.tk",
    "std/io.tk",
    # Platform base modules currently define __toka_panic_handler, which
    # collides with core/internal/runtime when both objects enter the archive.
    "sys/linux/base.tk",
    "sys/macos/base.tk",
    "sys/wasi/base.tk",
    "sys/windows/base.tk",
}
CACHE_FORMAT_VERSION = 9
CACHE_ARCHIVE_NAME = "libtoka_cache.a"


def fnv1a_hex(data: bytes) -> str:
    h = FNV_OFFSET
    for b in data:
        h ^= b
        h = (h * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return f"{h:016x}"


def canonical(path: str) -> str:
    return os.path.realpath(path).replace("\\", "/")


def file_hash(path: str) -> str:
    with open(path, "rb") as f:
        return fnv1a_hex(f.read())


def run(cmd, env=None):
    return subprocess.run(cmd, text=True, capture_output=True, env=env)


def is_lib_source(path: str, lib_root: str) -> bool:
    p = canonical(path)
    return p.startswith(lib_root + "/") and p.endswith(".tk")


def is_object_cache_allowed(path: str, lib_root: str) -> bool:
    rel = os.path.relpath(canonical(path), lib_root).replace("\\", "/")
    return rel not in OBJECT_CACHE_DENYLIST


def find_tests(paths):
    if paths:
        return [str(Path(p)) for p in paths]
    return sorted(str(p) for p in Path("tests/pass").rglob("*.tk"))


def test_key(path: str) -> str:
    return os.path.relpath(canonical(path), canonical(".")).replace("\\", "/")


def collect_lib_graph(tokac: str, tests, lib_root: str):
    modules = {}
    deps = {}
    test_modules = {}
    for test in tests:
        key = test_key(test)
        test_modules[key] = set()
        proc = run([tokac, "--dump-dependencies=json", test])
        if proc.returncode != 0:
            sys.stderr.write(proc.stderr)
            raise SystemExit(f"failed to dump dependencies for {test}")
        data = json.loads(proc.stdout)
        for path, info in data.get("modules", {}).items():
            cpath = canonical(path)
            if not is_lib_source(cpath, lib_root):
                continue
            test_modules[key].add(cpath)
            modules[cpath] = info
            local_deps = []
            for dep in info.get("dependencies", []):
                cdep = canonical(dep)
                if is_lib_source(cdep, lib_root):
                    local_deps.append(cdep)
            deps[cpath] = local_deps
    return modules, deps, test_modules


def topo_sort(nodes, deps):
    visited = set()
    visiting = set()
    order = []

    def visit(node):
        if node in visited:
            return
        if node in visiting:
            return
        visiting.add(node)
        for dep in deps.get(node, []):
            if dep in nodes:
                visit(dep)
        visiting.remove(node)
        visited.add(node)
        order.append(node)

    for node in sorted(nodes):
        visit(node)
    return order


def source_fingerprints(paths):
    return [
        {"path": path, "hash": file_hash(path)}
        for path in sorted(canonical(str(p)) for p in paths)
    ]


def build_common_fingerprint(tokac: str):
    version = run([tokac, "--version"]).stdout.strip()
    tokac_path = shutil.which(tokac) or tokac
    tokac_real = canonical(tokac_path)
    return {
        "tokac": tokac_real,
        "tokac_mtime": os.path.getmtime(tokac_real) if os.path.exists(tokac_real) else 0,
        "cache_format_version": CACHE_FORMAT_VERSION,
        "version": version,
        "target_env": os.environ.get("TOKA_TARGET_TRIPLE", ""),
        "object_cache_denylist": sorted(OBJECT_CACHE_DENYLIST),
    }


def build_fingerprint(tokac: str, tests, modules):
    payload = build_common_fingerprint(tokac)
    payload["tests"] = source_fingerprints(tests)
    payload["modules"] = source_fingerprints(modules)
    return payload


def write_object_list(cache_dir: str, modules):
    obj_dir = Path(cache_dir) / "objects"
    list_path = Path(cache_dir) / "objects.list"
    with open(list_path, "w") as f:
        for path in sorted(modules):
            obj = obj_dir / f"{fnv1a_hex(path.encode())}.o"
            if obj.exists():
                f.write(canonical(str(obj)) + "\n")
    return list_path


def cached_archive_path(cache_dir: str) -> Path:
    return Path(cache_dir) / CACHE_ARCHIVE_NAME


def find_archive_tool() -> str:
    candidates = []
    if os.environ.get("AR"):
        candidates.append(os.environ["AR"])
    candidates.extend(["llvm-ar", "ar"])
    for candidate in candidates:
        path = shutil.which(candidate) or (candidate if os.path.exists(candidate) else None)
        if path:
            return path
    raise SystemExit("failed to find an archive tool: llvm-ar or ar")


def write_archive(cache_dir: str, modules):
    archive_path = cached_archive_path(cache_dir)
    obj_paths = [
        cached_object_path(cache_dir, module)
        for module in sorted(modules)
        if cached_object_path(cache_dir, module).exists()
    ]
    if archive_path.exists():
        archive_path.unlink()
    if not obj_paths:
        return archive_path

    proc = run([find_archive_tool(), "rcs", str(archive_path)] + [str(obj) for obj in obj_paths])
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
        raise SystemExit(f"failed to create cache archive: {archive_path}")
    return archive_path


def write_test_object_map(cache_dir: str, test_cached_modules):
    map_path = Path(cache_dir) / "test_objects.map"
    with open(map_path, "w") as f:
        for key in sorted(test_cached_modules):
            objects = []
            for module in sorted(test_cached_modules[key]):
                obj = cached_object_path(cache_dir, module)
                if obj.exists():
                    objects.append(canonical(str(obj)))
            if objects:
                f.write(key + "\t" + "\t".join(objects) + "\n")
            else:
                f.write(key + "\n")
    return map_path


def cached_object_path(cache_dir: str, module: str) -> Path:
    return Path(cache_dir) / "objects" / f"{fnv1a_hex(module.encode())}.o"


def cached_interface_path(cache_dir: str, module: str) -> Path:
    return Path(cache_dir) / "interfaces" / f"{fnv1a_hex(module.encode())}.tki"


def remove_cached_interface(cache_dir: str, module: str):
    tki = cached_interface_path(cache_dir, module)
    if tki.exists():
        tki.unlink()


def remove_cached_module(cache_dir: str, module: str):
    obj = cached_object_path(cache_dir, module)
    if obj.exists():
        obj.unlink()
    remove_cached_interface(cache_dir, module)


def is_cached_interface_usable(tokac: str, cache_dir: str, module: str) -> bool:
    tki = cached_interface_path(cache_dir, module)
    if not tki.exists():
        return False
    proc = run([tokac, "--dump-dependencies=json", str(tki)])
    return proc.returncode == 0


def cache_is_fresh(existing, tokac: str, tests, cache_dir: str) -> bool:
    if not existing:
        return False

    current_common = build_common_fingerprint(tokac)
    for key, value in current_common.items():
        if existing.get(key) != value:
            return False

    if existing.get("tests") != source_fingerprints(tests):
        return False

    modules = existing.get("modules")
    if not modules:
        return False

    for item in modules:
        path = item.get("path")
        if not path or not os.path.exists(path):
            return False
        if item.get("hash") != file_hash(path):
            return False

    cached_modules = existing.get("cached_modules")
    if cached_modules is None:
        return False

    if existing.get("test_cached_modules") is None:
        return False

    for module in cached_modules:
        if not cached_object_path(cache_dir, module).exists():
            return False
        if not cached_interface_path(cache_dir, module).exists():
            return False

    if cached_modules and not cached_archive_path(cache_dir).exists():
        return False

    return True


def main():
    parser = argparse.ArgumentParser(description="Prepare shared lib .o/.tki cache for Toka pass tests.")
    parser.add_argument("--tokac", default="./build/bin/tokac")
    parser.add_argument("--cache-dir", default="tmp/toka_test_cache")
    parser.add_argument("tests", nargs="*")
    args = parser.parse_args()

    cache_dir = canonical(args.cache_dir)
    lib_root = canonical("lib")
    tests = find_tests(args.tests)

    Path(cache_dir).mkdir(parents=True, exist_ok=True)
    stamp_path = Path(cache_dir) / "cache.stamp.json"

    existing = None
    if stamp_path.exists():
        with open(stamp_path) as f:
            existing = json.load(f)

    if cache_is_fresh(existing, args.tokac, tests, cache_dir):
        write_object_list(cache_dir, existing["cached_modules"])
        map_path = write_test_object_map(cache_dir, existing["test_cached_modules"])
        archive_path = cached_archive_path(cache_dir)
        print(f"[cache] up to date: {cache_dir}")
        print(f"[cache] modules: {len(existing['modules'])}")
        print(f"[cache] cached objects: {len(existing['cached_modules'])}")
        print(f"[cache] object list: {Path(cache_dir) / 'objects.list'}")
        print(f"[cache] test object map: {map_path}")
        print(f"[cache] archive: {archive_path}")
        return

    obj_dir = Path(cache_dir) / "objects"
    int_dir = Path(cache_dir) / "interfaces"
    modules, deps, test_modules = collect_lib_graph(args.tokac, tests, lib_root)
    fingerprint = build_fingerprint(args.tokac, tests, modules)
    if existing != fingerprint:
        shutil.rmtree(obj_dir, ignore_errors=True)
        shutil.rmtree(int_dir, ignore_errors=True)
        obj_dir.mkdir(parents=True, exist_ok=True)
        int_dir.mkdir(parents=True, exist_ok=True)

        env = os.environ.copy()
        env["TOKA_BUILD_DIR"] = cache_dir
        order = topo_sort(set(modules.keys()), deps)
        skipped = set()
        cached = set()
        for idx, module in enumerate(order, start=1):
            if not is_object_cache_allowed(module, lib_root):
                skipped.add(module)
                remove_cached_module(cache_dir, module)
                print(f"[cache deny {idx}/{len(order)}] {module}")
                continue

            skipped_deps = [dep for dep in deps.get(module, []) if dep in skipped]
            if skipped_deps:
                skipped.add(module)
                remove_cached_module(cache_dir, module)
                print(f"[cache skip {idx}/{len(order)}] {module}")
                continue

            obj = obj_dir / f"{fnv1a_hex(module.encode())}.o"
            print(f"[cache {idx}/{len(order)}] {module}")
            proc = run([args.tokac, "-c", module, "-o", str(obj)], env=env)
            if proc.returncode != 0:
                skipped.add(module)
                remove_cached_module(cache_dir, module)
                print(f"[cache skip] failed to compile object for {module}")
                continue
            if not is_cached_interface_usable(args.tokac, cache_dir, module):
                skipped.add(module)
                remove_cached_module(cache_dir, module)
                print(f"[cache skip] invalid interface for {module}")
                continue

            cached.add(module)

        fingerprint["cached_modules"] = sorted(cached)
        fingerprint["test_cached_modules"] = {
            key: sorted(module for module in modules_for_test if module in cached)
            for key, modules_for_test in sorted(test_modules.items())
        }
        with open(stamp_path, "w") as f:
            json.dump(fingerprint, f, indent=2, sort_keys=True)
    else:
        print(f"[cache] up to date: {cache_dir}")

    cached_modules = fingerprint.get("cached_modules", [])
    test_cached_modules = fingerprint.get("test_cached_modules", {})
    list_path = write_object_list(cache_dir, cached_modules)
    map_path = write_test_object_map(cache_dir, test_cached_modules)
    archive_path = write_archive(cache_dir, cached_modules)
    print(f"[cache] modules: {len(modules)}")
    print(f"[cache] cached objects: {len(cached_modules)}")
    print(f"[cache] object list: {list_path}")
    print(f"[cache] test object map: {map_path}")
    print(f"[cache] archive: {archive_path}")


if __name__ == "__main__":
    main()
