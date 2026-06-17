#!/usr/bin/env python3
import os
import sys
import json
import subprocess
import argparse
import shlex

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

def run_tokac_dump(tokac_path: str, compiler_args: list, entry_files: list) -> dict:
    cmd = [tokac_path, "--dump-dependencies=json"] + compiler_args + entry_files
    try:
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
        return json.loads(res.stdout)
    except subprocess.CalledProcessError as e:
        sys.stderr.write(f"Error calling tokac dump-dependencies:\n{e.stderr}\n")
        sys.exit(e.returncode)
    except json.JSONDecodeError as e:
        sys.stderr.write(f"Failed to parse tokac dependency JSON output: {e}\n")
        sys.exit(1)

def run_tokac_compile(tokac_path: str, compiler_args: list, entry_files: list) -> int:
    cmd = [tokac_path] + compiler_args + entry_files
    try:
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
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

        # 2. Outputs verification
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
    parser = argparse.ArgumentParser(description="Toka Incremental Build Manager (External Consumer Demo)")
    parser.add_argument('entry_files', nargs='+', help='Entry file paths')
    parser.add_argument('--manifest', '-m', default='.toka/build/manifest.json', help='Manifest store path')
    parser.add_argument('--tokac', default='build/bin/tokac', help='Path to tokac compiler')
    parser.add_argument('--compiler-args', default='', help='Arguments to pass to tokac (e.g. "-I lib -o app")')
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--plan', action='store_true', help='Perform dirty check and print rebuild plan JSON')
    group.add_argument('--build', action='store_true', help='Compile dirty targets and persist manifest')

    args = parser.parse_args()

    # Parse compiler arguments
    c_args = shlex.split(args.compiler_args)

    # 1. Run compiler dependency dump to get current graph
    current_graph = run_tokac_dump(args.tokac, c_args, args.entry_files)

    # 2. Load old manifest
    old_manifest = load_manifest(args.manifest)

    # 3. Generate rebuild plan
    plan = generate_rebuild_plan(current_graph, old_manifest)

    if args.plan:
        # Output plan JSON for machine consumption
        print(json.dumps(plan, indent=2))
        return

    if args.build:
        if plan["status"] == "clean":
            print("All targets are clean. Nothing to compile!")
            return

        print(f"Compiling {len(plan['dirty_roots'])} dirty targets...")
        ret = run_tokac_compile(args.tokac, c_args, args.entry_files)
        if ret == 0:
            # Re-generate dependencies graph to record final success state
            success_graph = run_tokac_dump(args.tokac, c_args, args.entry_files)
            save_manifest(args.manifest, success_graph)
            print("Build successful!")
        else:
            sys.exit(ret)

if __name__ == "__main__":
    main()
