# Toka I/O 底盘与 DataFile 完整实现 Walkthrough (终版复核)

本文档记录了对 Toka I/O 底盘（`ReadDataFile`、`WalFile`、`atomic_write_durable`）在最新 P0 审计反馈下的全面整改与复核验证证据。

---

## 1. P0 缺陷整改与对账清单

### 1.1 原生句柄/FD 严格私有封装（防止裸 FD 逃逸与非法构造）
- **缺陷**：此前在 `@Encap` 实现块中使用了 `pub handle` 与 `pub fd, opened`，导致外部调用方可直接读取或构造底层 native handle / fd，破坏 RAII 封装。
- **修复**：
  - 在 [`lib/std/data_file.tk`](file:///Users/zhyi/GitDP/tokalang/toka/lib/std/data_file.tk) 中移除所有 `pub` 字段导出，`handle` 与 `fd` 成为严格的模块内部私有字段；
  - 新增 Compile-Fail 测试：
    - [`tests/conformance/diagnostics/datafile_private_handle.tk`](file:///Users/zhyi/GitDP/tokalang/toka/tests/conformance/diagnostics/datafile_private_handle.tk)（注册为 `diag_datafile_private_handle_01`）：验证外部代码访问 `ReadDataFile.handle` 被编译器精准拦截并输出 `error[E0418]`；
    - [`tests/conformance/diagnostics/datafile_private_wal_fd.tk`](file:///Users/zhyi/GitDP/tokalang/toka/tests/conformance/diagnostics/datafile_private_wal_fd.tk)（注册为 `diag_datafile_private_wal_fd_01`）：验证外部代码访问 `WalFile.fd` 被编译器精准拦截并输出 `error[E0418]`。

---

### 1.2 `read_at` 修正为只读 Receiver（支持共享无锁并发读取）
- **缺陷**：此前 `read_at` 误标为 `self#` 独占接收者，导致无法通过共享借用调用。
- **修复**：
  - 将 `read_at` 接收者修正为只读 `self`：
    ```toka
    pub fn read_at(self, cede buffer#: Vec<u8>, offset: u64, max_len: usize) -> Result<ReadAtResult, ReadAtError>
    ```
  - 新增共享读测试用例 [`tests/conformance/io/datafile_shared_view_test.tk`](file:///Users/zhyi/GitDP/tokalang/toka/tests/conformance/io/datafile_shared_view_test.tk)（注册为 `io_datafile_shared_view_01`）：
    - 验证多个只读函数（`read_block` / `verify_shared_reads`）可以直接传递同一不可变 `reader: ReadDataFile` 并在多个 call site 上无锁并发执行 `read_at`，无需独占 receiver (`self#`)，也无需额外 `.clone()`；
  - 更新并发测试 [`tests/conformance/io/datafile_concurrency_test.tk`](file:///Users/zhyi/GitDP/tokalang/toka/tests/conformance/io/datafile_concurrency_test.tk)，子线程直接对不可变 `r` 执行 `r.read_at(...)`。

---

### 1.3 测试范围与耐久性证据边界声明
- **持久化实验证据边界**：
  - `run_crash_harness.py` 验证的是 **VFS 崩溃一致性、文件原子替换协议状态机与 Failpoint 控制流**（验证在任何断电模拟点下目标文件绝不出现半写、截断或损坏）；
  - 该测试是在同机未重启的 Page Cache 环境下通过子进程 `SIGKILL` 触发，**不代表经过真实断电或物理断电后特定存储硬件的刷盘行为**（物理断电需依赖 POSIX `fdatasync` / `F_FULLFSYNC` 的操作系统与设备驱动正确实现）。

---

## 2. 自动化测试与独立复核结果

### 2.1 全量语言一致性套件 (Full Language Conformance Suite)
运行命令：
```bash
python3 tools/run_conformance.py
```
**实际产出**：
```
[PASSED] [io_datafile_buffer_contract_01] Run verified cleanly (exit code 0).
[PASSED] [io_datafile_concurrency_01] Run verified cleanly (exit code 0).
[PASSED] [io_datafile_owner_return_01] Run verified cleanly (exit code 0).
[PASSED] [io_durable_replace_01] Run verified cleanly (exit code 0).
[PASSED] [io_datafile_write_all_partial_01] Run verified cleanly (exit code 0).
[PASSED] [io_datafile_shared_view_01] Run verified cleanly (exit code 0).
[PASSED] [diag_datafile_private_handle_01] Compile-fail verified cleanly with code 'E0418'.
[PASSED] [diag_datafile_private_wal_fd_01] Compile-fail verified cleanly with code 'E0418'.

--- Conformance Suite Results: 285 Passed, 0 Failed ---
```

---

### 2.2 崩溃恢复与 VFS 原子一致性测试 (Crash Harness)
运行命令：
```bash
python3 tests/conformance/io/run_crash_harness.py
```
**实际产出**：
```
[Crash Harness] Compiling test runtime with -DTOKA_TESTING=1...
[Crash Harness] Compiling durable_replace_crash_target.tk...
[Crash Harness] Baseline durable replace OK.
[Crash Harness] Failpoint 'after_temp_write' recovery verified (target intact).
[Crash Harness] Failpoint 'after_temp_sync' recovery verified (target intact).
[Crash Harness] Failpoint 'after_rename' recovery verified (valid uncorrupted state: VERSION_FAILPOINT_3).
[Crash Harness] Failpoint 'after_parent_dir_sync' recovery verified (committed new version).

[Crash Harness] All Failpoint Visibility & Crash Recovery Invariants PASSED!
```

---

### 2.3 内存安全与数据竞争检测 (ASan & TSan Sanitizer Suite)
运行命令：
```bash
bash tools/scripts/test_sanitized_io.sh
```
**实际产出**：
```
=== [Sanitizer Runner] Testing Positional I/O under ASan / TSan (Darwin, /opt/homebrew/opt/llvm/bin/clang) ===
-> Building ASan runtime object...
-> Compiling conformance test object files...
-> Linking and running with AddressSanitizer (ASan)...
[Conformance] DataFile Buffer Contract PASSED!
[Conformance] DataFile Owner-Carrying Buffer Return & Error Paths PASSED!
[Conformance] DataFile Concurrency (Send + Sync RAII Shared Read) PASSED!
[Conformance] DataFile Partial/Short Write and Sync Error Invariants PASSED!
[Conformance] ReadDataFile Shared Read Receiver (self) PASSED!
-> All ASan checks PASSED!
-> Building TSan runtime and running ThreadSanitizer...
[Conformance] DataFile Concurrency (Send + Sync RAII Shared Read) PASSED!
-> All TSan checks PASSED!
=== [Sanitizer Runner] ALL ASAN + TSAN CHECKS PASSED CLEANLY! ===
```

---

### 2.4 Whitespace 检查
运行命令：
```bash
git diff --check
```
**实际产出**：Clean（退出码 0）。
