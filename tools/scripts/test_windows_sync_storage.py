#!/usr/bin/env python3
"""Native MinGW storage reserves plus source factory/runtime controls."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    compiler = root / args.build_dir / "bin/tokac.exe"
    cc = os.environ.get("CC", "clang")
    env = dict(os.environ, TOKA_LIB=str(root / "lib"))
    with tempfile.TemporaryDirectory(prefix="toka-windows-sync-storage-") as temp:
        work = Path(temp)
        native = work / "layout.c"
        native.write_text('''#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "toka_native_sync_layout.h"
#if !defined(__MINGW32__) || !defined(_WIN64) || !defined(__x86_64__)
#error This witness requires native MinGW x64
#endif
int main(void) {
    pthread_mutex_t mutex;
    pthread_rwlock_t rw;
    pthread_cond_t cond;
    if (pthread_mutex_init(&mutex, NULL) || pthread_rwlock_init(&rw, NULL) ||
        pthread_cond_init(&cond, NULL)) return 1;
    if (pthread_mutex_lock(&mutex) || pthread_mutex_unlock(&mutex) ||
        pthread_rwlock_rdlock(&rw) || pthread_rwlock_unlock(&rw) ||
        pthread_rwlock_wrlock(&rw) || pthread_rwlock_unlock(&rw) ||
        pthread_cond_signal(&cond) || pthread_cond_broadcast(&cond)) return 2;
    if (pthread_mutex_destroy(&mutex) || pthread_rwlock_destroy(&rw) ||
        pthread_cond_destroy(&cond)) return 3;
    for (size_t size = 1; size <= 512; ++size) {
        void *p = malloc(size);
        if (!p || (uintptr_t)p % 16) return 4;
        free(p);
    }
    printf("mutex=%zu rw=%zu cond=%zu max_align=%zu; native primitives and 512 allocations passed\\n",
           sizeof(pthread_mutex_t), sizeof(pthread_rwlock_t), sizeof(pthread_cond_t), _Alignof(max_align_t));
    return 0;
}
''')
        exe = work / "layout.exe"
        layout = subprocess.run([cc, "-std=c11", "-pthread", "-I", str(root / "lib/sys"),
                                 str(native), "-o", str(exe)], capture_output=True, text=True)
        assert layout.returncode == 0, layout.stdout + layout.stderr
        witness = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
        assert witness.returncode == 0, witness.stdout + witness.stderr
        print(witness.stdout.strip())
        condition = work / "condition.tk"
        condition.write_text("import std/sync::CondVar\nfn main() -> i32 {\n"
                             "auto condition = CondVar<i32>::new()\n"
                             "condition.notify_one()\ncondition.notify_all()\nreturn 0\n}\n")
        sources = (root / "tests/semantics/std_thread_handoff/sync_storage.tk", condition)
        for index, source in enumerate(sources):
            app = work / ("factories-%d.exe" % index)
            built = subprocess.run([str(compiler), str(source), "-o", str(app)], cwd=root,
                                   env=env, capture_output=True, text=True, timeout=60)
            assert built.returncode == 0 and app.is_file(), built.stdout + built.stderr
            ran = subprocess.run([str(app)], env=env, capture_output=True, text=True, timeout=15)
            assert ran.returncode == 0, ran.stdout + ran.stderr
    print(json.dumps({"schema": "toka.windows-sync-storage-targeted", "version": 1,
                      "result": "pass", "malloc_controls": 512,
                      "scope": "MinGW x64 storage and single-thread primitive operations; no thread witness"}))


if __name__ == "__main__":
    main()
