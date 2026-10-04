# 0.12.0 Q0 冻结与资格范围

前置 I2-C、I3、PL、E0 均已独立复审 Accepted。本批仅执行 Q0 和原包重放，
候选继续 Preview，不创建标签/draft，不公开发布或改变 Latest。

源码默认版本及显式 qualification label 均为 0.12.0 / v0.12.0。
不可变完整 SHA 写入冻结回执、workflow_dispatch candidate_ref、三个 gate 报告、
资格 summary 及 replay receipt；不在本文件内自引用提交 SHA。

平台 policy 为 toka.release-platforms.0.12.v1：Linux x64、Linux ARM64、macOS ARM64
是完整阻断核心目标。本轮 macOS x64 不提供包，optional 状态为 not_run；不能冒充 basic
或 full。Windows GNU x64 仍是源构建/dogfood，不纳入发布 SDK 资产。

完整 SDK 必须由本 SHA 的 CMake/编译器/manager/fmt/LSP、标准库与 helpers 构建，
四工具均报告 0.12.0；旧 composite Preview 包不能替代。release_gate 的全部阶段、
TaskHandle/取消契约与 version-bound summary 必须通过，再冻结三个原 tar.gz、
服务器 artifact 身份/digest、归档摘要与 source run/attempt。

资格通过后通过 qualified_artifact_replay.yml 的 candidate_only=true 路径，在三个原生
宿主认证 artifact ZIP digest 并重放同一 tar.gz，不要求先有版本标签，也不重新构建。
0.11 的路径仍要求 annotated tag；0.12 默认 tagged 路径也保持该要求。
候选路径只接纳 candidate_run，校验完整 SHA、版本、资格运行与精确资产集合。
每包执行 formal basic（四工具、非空锁定依赖、真实 compile/run、test 成功/非零/运行超时）
及新 add/evidence/argv 安装回归，原包的 archive_sha256 写入回执。

Q0 完成及原包重放自检通过后交付独立复审。只有复审 Accepted 后才进入 draft／
受保护发布阶段；公开发布和 Latest 不在本次授权范围。
