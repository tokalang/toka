# 线程所有权与错误处理（新协议实施预览）

新协议目前处于独立实现与验证阶段，**尚未接入 `std/thread`**。本文说明已接受的
公共行为，不表示当前源码 API 已完成迁移。完整设计见
[线程环境与结果责任通道](semantic_core/review/thread_handoff_ab_design.md)。

## join、detach 与句柄销毁

- join 成功时等待 native thread 真正终止，再取得返回值的唯一所有权。
- 显式 join 或 detach 失败时返回带 native 错误码的 `ThreadError`，原句柄仍然有效，
  不会提前丢失后续 join／detach 的能力。
- detach 不等待 worker 完成。结果尚未完成时由 worker 最终丢弃；已经完成时可由
  detach 调用方执行结果析构。用户析构可能耗时，因此“不等待 worker”不等于
  detach 调用本身是 wait-free 或耗时有界。
- 成功 join／detach 关闭句柄。重复显式操作返回 Closed，不再次调用 native API。
  已关闭句柄的隐式 Drop 是 no-op；句柄只能移动，不复制。

**活句柄的隐式 Drop 会尝试 detach。若 native detach 失败，程序在释放内部状态锁后，
使用无分配 fatal 路径终止进程。** Drop 没有可返回错误的位置；这里不静默泄漏，
不无限重试，也不把 Drop 偷换成阻塞 join。**不承诺进程终止路径会运行全部用户析构。**
需要处理错误的应用应显式调用 join／detach，并处理失败后仍持有的句柄。
当前 native 核心通过 `_Exit(134)` 直接终止：不写 stderr、不刷新 stdio、不运行
atexit／SIGABRT 用户处理器，因此 stderr 管道堵满或读端关闭不会阻止这条退出路径。
这不是可恢复错误，也不承诺生成 core dump 或 fatal 日志。

## 析构在哪个线程执行

| 资源／情形 | 析构执行者 |
|---|---|
| 准备或 create 失败的输入／环境 | 创建线程 |
| 已启动任务的输入／capture | worker |
| join 交给调用方的返回值 | 最终 owner，遵循普通作用域规则 |
| 先 detach、后完成的返回值 | worker |
| 先完成、后 detach 的返回值 | detach 调用线程 |

invoke 和用户析构均不持内部状态 mutex。`@Send` 检查不能替代环境及返回值的真实
生命周期证明；未知 borrowed/raw/callable 依赖不得从类型擦除中获得所有权或静态寿命。

## 接口变化

设计将 spawn 改为 `Result<JoinHandle<T>, ThreadError>`，显式 detach 也返回 Result；
JoinHandle 将持有不透明控制块责任，不再公开 native tid。错误描述使用不分配的
kind/native_code；错误消息的字符串格式化不参与责任交接。

输入显式交出后，即使 spawn 失败也不会“复活”原变量；未启动环境在创建线程清理。
新的 compiler/private runtime 协议使用版本门禁，不能混用旧 TKI／缓存／runtime。
不支持绕过句柄直接对内部 pthread_t 重复 join/detach，或从 unsafe alias 制造第二个 owner。
首批不提供 native cancellation、跨 C unwind 或动态卸载清理代码的资格承诺。
