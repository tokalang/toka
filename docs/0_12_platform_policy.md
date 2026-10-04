# 0.12 发布平台政策

本政策实现为独立 PL 提交，待复审；不宣布 0.12 已取得资格。I2-C、I3 功能验收已
完成，Preview 继续保留；PL/E0 完成后才冻结 Q0 候选。

| 版本 | 完整阻断资格及原包重放 | 独立可选目标 |
| --- | --- | --- |
| 0.11.x | Linux x64、Linux ARM64、macOS ARM64、macOS x64 | 无；原四平台规则及 Intel 重放不放宽 |
| 0.12.x | Linux x64、Linux ARM64、macOS ARM64 | macOS x64，基本验证通过后才接纳其原包 |

0.12 的 policy_id 为 `toka.release-platforms.0.12.v1`。核心 summary v2 明确记录
expected_core_targets、source_run_id/attempt 与 optional_targets.macos-x64；核心
workflow 不依赖 Intel job。Intel 只有 not_run/running/passed/failed 四种状态，必须
有原因；非 passed 状态没有可发布资产、receipt 或 validation_level。0.11 summary
v1 的四平台字段和旧 Intel receipt 保持不变；不能用新 summary 追认历史版本。

独立 `optional_macos_x64.yml` 仅构建 SDK、执行 basic，不运行完整 release_gate。
验证 actual package 的安装、工具版本、创建、编译链接运行与 test 成功/非零/超时；
四个工具 tokac/toka/tokafmt/tokalsp 均执行版本检查并保留原始输出及二进制身份。
创建后加入固定非空本地依赖 basic_dep，应用与三项测试实际导入调用它；build/run/
test 前后锁字节不变，content 摘要、锁节点与 C6 dependencies/identity 均核对一致。
test 成功、失败、超时回执都须为 finalized 的单项 C6 报告。三项均先 compile_link
成功；失败必须 run_failed 且运行原始退出码 7；超时必须已启动的 run 阶段超时，
并确认 leader 回收、进程组消失及输出完成。编译失败或编译超时不能替代运行路径。
超时清理须确认。源码在包与 harness 保存后从一次性 runner 删除，运行目录没有
编译器源码。只有同 candidate/version、clean、归档 SHA 和 source run/attempt 完整
的 `toka.sdk-basic-validation` 可以把 optional 状态置为 passed/basic。full 标签
另需完整 release_gate、TaskHandle 与取消契约证据，不能用 basic 冒充。

## 资产与推广

0.12 只接受三核心包 + SHA256SUMS，或这一集合加唯一经验证的 Intel 包。额外未知
文件、缺包、重复、软链接、未验证 Intel、错版本/SHA/摘要均拒绝。optional 包由
显式 optional_run_id 接纳，不能因为某处存在 Intel 文件就自动加入。

fetch_policy_candidate.py 认证 source run，保存服务器 artifact ID/name/digest 和 ZIP
原字节；校验 ZIP 摘要及唯一 archive 内容。draft/推广验证器同时绑定 core run/attempt、
可选 run/attempt、policy、candidate/version、exact asset set 与 archive SHA。原包
复制后再次比较，不重建 SDK。draft 入口仍由 GITHUB_TOKEN 建 annotated tag，防止
重复 tag matrix；tag push 的 0.12 资格也不会自动创建另一份 draft。

0.12 重放入口选择实际资产集，三核心包均在各自原生宿主重放；包含 Intel 时也重放
其实际原包。汇总 v2 与受保护 release-publication 推广使用同一资产集。发布字段、
Latest 仍由已验证的推广步骤修改，PL 控制不执行这些远端写入。

安装器读取指定版本 SHA256SUMS，按本机目标取包并校验。0.12 Intel 无包时明确
指出版本/目标和源构建链接，不装 ARM 包、回退版本或跟随 Latest。历史精确标签
安装继续支持，0.11 的记录与支持承诺不改变。

## PL 证据与控制边界

test_release_platform_policy.py 覆盖 V01–V06、V09–V12 的结构化正反向控制，包含
attempt/policy/ZIP/source/bytes 错配；test_platform_installer.py 覆盖 V07/V08 的平台
选择、checksum 与缺包行为。后者使用明确标注的合成安装包，不冒充真实 SDK。

PL 定向 job 对四个原生宿主执行 basic runner 的真实包控制：固定 0.11 原编译器与
candidate manager/helpers 构成 Preview 归档，完整记录 create/build/run/test。
它产生 `toka.sdk-basic-control`，**不接纳为 0.12 Intel 资产回执**。未来可提供的
真正 0.12 Intel 包必须独立执行上述 formal basic workflow；当前不声称已有该包。
这与“不提供未验证可选包”的状态边界一致，不把 PL 控制当 Q0。

验收编号 V01–V12 见 toka_test_v1_acceptance.md；E0 输入另行固定并执行。
