# 0.12 整版路线与当前阻断

状态：2026-10-04，I2-C、I3、PL、E0 均已独立复审 Accepted。
当前进入 Q0，继续 Preview；源码版本冻结为 0.12.0，最终完整 SHA 由冻结回执及资格运行绑定。

| 里程碑 | 当前状态 | 依据与剩余完成条件 |
| --- | --- | --- |
| I2-C 主功能 | Accepted | SDK 8341ab9b，三核心平台 95/95；原失败保留 |
| I3 开发反馈 | Accepted | I3-A 3f5c4d68；I3-B 01b6bce3 |
| PL 平台迁移 | Accepted | 0e4da202；三个核心平台完整阻断，Intel 独立尽力支持 |
| E0 真实项目 | Accepted | 最终指南 7e9a3122；两名独立 AI 8/8、12 条获取计时、Linux 原包 4/4；旧失败及修订保留 |
| Q0／原包重放 | 准备冻结并启动 | 同一 SHA 构建完整 SDK，三核心资格通过，固定 run/attempt 后原字节重放 |
| draft／受保护发布 | 未启动 | Q0 复审通过后进入；公开发布与 Latest 另需授权 |

本次 Q0 不包含 macOS x64 资产；optional 状态明确为 not_run，不能记为验证通过。
Windows GNU x64 保持源构建/dogfood 范围。0.11 四平台记录不变。

复审记录：
[PL](/Users/zhyi/GitDP/tokalang/evidence/0.12-platform-policy-p2-independent-review/verification.json)、
[E0](/Users/zhyi/GitDP/tokalang/evidence/0.12-e0-closure-independent-review/verification.json)。
资格构建不使用此前 composite Preview 包；原包重放不重新构建 SDK。
