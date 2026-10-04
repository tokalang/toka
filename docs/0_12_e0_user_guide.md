# E0：从发布 SDK 完成两项真实任务

本指南使用冻结输入 c4c1030cd33504a9f5d8dc670ab4ab0c1fc515e7 和
Preview SDK 59b40e6dd659593871c644fc2f00a696606ab850。
SDK 归档及输入包由验收交付提供；无需编译器源码目录。
每名执行者使用独立安装目录和工作目录，保留第一次命令失败。
请自行执行下面步骤，不运行预置项目修复程序。
身份、命令、原始输出、文件修订、锁及交付摘要需要留存。

## 安装与执行记录

解压完整 SDK，确认归档摘要及 preview-sdk.json 的 candidate_sha；将 SDK/bin
放在 PATH 最前。不要设置 TOKA_LIB、TOKAC 或其他源码覆盖路径。
固定 inputs/fixtures 只读，任何修订都在新建的工作副本内。
可用交付中的 record_e0_command.py 保存命令；它只捕获命令输出和单调耗时，
不修改项目、不选择修复，也不调用上轮执行脚本。

每个任务先做冷执行，再从原始固定输入做一次热执行：热目录必须重新创建，
只复制本执行者冷任务的 .toka/cache 和 .toka/packages；Unicode 可复用冷任务
生成的 package.lock。不得复制修订后的源码或测试入口，以免漏过文档步骤。
“冷”仅表示空项目包缓存，不声称清空 OS/CDN 缓存。

## Unicode 消费项目

1. 在空任务目录运行 `toka new registry_unicode_consumer`。
2. 将 fixtures/source/registry_unicode_consumer 的 build.tk 和 src/main.tk
   复制到新项目。不要复制原 0.1.1 manifest/lock；它们是不可变旧输入。
3. 在项目目录运行 `toka add unicode:0.1.2`。新 package.lock 必须逐字节等于
   fixtures/expected-unicode-0.1.2.lock；其中 archive/content 摘要固定，不能换 latest。
4. 旧示例用 `'value` 表示模式绑定，而当前语言使用普通名字。将模式中的
   `Some('value)` 改为 `Some(value)`，相应引用 `'value` 改为 `value`。
   保留真正字符字面量，例如 `'B'`；不要删除所有单引号。
   将修订后的主入口复制为 tests/basic_test.tk；留存修订差异。
5. 依次运行 `toka check --json src/main.tk`、`toka build`、`toka test --json`、
   `toka run`。test 报告应 finalized=true、exit_code=0、passed=1；
   程序应打印 public Unicode registry consumer passed 并返回 0。
6. 比较 add 后及运行后的锁原始字节，必须不变。

## CSV 资源转换项目

1. 在另一空目录运行 `toka new csv_transform`，恢复
   fixtures/source/csv-transform 的 build.tk 和 src/main.tk。
2. 复制 fixtures/local-dependency 为项目相邻的 local-dependency 目录，
   将 fixtures/resources 复制到项目 resources；在项目目录执行
   `toka add ../local-dependency/e0_paths`。
3. 固定 csv-e0_test.tk 是旧的紧凑写法。先复制到 tests/basic_test.tk 留存原件，
   再按下面正常多行测试入口修订工作副本，不使用语句末尾分号：

```toka
import official/e0_paths::{input_path, output_path}
import std/process::{Command}
import std/fs::{exists}
fn main() -> i32 {
    auto command# = Command::new(string::from("./target/debug/csv_transform"))
    command#.arg(input_path())
    command#.arg(output_path())
    if command#.status() != 0 { return 1 }
    if !exists(output_path()) { return 2 }
    return 0
}
```

4. SDK 的 RFC 4180 CSV 解析器要求记录终止符为 CRLF。固定 input.csv 为 LF；
   在工作副本 resources/input.csv 中仅把行结束改为 CRLF，保留三条记录及所有
   字段内容、顺序和 UTF-8 编码。留存修订前后原始字节或十六进制及摘要。
5. 依次运行 `toka check --json src/main.tk`、`toka build`、`toka test --json`，
   然后 `toka run -- resources/input.csv output.csv`。
   `--` 分隔工具参数和应用参数；其后的空格、--forge 等均作为应用参数传递。
   单文件程序也使用 `toka run app.tk -- args...`。应用非零退出会原样返回。
6. 运行固定 fixtures/verify_csv.py 核验 output.csv：应保留 header 与 alpha/beta
   两条数据记录，依次为 name/value、alpha/1、beta/2。确认锁字节未变。

## 形成可运行交付物

每项任务按现有文件交付：package.tk、package.lock、build.tk、src、tests、
资源及 target/debug 中的实际应用二进制；CSV 额外包含本地依赖目录和 output.csv。
保存 tar.gz 及 SHA-256。将归档解压到新的目录，在项目目录直接运行交付二进制：
Unicode 无参数；CSV 用 resources/input.csv 和新的输出路径，再用固定 verifier 检查。
这一步不得重新编译，也不得借原构建目录查找资源。
本轮不要求跨宿主重新锁定源码依赖；交付中原锁保留原宿主身份。

冷/热各保留命令原始状态、JSON、锁前后字节、修订 diff、资源原始字节、交付包和
运行回执。修复轮次按一次决策/尝试计数，文件改动数另列；主动按文档迁移也须记录。
与 0.11 的上轮失败对照保持原样，本批无需重复其不支持的命令。

## 网络获取计时的解释

独立网络探针直接调用同一冻结 SDK 的下载函数，分别获取 registry catalog 和
catalog 中 unicode 0.1.2 的真实 tarball，校验原包摘要。计时边界从发起请求之前
开始，到完整响应读完且目标文件关闭；包含 DNS/TLS/重定向、读取及本地写入，
不含校验、解压、依赖图解析。它不写 SDK 项目缓存，也不插桩 add。
因此这是独立依赖获取样本，不是 add 内部网络占比，重复请求也不等于 SDK 热任务。
`add` 记录继续标为获取/解析等工作的聚合耗时；不可观测的内部网络时间仍为 null。
