# Toka 依赖 Manifest JSON 协议标准 (v1.0.0)

本协议定义了 `tokac --dump-dependencies=json` 的机器可读依赖关系图格式，用以支持外部构建引擎、包管理器以及增量构建决策驱动器实现模块的依赖收集 and 脏状态检查。

## 示例数据结构

```json
{
  "manifest_version": "1.0.0",
  "roots": [
    "/absolute/path/to/src/main.tk"
  ],
  "modules": {
    "/absolute/path/to/src/main.tk": {
      "kind": "source",
      "fallback_triggered": false,
      "cache_status": "Ok",
      "cache_status_reason": "",
      "target_triple": "x86_64-unknown-linux-gnu",
      "compiler_version": "0.9.8",
      "interface_version": "1",
      "source_hash": "a4d8c52d8e412bc4",
      "content_hash": "a4d8c52d8e412bc4",
      "outputs": {
        "interface": "/absolute/path/to/build/main.tki",
        "object": "",
        "executable": "/absolute/path/to/build/main_app"
      },
      "dependencies": [
        "/absolute/path/to/src/lib.tk"
      ]
    },
    "/absolute/path/to/src/lib.tk": {
      "kind": "source",
      "fallback_triggered": false,
      "cache_status": "Ok",
      "cache_status_reason": "",
      "target_triple": "x86_64-unknown-linux-gnu",
      "compiler_version": "0.9.8",
      "interface_version": "1",
      "source_hash": "b2f8c51a8e1329c2",
      "content_hash": "b2f8c51a8e1329c2",
      "outputs": {
        "interface": "",
        "object": "",
        "executable": ""
      },
      "dependencies": []
    }
  }
}
```

---

## 顶层字段说明

### 1. `manifest_version`
* **类型**：`string`
* **定义**：协议版本号。当前固定的主版本号为 `"1.0.0"`。外部工具在解析时应验证该版本号，若主版本号不匹配，应拒绝解析并使缓存失效。

### 2. `roots`
* **类型**：`array of string`
* **定义**：本次编译事务的顶级根入口文件（Root Modules）的绝对物理规范化路径（Canonical Paths）列表。

### 3. `modules`
* **类型**：`object` (以 Canonical 路径为 Key 的 Map 结构)
* **定义**：所有参与编译的模块及其依赖项的详细元数据与编译状态快照。

---

## 模块快照字段说明

对于 `modules` 字典中的每一个模块节点：

### 1. `kind`
* **类型**：`string`
* **取值范围**：`"source"` 或 `"interface"`
* **说明**：指示当前模块的物理载体是 `.tk` 源码文件（`source`）还是编译生成的 `.tki` 接口声明文件（`interface`）。

### 2. `fallback_triggered`
* **类型**：`boolean`
* **说明**：指示当前模块在加载时是否触发了回退行为。当试图以 `interface` 加载失效的 `.tki` 但存在对应的 `.tk` 时，该字段为 `true`。

### 3. `cache_status`
* **类型**：`string`
* **说明**：强类型缓存失效检测状态码。常见的状态码包括：
  - `Ok`：缓存完好并成功使用。
  - `MissingCompilerVersion`：接口文件缺失编译器版本信息。
  - `CompilerVersionMismatch`：接口文件的编译器版本与当前不匹配。
  - `TargetTripleMismatch`：目标平台三元组不匹配。
  - `SourceHashMismatch`：源码已被修改导致接口哈希过期。

### 4. `cache_status_reason`
* **类型**：`string`
* **说明**：缓存失效的具体人类可读描述。在 `cache_status` 不为 `Ok` 时提供详细诊断说明。

### 5. `target_triple`
* **类型**：`string`
* **说明**：编译该模块时的目标平台三元组（例如 `x86_64-unknown-linux-gnu`）。

### 6. `compiler_version`
* **类型**：`string`
* **说明**：编译该模块时对应的编译器物理版本号。

### 7. `interface_version`
* **类型**：`string`
* **说明**：接口表达格式的版本，当前为常数 `"1"`。

### 8. `source_hash`
* **类型**：`string` (16位 FNV-1a 64位十六进制哈希值)
* **说明**：该模块对应的**原始 `.tk` 源码**的逻辑哈希快照。对于接口模块，这是生成它的源码文件的哈希，用于检测源码是否过期。

### 9. `content_hash`
* **类型**：`string` (16位 FNV-1a 64位十六进制哈希值)
* **说明**：该模块**实际被加载的物理文件**（即 `kind` 为 `interface` 时对 `.tki` 文件，`kind` 为 `source` 时对 `.tk` 文件）的当前物理哈希快照。

### 10. `outputs`
* **类型**：`object`
* **说明**：模块被成功编译后预期的输出产物物理路径：
  - `interface`：输出的 `.tki` 接口物理绝对路径。仅对入口根模块输出。
  - `object`：开启 `--emit-obj` 并启用 `-c` 时输出的 `.o` 临时或目标对象物理路径。
  - `executable`：非仅编译模式下最终链接生成的可执行文件物理绝对路径。
  - *注：在 `tokac` 编译器原始导出的依赖图 JSON 中，非根模块的所有 outputs 路径均置空 `""`。而在外部构建驱动器（如 `toka_build.py`）持久化增强的 `manifest.json` 中，非根模块的 `outputs` 会被自动注入并记录其分立编译时对应的唯一缓存 `.o`（object）与 `.tki`（interface）产物的物理绝对路径，以便于后续增量编译状态比对与依赖链接收集。*

### 11. `dependencies`
* **类型**：`array of string`
* **说明**：当前模块直接 import 的所有子模块的 Canonical 绝对路径列表。
