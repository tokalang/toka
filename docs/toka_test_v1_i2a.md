# toka test I2-A：进程监督候选

状态：Preview；固定候选 `eacc3f3e8ede815fd0bab68b1eef6d1b6bb978f9` 的三核心平台
定向验收已通过，但独立复审要求 P2 修订，尚未 Accepted。I1 候选 `8b685ef0` 已 Accepted；
本批仅实现生命周期，不宣布稳定 v1，不实现 C6 JSON 或 #41 来源分类。

## 预算与监督范围

| 阶段 | 默认预算 | 归属与失败 |
| --- | --- | --- |
| compiler probe | 当前 compile_ms，独立调用 | 身份准备；超时返回 2 |
| dependency context | 180000 ms | 调用级准备保护；保留 fetch 网络/离线规则；超时返回 2 |
| native inputs | 120000 ms | native plan、pkg-config、宿主 C 编译及对象准备共享；超时返回 2 |
| 每项 compile_link | 30000 ms，可用既有 CLI 覆盖 | B0 直接 -O0 编译/链接边界；已确认清理的超时返回 1 |
| 每项 run | 5000 ms，可用既有 CLI 覆盖 | B0 运行边界；已确认清理的超时返回 1 |

准备保护值属于政策，不冒称测量推导。所有阶段采用单调时钟，从受控启动算起。
本批不把整个 toka build 放入 30 秒预算。准备期间不会启动测试项。

POSIX 每阶段创建 session/process group。Linux /proc 与 Darwin proc_listpids
只读取锚定组成员判断残留；取消始终针对该组，不逐个取消枚举 PID。waitid(WNOWAIT)
保留直接 child 身份，最后一次信号之前不 wait/reap；失去 wait 权则停止信号。
正常退出但留下组成员也触发失败与清理。TERM grace 2000 ms、KILL 确认 5000 ms；
最终同时确认直接 child 已回收、组 ESRCH、日志关闭，否则返回 2 并停止调度。

捕获日志写入独立普通文件，没有必须等待输出 EOF 的未排空管道；受控组退出与
文件关闭共同构成确认。逃离组的后代不在监督承诺内；SIGKILL/断电没有最终报告保证。
Windows 非空选中集合仍返回 2，没有 managed backend 承诺。

## 命令入口与中断

POSIX manager 使用 argv（无 shell）execvp 交接给 Python supervisor，保留实际命令 PID。
SIGINT/SIGTERM 到该 PID 均进入同一有界清理，成功后 130；重复中断不跳过清理。
基础设施错误优先于中断/超时，返回 2；后项保留 not_run。

dependency/native helper 是独立受控 worker，调用的子工具继承其组。项目写入锁由
context worker 持有；中断退出会释放 OS 锁，不以源码补路径或重写锁文件规避依赖。

## 验收与证据边界

`tools/scripts/test_toka_test_i2a.py` 记录控制用例与安装 SDK 场景；故障注入只发生于
测试 seam，不增加生产环境 bypass。安装用例从真实编译的 toka manager 入口启动，
中断只发给 manager PID；受控 compiler fixture 与原 SDK compiler 的场景分开记录。
I1 项目发现/依赖/并发/串行用例同步回归。

内部 `preview.json` 为 `toka.test-preview-i2a`，不是稳定 C6 报告；缺 Python/helper
的外层 JSON 错误、可信诊断来源、实时输出与固定 phase 对象留到 I2-B。
本批定向三核心平台任务不替代 I2-C 最终安装 SDK 验收，也不启动 Q0 全资格矩阵。
Unicode 包原始失败路径与不可变 0.11 SDK 保留，受控 registry 夹具不替代该验收。
平台政策仍是独立提交。

## 固定候选回执

[定向运行 36973989245](https://github.com/tokalang/toka/actions/runs/36973989245)，attempt 1，
三个目标绑定同一上述 SHA。每目标通过 14 组 I2-A 控制、16 组 I1 控制、供应链资格、
16 个安装生命周期场景和 10 个 I1 安装回归场景。I2-A 合计保存 48 个场景回执与
228 个清理 confirmed 的阶段；故障注入控制另外验证返回 2 和停止调度。

证据位于 `evidence/0.12-i2a/delivery-eacc3f3e`（仓库外的 tokalang evidence 根）。
三份远端 ZIP 摘要、提取字节、源码 helper 摘要和原 SDK 编译器/runtime 身份已回读核验。
旧 `09347bef` 与本地探索记录单独留存，不混入该 SHA 的结果。本记录不自行标记
I2-A Accepted；Preview、I2-B/I2-C 和 Q0 边界仍按上文保持。

## I2-A P2 独立修订

`eacc3f3e` 的证据通过核验，但独立复审要求先修 I2A-P2-1/2，尚未 Accepted。
本次修订仍为 Preview，仅收闭两个边界：

- 本次有效 compile_ms 传入 context worker 和 OS 写入锁等待；每次获取尝试前
  检查 deadline。锁等待超限返回 2，全部入口 not_run；180 秒 context 与 120 秒
  native 保护预算继续独立，不用固定 30 秒替代 CLI 覆盖值。
- 汇总之后先写 `finalized=false` 的暂存回执，随后屏蔽 SIGINT/SIGTERM，收集已观察
  和待处理信号，再固定最终结果快照。该快照是调用的结果提交边界；此前汇总或
  暂存持久化期间的中断返回 130，基础设施错误仍优先返回 2。固定快照后的信号
  属于完成后的事件，不再修改回执或返回值。最终回执为 finalized=true；持久化
  失败仍返回 2，并尽力保存失败回执。不能将暂存回执当作完成结果。

新增控制覆盖 CLI 锁等待覆盖值、最终汇总中断、暂存写入中断、基础设施与中断
优先级，以及暂存持久化失败。安装场景额外从真实 manager 入口持有 OS 文件锁，
以 compile_ms=2000 验证返回 2、context 清理确认且所有入口 not_run。
正式 JSON/诊断来源仍需修订复审通过后进入 I2-B；本批不启动完整发布矩阵。

## 修订复审结果

`54ca45a8` 的独立复审已通过，两项 P2 关闭，I2-A Accepted（仅 Preview 范围）。
21 组监督与 16 组 I1 控制、三份远端 artifact、1493 文件摘要、51 个生命周期
回执与 234 个阶段确认均通过独立核验。锁预算与提交边界控制继续作为 I2-B 回归。
