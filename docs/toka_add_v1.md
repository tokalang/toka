# I3-A：add 解析反馈契约（#39）

候选，待独立复审；不改变 I2-C 已 Accepted 的 SDK 8341ab9b。Preview 保留。

命令：`toka add <package-or-url> [--alias name] [--json]`。
裸 registry 名称保持 requested 原输入，实际 selector 为 latest；明确版本
使用 name:version。本地路径可用 --alias 固定别名；固定 Git revision 可用
`Git("URL", commit="完整 SHA")` 加 --alias。已有别名拒绝，不能静默覆盖。

add 调用实际 SDK 的包 helper，不从用户工程或相邻源码目录选择 helper。
resolver worker 使用固定协议 toka.resolve-report/version=1，返回完整锁条目
和节点。消费者在显示成功前逐项比较磁盘 lock、节点、已安装内容摘要；成功
退出但空 JSON、缺字段或错节点均失败。失败保留原 worker stdout/stderr 的
base64 及退出码，恢复 manifest 和原 lock；留下的可验证缓存不伪装成 add 成功。

人类成功摘要为 `Added alias: identity [kind] sha256=前12位`。registry identity
为已解析版本，摘要为已校验 archive；本地使用实际路径、Git 使用实际 revision，
摘要为内容摘要。不显示 latest 冒充 resolved，不伪造本地/Git registry 版本。

--json stdout 为单一 toka.add-report/version=1。成功为 added/0，字段：
requested、alias、kind、locator、requested_version、resolved_version、
resolved_revision、resolved_path、archive_sha256、content_sha256、
package_node_id、lock_sha256、manifest_sha256、errors。类型不适用的字段为 null，
完整摘要用于身份，前缀仅供显示。失败为 failed/1，错误记录原因及 worker
详情，不生成成功节点和成功摘要；stderr 同时保留原 helper 根因。

F01–F05 验证 registry latest/明确版本、缺版本/坏摘要、JSON、路径/固定 Git，
以及成功 worker 的空输出/错节点。源控制和私有 CLI 重放不能自行等同三平台
安装验收，固定候选 SDK 后再提交证据。#38 的 S01–S04 是后续独立批次；
PL、E0、Q0 与发布仍未完成，本项不启动完整发布资格矩阵。
