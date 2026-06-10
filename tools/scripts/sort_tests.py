#!/usr/bin/env python3
import os
import re
import sys
import argparse

TESTS_DIR = "tests/pass"

# Rule matcher from complex (hardest) to basic (easiest)
RULES = [
    # 10_系统级与高级I/O应用
    ("10", [
        r'\bFile::\b', r'\bTcpStream::\b', r'\bTcpListener::\b', r'\bJSON::\b', 
        r'\bsha256::\b', r'\bmd5::\b', r'\bbase64::\b', r'\bwebsocket::\b', 
        r'\bWebSocket::\b', r'\bHttpServer\b', r'\bSocket::\b', r'\bPath::\b',
        r'\bfs::\b', r'\bnet::\b', r'\bDirectory::\b', r'\bUrl::\b'
    ]),
    # 09_并发与异步
    ("09", [
        r'\basync\b', r'\bawait\b', r'\bMutex\b', r'\bMPSC\b', r'\bCondVar\b', 
        r'\bOnce\b', r'\bRwLock\b', r'\bThread\b', r'\bChannel\b', r'\bWaitGroup\b',
        r'\batomic\b'
    ]),
    # 08_所有权、智能指针与闭包
    ("08", [
        r'\bcede\b', r'\bdrop\b', r'\bborrow\b', r'\bclosure\b', 
        r'\bUnique\b', r'\bShared\b', r'\bRc\b',
        r'\^\w+', r'\~\w+',
        r'=\s*fn\s*\('
    ]),
    # 07_泛型与元编程
    ("07", [
        r'\bcomptime\b', r'\breflect\b', 
        r'\b\w+<[A-Za-z0-9_,\s<>]+>'
    ]),
    # 06_Trait接口
    ("06", [
        r'\btrait\b', r'\w+@\w+'
    ]),
    # 05_OOP方法
    ("05", [
        r'\bimpl\b'
    ]),
    # 04_复合类型定义
    ("04", [
        r'\bshape\b', r'\bunion\b', r'\bOption\b', r'\bResult\b',
        r'\[\s*\d+\s*\]\w+', r'\[\s*\w+\s*;\s*\d+\s*\]'
    ]),
    # 03_基础函数
    ("03", [
        r'\bfn\s+\w+'
    ]),
    # 02_控制流
    ("02", [
        r'\bif\b', r'\belse\b', r'\bloop\b', r'\bbreak\b', r'\bcontinue\b'
    ]),
]

def clean_comment(content):
    # Remove single line comments
    content = re.sub(r'//.*$', '', content, flags=re.MULTILINE)
    # Remove multi-line comments
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
    return content

def classify_file(filepath):
    filename = os.path.basename(filepath)
    
    # Explicit overrides for clarity
    if filename.endswith(".tk_lib"):
        return "04"
    if "helloworld" in filename or "dummy" in filename or "simple" in filename:
        return "01"
    if "stress_control_flow" in filename:
        return "02"
        
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = clean_comment(f.read())
    except Exception:
        return "01"
        
    for group, patterns in RULES:
        for pattern in patterns:
            if re.search(pattern, content):
                return group
                
    return "01"

def apply_prefixes():
    print("Applying group prefixes to tests...")
    renamed_count = 0
    skipped_count = 0
    
    # Sort files to ensure stability
    files = sorted(os.listdir(TESTS_DIR))
    
    for filename in files:
        if not (filename.endswith(".tk") or filename.endswith(".tk_lib")):
            continue
            
        filepath = os.path.join(TESTS_DIR, filename)
        
        # Check if already prefixed
        if re.match(r'^g\d{2}_', filename):
            print(f"Skipped (already prefixed): {filename}")
            skipped_count += 1
            continue
            
        group = classify_file(filepath)
        new_filename = f"g{group}_{filename}"
        new_filepath = os.path.join(TESTS_DIR, new_filename)
        
        os.rename(filepath, new_filepath)
        print(f"Renamed: {filename} -> {new_filename}")
        renamed_count += 1
        
    print(f"Finished. Renamed: {renamed_count}, Skipped: {skipped_count}")

def rollback_prefixes():
    print("Rolling back group prefixes from tests...")
    restored_count = 0
    skipped_count = 0
    
    files = sorted(os.listdir(TESTS_DIR))
    
    for filename in files:
        if not (filename.endswith(".tk") or filename.endswith(".tk_lib")):
            continue
            
        if not re.match(r'^g\d{2}_', filename):
            skipped_count += 1
            continue
            
        filepath = os.path.join(TESTS_DIR, filename)
        original_filename = re.sub(r'^g\d{2}_', '', filename)
        original_filepath = os.path.join(TESTS_DIR, original_filename)
        
        os.rename(filepath, original_filepath)
        print(f"Restored: {filename} -> {original_filename}")
        restored_count += 1
        
    print(f"Finished. Restored: {restored_count}, Skipped: {skipped_count}")

def main():
    parser = argparse.ArgumentParser(description="Sort and prefix Toka test files by feature complexity.")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--apply", action="store_true", help="Apply group prefixes (01_ to 10_) to test files.")
    group.add_argument("--rollback", action="store_true", help="Rollback and remove prefixes from test files.")
    
    args = parser.parse_args()
    
    if not os.path.exists(TESTS_DIR):
        print(f"Error: {TESTS_DIR} directory does not exist.")
        sys.exit(1)
        
    if args.apply:
        apply_prefixes()
    elif args.rollback:
        rollback_prefixes()

if __name__ == "__main__":
    main()
