#!/usr/bin/env python3
import os
import sys
import json
import subprocess
import argparse
import shlex
import hashlib
from pathlib import Path

def fnv1a_64(data: bytes) -> str:
    # FNV-1a 64-bit algorithm matching ModuleResolver.cpp
    h = 14695981039346656037
    for b in data:
        h ^= b
        h = (h * 1099511628211) & 0xffffffffffffffff
    return f"{h:016x}"

def canonicalize(path: str) -> str:
    if not path:
        return ""
    return os.path.realpath(os.path.abspath(path)).replace('\\', '/')

def calculate_file_hash(path: str) -> str:
    if not os.path.exists(path) or os.path.isdir(path):
        return ""
    try:
        with open(path, 'rb') as f:
            return fnv1a_64(f.read())
    except Exception:
        return ""

def run_tokac_dump(tokac_path: str, compiler_args: list, entry_files: list, env: dict = None) -> dict:
    cmd = [tokac_path, "--dump-dependencies=json"] + compiler_args + entry_files
    try:
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True, env=env)
        return json.loads(res.stdout)
    except subprocess.CalledProcessError as e:
        sys.stderr.write(f"Error calling tokac dump-dependencies:\n{e.stderr}\n")
        sys.exit(e.returncode)
    except json.JSONDecodeError as e:
        sys.stderr.write(f"Failed to parse tokac dependency JSON output: {e}\n")
        sys.exit(1)

def run_tokac_compile(tokac_path: str, compiler_args: list, files_and_objs: list, env: dict = None) -> int:
    cmd = [tokac_path] + compiler_args + files_and_objs
    try:
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
        if res.returncode != 0:
            sys.stderr.write(f"Compilation Failed:\n{res.stderr}\n")
            sys.stdout.write(res.stdout)
        return res.returncode
    except Exception as e:
        sys.stderr.write(f"Failed to launch tokac: {e}\n")
        return 1

def load_manifest(path: str) -> dict:
    if not os.path.exists(path):
        return {}
    try:
        with open(path, 'r') as f:
            return json.load(f)
    except Exception:
        return {}

def save_manifest(path: str, data: dict):
    dir_name = os.path.dirname(path)
    if dir_name and not os.path.exists(dir_name):
        os.makedirs(dir_name)
    with open(path, 'w') as f:
        json.dump(data, f, indent=2)

def filter_args_for_submodule(args_list: list) -> list:
    res = []
    skip = False
    for arg in args_list:
        if skip:
            skip = False
            continue
        if arg == "-o":
            skip = True
            continue
        if arg == "-c" or arg == "--emit-interface" or arg == "--emit-obj":
            continue
        if arg == "--link-lib" or arg == "--link-search":
            skip = True
            continue
        res.append(arg)
    return res


def package_helper_path() -> Path | None:
    candidates: list[Path] = []
    toka_lib = os.environ.get("TOKA_LIB")
    if toka_lib:
        candidates.append(Path(toka_lib) / "toolchain" / "toka_package.py")
    candidates.append(Path(__file__).resolve().parents[2] / "lib" / "toolchain" / "toka_package.py")
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def pkg_config(mode: str, library: str) -> list[str]:
    tool = os.environ.get("PKG_CONFIG") or "pkg-config"
    try:
        result = subprocess.run(
            [tool, mode, library], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, check=False,
        )
    except OSError as error:
        raise RuntimeError("native package support requires pkg-config: " + str(error)) from error
    if result.returncode != 0:
        raise RuntimeError(
            "pkg-config %s %s failed: %s" % (mode, library, result.stderr.strip())
        )
    return shlex.split(result.stdout)


