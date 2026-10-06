# 消费同名本地库

适用：0.13 B1 开发契约；0.12 已发布 SDK 的任意别名重命名不在本示例支持范围。示例须使用本批实现或后续正式候选。命令顺序沿用现有 CLI：名称在前，--lib 在后。

```sh
mkdir package-entry-example
cd package-entry-example
toka new directory-digest --lib
toka new digest-cli
```

把 directory-digest/lib/directory-digest/mod.tk 替换为：

```toka
pub fn answer() -> i32 { return 42 }
```

进入消费项目并锁定：

```sh
cd digest-cli
toka add ../directory-digest --alias directory-digest
```

add 成功表示依赖解析和锁定成功，还没有证明下面的消费构建通过。保留 package.lock；不要手改它。

把 src/main.tk 替换为：

```toka
import directory-digest::{answer}
fn main() -> i32 {
    if answer() != 42 { return 1 }
    return 0
}
```

创建 tests/answer_test.tk，内容同上。使用根入口 directory-digest，不能写 directory-digest/mod，也不要把别名改成 dir_digest。

```sh
mkdir -p tests
# 将上述完整代码保存为 tests/answer_test.tk
toka check src/main.tk
toka build
toka test --json
./target/debug/digest-cli
cp package.lock package.lock.before
# 再次执行同一锁，不 add、不 fetch、不 update：
toka check src/main.tk
toka build
toka test --json
./target/debug/digest-cli
cmp package.lock.before package.lock
```

预期：各命令退出0；test 是 toka.test-report v1，finalized=true、result=passed、exit_code=0，所有选中测试 passed；程序不输出文本、返回0。cmp 返回0证明锁原始字节一致，验收同时核对锁节点身份。

失败时：不同别名核对库名与lib/NAME/mod.tk并显式修正依赖后fetch；缺入口恢复文件并显式重新锁定；错误import按错误提供的受支持入口修改。读取/完整性/工具故障按原错误检查，不靠反复fetch掩盖。check --json 的入口准备失败为 resolve-report v1，不能当成编译器语义失败报告；test 入口准备失败为C6 configuration_error/2、context阶段、测试not_run。

本地定向脚本 test_package_entry.py 对此完整流程实际创建文件并验证；本页不声称目录摘要真实使用轮已完成，原轮阻断及原锁未重放的记录仍保留。