def parse_link_flags(tokens: list[str], library: str) -> tuple[list[str], list[str]]:
    search_paths: list[str] = []
    libraries: list[str] = []
    index = 0
    while index < len(tokens):
        token = tokens[index]
        if token == "-L" or token == "-l":
            if index + 1 >= len(tokens):
                raise RuntimeError("pkg-config emitted an incomplete %s flag for %s" % (token, library))
            value = tokens[index + 1]
            index += 2
            if token == "-L":
                search_paths.append(value)
            else:
                libraries.append(value)
            continue
        if token.startswith("-L") and len(token) > 2:
            search_paths.append(token[2:])
        elif token.startswith("-l") and len(token) > 2:
            libraries.append(token[2:])
        else:
            raise RuntimeError(
                "native package library %s emitted unsupported linker flag %r; "
                "v1 accepts only pkg-config -L/-l output" % (library, token)
            )
        index += 1
    return search_paths, libraries


def native_package_plan() -> tuple[list[dict], list[str], list[str], list[str], str]:
    if not Path("package.lock").is_file():
        return [], [], [], [], ""
    helper = package_helper_path()
    if helper is None:
        raise RuntimeError("native package support could not find toka_package.py")
    result = subprocess.run(
        [sys.executable, str(helper), "native-build-plan", "--lock", "package.lock", "--state", ".toka"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False,
    )
    if result.returncode != 0:
        raise RuntimeError("could not read locked native package metadata: " + result.stderr.strip())
    try:
        plan = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError("native package plan was not JSON") from error
    if plan.get("schema") != "toka.native-package-plan-v1" or plan.get("version") != 1:
        raise RuntimeError("unsupported native package plan")
    packages = plan.get("packages")
    if not isinstance(packages, list):
        raise RuntimeError("native package plan has invalid packages")

    cflags: list[str] = []
    search_paths: list[str] = []
    link_libraries: list[str] = []
    fingerprint = hashlib.sha256()
    for package in packages:
        if not isinstance(package, dict):
            raise RuntimeError("native package plan contains an invalid package")
        alias = package.get("alias")
        sources = package.get("sources")
        libraries = package.get("libraries")
        if not isinstance(alias, str) or not isinstance(sources, list) or not isinstance(libraries, list):
            raise RuntimeError("native package plan contains invalid fields")
        fingerprint.update(alias.encode("utf-8"))
        for source in sources:
            if not isinstance(source, str) or not Path(source).is_file():
                raise RuntimeError("native package source is unavailable: " + str(source))
            fingerprint.update(source.encode("utf-8"))
            fingerprint.update(Path(source).read_bytes())
        for library in libraries:
            if not isinstance(library, str):
                raise RuntimeError("native package library is invalid")
            library_cflags = pkg_config("--cflags", library)
            library_search, library_links = parse_link_flags(pkg_config("--libs", library), library)
            cflags.extend(library_cflags)
            search_paths.extend(library_search)
            link_libraries.extend(library_links)
            fingerprint.update(library.encode("utf-8"))
            fingerprint.update("\0".join(library_cflags + library_search + library_links).encode("utf-8"))
    return (
        packages,
        list(dict.fromkeys(cflags)),
        list(dict.fromkeys(search_paths)),
        list(dict.fromkeys(link_libraries)),
        fingerprint.hexdigest(),
    )


def compile_native_sources(packages: list[dict], build_dir: str, cflags: list[str]) -> list[str]:
    if not packages:
        return []
    compiler = shlex.split(os.environ.get("CC", "cc"))
    if not compiler:
        raise RuntimeError("CC must name a C compiler")
    native_dir = Path(build_dir) / "native"
    native_dir.mkdir(parents=True, exist_ok=True)
    objects: list[str] = []
    for package in packages:
        alias = package["alias"]
        for source in package["sources"]:
            source_path = Path(source)
            digest = hashlib.sha256((alias + "\0" + str(source_path)).encode("utf-8")).hexdigest()[:16]
            object_path = native_dir / (source_path.stem + "-" + digest + ".o")
            command = compiler + ["-c", str(source_path), "-o", str(object_path)] + cflags
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
            if result.returncode != 0:
                raise RuntimeError(
                    "native source compilation failed for %s:\n%s" % (source_path, result.stderr)
                )
            objects.append(str(object_path))
    return objects

def is_std_or_core(path: str) -> bool:
    path_norm = os.path.realpath(path).replace('\\', '/')
    std_paths = ["/usr/local/lib/toka"]
    toka_lib = os.environ.get("TOKA_LIB")
    if toka_lib:
        std_paths.append(os.path.realpath(toka_lib))

    for std_p in std_paths:
        std_p_norm = std_p.replace('\\', '/')
        if path_norm.startswith(std_p_norm):
            return True

    std_folders = ["/lib/core/", "/lib/std/", "/lib/sys/", "/lib/stdx/", "/lib/prim/", "/lib/hal/", "/lib/toolchain/"]
    if any(f in path_norm for f in std_folders) or path_norm.endswith("/lib/build.tk"):
        return True
    return False

def populate_submodule_outputs(graph: dict, build_dir: str):
    # Dynamically inject unique, stable objects/interfaces paths for all non-root source modules
    roots = graph.get("roots", [])
    for m_path, info in graph.get("modules", {}).items():
        if m_path not in roots and info.get("kind") == "source":
            if is_std_or_core(m_path):
                continue
            h = fnv1a_64(m_path.encode())
            obj_path = canonicalize(f"{build_dir}/objects/{h}.o")
            tki_path = canonicalize(f"{build_dir}/interfaces/{h}.tki")
            info["outputs"] = {
                "interface": tki_path,
                "object": obj_path,
                "executable": ""
            }

def get_transitive_dependencies(root: str, current_graph: dict) -> list:
    curr_roots = current_graph.get("roots", [])
    modules = current_graph.get("modules", {})
    visited = set()
    deps = []

    def dfs(node):
        if node in visited:
            return
        visited.add(node)
        info = modules.get(node)
        if not info:
            return
        for dep in info.get("dependencies", []):
            dfs(dep)
            dep_info = modules.get(dep)
            if dep_info and dep_info.get("kind") == "source" and dep not in curr_roots:
                deps.append(dep)

    dfs(root)
    # Deduplicate while preserving order
    seen = set()
    res = []
    for d in deps:
        if d not in seen:
            seen.add(d)
            res.append(d)
    return res

def topological_sort_submodules(dirty_submodules: dict, current_graph: dict) -> list:
    modules = current_graph.get("modules", {})
    roots = current_graph.get("roots", [])
    visited = set()
    temp_visited = set()
    order = []

    def dfs(node):
        if node in temp_visited:
            return
        if node in visited:
            return
        temp_visited.add(node)

        info = modules.get(node, {})
        for dep in info.get("dependencies", []):
            dfs(dep)

        temp_visited.remove(node)
        visited.add(node)

        if node in dirty_submodules and node not in roots and not is_std_or_core(node):
            order.append(node)

    for m_path in dirty_submodules:
        dfs(m_path)

    return order

def generate_rebuild_plan(current_graph: dict, old_manifest: dict) -> dict:
    plan = {
        "status": "clean",
        "dirty_roots": [],
        "dirty_modules": {}
    }

    if not old_manifest:
        # First build case
        plan["status"] = "dirty"
        plan["dirty_roots"] = current_graph.get("roots", [])
        for k in current_graph.get("modules", {}):
            plan["dirty_modules"][k] = {
                "reason": "first build",
                "dirty_deps": []
            }
        return plan

    # Validate schema version
    old_ver = old_manifest.get("manifest_version", "")
    if not old_ver.startswith("1."):
        plan["status"] = "dirty"
        plan["dirty_roots"] = current_graph.get("roots", [])
        for k in current_graph.get("modules", {}):
            plan["dirty_modules"][k] = {
                "reason": "version/target changed",
                "dirty_deps": []
            }
        return plan

    # Check roots consistency
    curr_roots = sorted(current_graph.get("roots", []))
    old_roots = sorted(old_manifest.get("roots", []))
    if curr_roots != old_roots:
        plan["status"] = "dirty"
        plan["dirty_roots"] = current_graph.get("roots", [])
        for k in current_graph.get("modules", {}):
            plan["dirty_modules"][k] = {
                "reason": "roots changed",
                "dirty_deps": []
            }
        return plan

    dirty_map = {}

    # Perform physical checking for each module
    for m_path, curr_info in current_graph.get("modules", {}).items():
        old_info = old_manifest.get("modules", {}).get(m_path)
        if not old_info:
            dirty_map[m_path] = {"reason": "new module", "dirty_deps": []}
            continue

        # 1. Target & Compiler Version verification
        if (curr_info.get("target_triple") != old_info.get("target_triple") or
            curr_info.get("compiler_version") != old_info.get("compiler_version") or
            curr_info.get("interface_version") != old_info.get("interface_version")):
            dirty_map[m_path] = {"reason": "version/target changed", "dirty_deps": []}
            continue

        # 2. Outputs verification (outputs changed check and current output existence check)
        curr_outputs = curr_info.get("outputs", {})
        old_outputs = old_info.get("outputs", {})
        if curr_outputs != old_outputs:
            dirty_map[m_path] = {"reason": "outputs changed", "dirty_deps": []}
            continue

        outputs_missing = False
        for out_key, out_path in curr_outputs.items():
            if out_path:
                if not os.path.exists(out_path):
                    outputs_missing = True
                    break
        if outputs_missing:
            dirty_map[m_path] = {"reason": "missing output", "dirty_deps": []}
            continue

        # 3. File existence & Hash validation
        if not os.path.exists(m_path):
            dirty_map[m_path] = {"reason": "file missing", "dirty_deps": []}
            continue

        curr_hash = calculate_file_hash(m_path)
        if curr_hash != old_info.get("content_hash", ""):
            reason = "hash changed" if curr_info.get("kind") == "source" else "stale interface"
            dirty_map[m_path] = {"reason": reason, "dirty_deps": []}
            continue

    # 4. Dependency contagion checking (DFS with memoization)
    memo = {}
    def check_dirty_recursive(node: str) -> tuple:
        if node in memo:
            return memo[node]

        if node in dirty_map:
            memo[node] = (True, dirty_map[node]["reason"], [])
            return memo[node]

        curr_info = current_graph.get("modules", {}).get(node)
        if not curr_info:
            # Module missing in current graph (highly unlikely but safeguard)
            memo[node] = (True, "missing module info", [])
            return memo[node]

        for dep in curr_info.get("dependencies", []):
            dep_dirty, dep_reason, dep_chain = check_dirty_recursive(dep)
            if dep_dirty:
                memo[node] = (True, "dependency changed", [dep] + dep_chain)
                return memo[node]

        memo[node] = (False, "", [])
        return memo[node]

    # Populate final plan mapping
    for m_path in current_graph.get("modules", {}):
        is_dirty, reason, chain = check_dirty_recursive(m_path)
        if is_dirty:
            plan["dirty_modules"][m_path] = {
                "reason": reason,
                "dirty_deps": chain
            }
            # If a root is dirty, register it
            if m_path in current_graph.get("roots", []):
                if m_path not in plan["dirty_roots"]:
                    plan["dirty_roots"].append(m_path)

    if plan["dirty_modules"]:
        plan["status"] = "dirty"

    return plan

def main():
    parser = argparse.ArgumentParser(description="Toka Incremental Build Manager (Distributed Compile & Link)")
    parser.add_argument('entry_files', nargs='+', help='Entry file paths')
    parser.add_argument('--manifest', '-m', default='.toka/build/manifest.json', help='Manifest store path')
    parser.add_argument('--tokac', default='build/bin/tokac', help='Path to tokac compiler')
    parser.add_argument('--compiler-args', default='', help='Arguments to pass to tokac (e.g. "-I lib -o app")')
    parser.add_argument('--compiler-arg', action='append', default=[], help='One literal argument to pass to tokac')
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--plan', action='store_true', help='Perform dirty check and print rebuild plan JSON')
    group.add_argument('--build', action='store_true', help='Compile dirty targets and persist manifest')

    args = parser.parse_args()

    # Parse compiler arguments
    c_args = shlex.split(args.compiler_args) + args.compiler_arg

    # Resolve build directory and construct environment
    build_dir = canonicalize(os.path.dirname(args.manifest) or ".")
    env = os.environ.copy()
    env["TOKA_BUILD_DIR"] = build_dir

    try:
        native_packages, native_cflags, native_search_paths, native_libraries, native_fingerprint = native_package_plan()
    except RuntimeError as error:
        sys.stderr.write("Native package build error: %s\n" % error)
        sys.exit(1)
    native_link_args: list[str] = []
    for search_path in native_search_paths:
        native_link_args.extend(["--link-search", search_path])
    for library in native_libraries:
        native_link_args.extend(["--link-lib", library])

    # 1. Run compiler dependency dump to get current graph
    current_graph = run_tokac_dump(args.tokac, c_args, args.entry_files, env=env)

    # 2. Inject stable module-level objects/interfaces outputs for all submodules
    populate_submodule_outputs(current_graph, build_dir)

    # 3. Load old manifest
    old_manifest = load_manifest(args.manifest)

    # 4. Generate rebuild plan
    plan = generate_rebuild_plan(current_graph, old_manifest)
    if old_manifest.get("native_package_fingerprint", "") != native_fingerprint:
        plan["status"] = "dirty"
        for root in current_graph.get("roots", []):
            if root not in plan["dirty_roots"]:
                plan["dirty_roots"].append(root)
        plan["native_package_changed"] = True

    if args.plan:
        # Output plan JSON for machine consumption
        print(json.dumps(plan, indent=2))
        return

    if args.build:
        if plan["status"] == "clean":
            print("All targets are clean. Nothing to compile!")
            return

        print(f"Compiling {len(plan['dirty_modules'])} dirty modules...")

        try:
            native_objects = compile_native_sources(native_packages, build_dir, native_cflags)
        except RuntimeError as error:
            sys.stderr.write("Native package build error: %s\n" % error)
            sys.exit(1)

        # 1. Build all dirty non-root modules separately
        sub_c_args = filter_args_for_submodule(c_args)

        # Create build directories for objects and interfaces
        obj_dir = os.path.join(build_dir, "objects")
        if not os.path.exists(obj_dir):
            os.makedirs(obj_dir)
        int_dir = os.path.join(build_dir, "interfaces")
        if not os.path.exists(int_dir):
            os.makedirs(int_dir)

        sorted_submodules = topological_sort_submodules(plan["dirty_modules"], current_graph)
        for m_path in sorted_submodules:
            # This is a dirty submodule, compile it separately
            curr_info = current_graph["modules"][m_path]
            expected_obj = curr_info.get("outputs", {}).get("object", "")
            if not expected_obj:
                continue

            print(f"Compiling dependency submodule: {m_path}")
            ret = run_tokac_compile(args.tokac, ["-c", "-o", expected_obj] + sub_c_args, [m_path], env=env)
            if ret != 0:
                sys.exit(ret)

        # 2. Compile and link all dirty root modules
        for root in plan["dirty_roots"]:
            # Collect all compiled/cached dependency objects (.o) transitive to this root
            dep_paths = get_transitive_dependencies(root, current_graph)
            dep_objs = []
            for dep in dep_paths:
                dep_info = current_graph["modules"][dep]
                obj = dep_info.get("outputs", {}).get("object")
                if obj:
                    dep_objs.append(obj)

            print(f"Linking root module with dependencies: {root}")
            # Compile root and link everything in a single driver call
            ret = run_tokac_compile(
                args.tokac, c_args + native_link_args + native_objects + dep_objs,
                [root], env=env,
            )
            if ret != 0:
                sys.exit(ret)

        # 3. Re-generate dependencies graph to record final success state
        success_graph = run_tokac_dump(args.tokac, c_args, args.entry_files, env=env)
        populate_submodule_outputs(success_graph, build_dir)
        success_graph["native_package_fingerprint"] = native_fingerprint
        save_manifest(args.manifest, success_graph)
        print("Build successful!")

if __name__ == "__main__":
    main()
