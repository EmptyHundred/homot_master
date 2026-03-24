# LSPA — Language Server Protocol for Agents

## 概述

LSPA (Language Server Protocol for Agents) 是专为 AI Agent 设计的代码智能系统，替代传统 LSP 在 AI 辅助编码场景中的角色。它提供 API 查询、代码辅助、静态分析三大能力，统一在一个工具中，取代现有的 api-query MCP 服务。

### 为什么不直接用 LSP

LSP 是为人类编辑器设计的，与 AI Agent 的工作方式存在根本性的阻抗不匹配：

| 维度 | LSP (为人类编辑器) | AI Agent 实际需要 |
|------|-------------------|------------------|
| 交互模型 | 文档生命周期 (open/edit/close) | 无状态查询 (问完即走) |
| 查询粒度 | 光标位置 (line:col) | 符号/类级别 ("Node3D 有什么方法") |
| 输出格式 | Markdown/UI 控件 (给人看) | 结构化紧凑文本 (给 LLM 吃 token) |
| 搜索方式 | 逐字触发补全 | 语义搜索 ("碰撞检测怎么做") |
| 代码分析 | 增量 (每次按键重分析) | 批量 (写完一起 lint) |
| 上下文 | 当前光标处单点 | 整个项目/多文件 |
| 响应优化 | 低延迟 (打字体验) | token 效率 (塞进上下文窗口) |

LSPA 从 AI Agent 的需求出发，重新设计查询接口和输出格式。

### 替代的现有系统

LSPA 替代 `hmmaker/Information/Provider/api-query/` 整套系统：

| 被替代的组件 | 说明 |
|---|---|
| `server.py` | Python MCP server (SQLite + FTS5 + 向量嵌入) |
| `build_index.py` | 从 godot-sandbox.d.ts 构建 knowledge_base.db |
| `build_embeddings.py` | 构建向量嵌入 |
| `knowledge_base.db` | SQLite 全文搜索数据库 |
| `embeddings.npz` | 语义搜索向量 |
| `godot-sandbox.d.ts` | 手动维护的 TypeScript 类型声明 |

替代后的数据源：**`linterdb.json`** — 由引擎通过 `homot --dump-linterdb` 自动生成，包含 ~1025 个原生类、~39 个内置类型、全局函数/枚举/常量及完整文档。

---

## 架构

### 系统架构

```
┌─────────────────────────────┐
│        homot-lspa           │   单一可执行文件 (C++)
│     stdin/stdout JSON-RPC   │
├──────┬──────────┬───────────┤
│DISCOVER│ WRITE   │  VERIFY  │   三个能力域
│api/*  │ code/*  │ verify/* │
├──────┴──────────┴───────────┤
│         QueryEngine         │   linterdb.json 内存索引
│  - ClassIndex (HashMap)     │   + 关键词搜索
│  - InheritanceTree          │   + 继承链遍历
│  - DocStore                 │   + 文档查询
├─────────────────────────────┤
│      GDScript Analyzer      │   复用 homot 现有解析器
│  (Parser + TypeChecker)     │   用于 code/* 和 verify/*
├─────────────────────────────┤
│    JSON-RPC Transport       │   复用 homot 现有传输层
│      stdin/stdout           │
└─────────────────────────────┘
```

### 复用 homot 已有组件

| 组件 | 来源 | 用途 |
|------|------|------|
| LinterDB | `linter/stubs/linterdb.h/.cpp` | 加载 linterdb.json, 类/方法/属性查询 |
| ClassDB Stub | `linter/stubs/classdb_stub.h/.cpp` | 为 GDScript 分析器提供类型信息 |
| ScriptServer Stub | `linter/stubs/script_server_stub.h/.cpp` | 跨文件 class_name 解析 |
| GDScriptParser | `modules/gdscript/gdscript_parser.h` | 解析 GDScript 源码 |
| GDScriptAnalyzer | `modules/gdscript/gdscript_analyzer.h` | 类型检查和语义分析 |
| JSON-RPC Transport | `linter/lsp/lsp_transport.h/.cpp` | stdin/stdout 消息传输 |
| Engine Bootstrap | `linter/lsp/lsp_main.cpp` | 最小引擎初始化 |

### 新增组件

| 组件 | 文件 | 职责 |
|------|------|------|
| LSPA Main | `linter/lspa/lspa_main.cpp` | 入口, 引擎 bootstrap, 消息循环 |
| LSPA Server | `linter/lspa/lspa_server.h/.cpp` | 请求分发 |
| QueryEngine | `linter/lspa/query_engine.h/.cpp` | linterdb 上的索引和搜索 |
| CodeAnalyzer | `linter/lspa/code_analyzer.h/.cpp` | 代码片段分析 (typeof/complete) |
| Verifier | `linter/lspa/verifier.h/.cpp` | 批量 lint + 契约校验 |
| Formatter | `linter/lspa/formatter.h/.cpp` | Token-budget-aware 输出格式化 |

### 构建

与 homot-linter / homot-lsp 共用构建系统：

```bash
scons platform=windows target=template_debug linter=yes
# 产出: bin/linter/homot-lspa.exe (alongside homot-linter.exe and homot-lsp.exe)
```

---

## 协议规范

传输层：JSON-RPC 2.0 over stdin/stdout (Content-Length 头分帧)

### 生命周期

```
Client                          Server
  |                               |
  |--- initialize --------------→ |   初始化, 返回 capabilities
  |←-- initialize result ---------|
  |                               |
  |--- api/class ---------------→ |   查询请求 (无状态, 任意顺序)
  |←-- result -------------------|
  |                               |
  |--- verify/lint -------------→ |
  |←-- result -------------------|
  |                               |
  |--- shutdown ----------------→ |   关闭
  |←-- result -------------------|
  |                               |
```

无需 `textDocument/didOpen` 等文档生命周期管理。每个请求独立，无状态。

### Initialize

```jsonc
// Request
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "initialize",
  "params": {}
}

// Response
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "name": "homot-lspa",
    "version": "0.1.0",
    "capabilities": {
      "discover": ["class", "classes", "search", "hierarchy", "catalog", "globals"],
      "write": ["typeof", "signature", "complete"],
      "verify": ["lint", "check", "contract"]
    },
    "stats": {
      "classes": 1025,
      "builtin_types": 39,
      "singletons": 39,
      "utility_functions": 114
    }
  }
}
```

---

## DISCOVER 域 — "我能用什么？"

AI Agent 在写代码前需要了解可用的 API。DISCOVER 域提供类级别的查询能力。

### api/class

查询单个类的完整信息。

```jsonc
// Request
{
  "method": "api/class",
  "params": {
    "name": "CharacterBody3D",     // 类名 (大小写不敏感, 支持模糊匹配)
    "detail": "standard",          // "names_only" | "standard" | "full"
    "sections": null               // null=全部 | ["methods", "signals"] 只返回指定部分
  }
}

// Response (detail="standard")
{
  "result": {
    "class": "CharacterBody3D",
    "extends": "PhysicsBody3D",
    "brief": "A 3D physics body specialized for characters moved by script.",
    "properties": [
      { "name": "velocity", "type": "Vector3", "desc": "Current velocity vector." },
      { "name": "floor_max_angle", "type": "float", "desc": "..." }
    ],
    "methods": [
      { "name": "move_and_slide", "sig": "() -> bool", "desc": "Moves the body..." },
      { "name": "is_on_floor", "sig": "() -> bool" },
      { "name": "get_slide_collision", "sig": "(idx: int) -> KinematicCollision3D" }
    ],
    "signals": [
      { "name": "floor_changed", "args": "" }
    ],
    "enums": {},
    "constants": {}
  }
}

// Response (detail="names_only") — 最紧凑, 用于浏览
{
  "result": {
    "class": "CharacterBody3D",
    "extends": "PhysicsBody3D",
    "properties": ["velocity", "floor_max_angle", "floor_snap_length", ...],
    "methods": ["move_and_slide", "is_on_floor", "is_on_wall", ...],
    "signals": ["floor_changed"]
  }
}
```

**detail 级别说明**：
- `names_only` — 只有成员名字列表, ~200 tokens
- `standard` — 名字 + 签名, 省略空描述, 过滤 getter/setter, ~800 tokens
- `full` — 完整签名 + 描述 + 文档, ~2000+ tokens

### api/classes

批量查询多个类（减少 round-trip）。

```jsonc
{
  "method": "api/classes",
  "params": {
    "names": ["Node3D", "Area3D", "CharacterBody3D"],
    "detail": "standard"
  }
}

// Response
{
  "result": {
    "found": {
      "Node3D": { ... },
      "Area3D": { ... },
      "CharacterBody3D": { ... }
    },
    "not_found": []
  }
}
```

### api/search

跨所有类搜索 API（关键词匹配）。替代原 `search_api` 和 `search_infra`。

```jsonc
{
  "method": "api/search",
  "params": {
    "query": "collision body entered",
    "filter": null,             // null | "method" | "property" | "signal" | "enum" | "constant"
    "class_filter": null,       // null | "Area3D" (限定在某个类中搜索)
    "limit": 10
  }
}

// Response
{
  "result": [
    { "class": "Area3D", "name": "body_entered", "kind": "signal", "sig": "(body: Node3D)" },
    { "class": "Area3D", "name": "body_exited", "kind": "signal", "sig": "(body: Node3D)" },
    { "class": "PhysicsBody3D", "name": "move_and_collide", "kind": "method",
      "sig": "(motion: Vector3, ...) -> KinematicCollision3D" }
  ]
}
```

### api/hierarchy

获取继承链。

```jsonc
{
  "method": "api/hierarchy",
  "params": {
    "name": "CharacterBody3D",
    "direction": "up"          // "up" (到 Object) | "down" (子类列表)
  }
}

// Response (direction="up")
{
  "result": {
    "chain": ["CharacterBody3D", "PhysicsBody3D", "CollisionObject3D", "Node3D", "Node", "Object"]
  }
}

// Response (direction="down")
{
  "result": {
    "children": ["MyCustomBody"]   // 来自 linterdb 中注册的子类
  }
}
```

### api/catalog

列出可用类的目录。替代原 `list_categories`。

```jsonc
{
  "method": "api/catalog",
  "params": {
    "domain": null              // null=全部 | "physics" | "visual" | "audio" | "ui" | "3d" | "2d"
  }
}

// Response
{
  "result": {
    "total": 1025,
    "categories": [
      { "name": "Node3D", "extends": "Node", "entries": 91 },
      { "name": "Area3D", "extends": "CollisionObject3D", "entries": 34 },
      ...
    ]
  }
}
```

域分类逻辑：基于类名前缀和继承链自动推断 (与 build_index.py 中的 tag_map 类似)。

### api/globals

查询全局函数、枚举、单例、常量。

```jsonc
{
  "method": "api/globals",
  "params": {
    "category": "singletons"  // "utility_functions" | "singletons" | "global_enums" | "global_constants"
  }
}

// Response (category="singletons")
{
  "result": [
    { "name": "Input", "class": "Input", "desc": "Singleton for handling input events." },
    { "name": "Engine", "class": "Engine", "desc": "..." },
    ...
  ]
}

// Response (category="utility_functions")
{
  "result": [
    { "name": "print", "sig": "(...) -> void", "desc": "..." },
    { "name": "lerp", "sig": "(from: Variant, to: Variant, weight: float) -> Variant" },
    ...
  ]
}
```

---

## WRITE 域 — "这样写对吗？"

AI Agent 在编写代码过程中需要确认用法、获取签名。WRITE 域支持基于代码片段的查询，不需要打开完整文档。

### code/typeof

推断表达式的类型。

```jsonc
{
  "method": "code/typeof",
  "params": {
    "expression": "get_node('Player').global_position",
    "context_class": "Node3D"   // 可选: self 的类型, 用于解析 get_node 等
  }
}

// Response
{
  "result": {
    "type": "Vector3",
    "resolved_chain": [
      { "expr": "get_node('Player')", "type": "Node" },
      { "expr": ".global_position", "type": "Vector3", "from": "Node3D" }
    ]
  }
}
```

**实现**: 构造虚拟 GDScript 文件 `extends {context_class}\nfunc _probe():\n\tvar __result = {expression}\n`，解析后提取 `__result` 的推断类型。

### code/signature

查询方法签名（直接用类名+方法名，不需要文件上下文）。

```jsonc
{
  "method": "code/signature",
  "params": {
    "class": "CharacterBody3D",
    "method": "move_and_slide"
  }
}

// Response
{
  "result": {
    "class": "CharacterBody3D",
    "method": "move_and_slide",
    "signature": "move_and_slide() -> bool",
    "params": [],
    "return_type": "bool",
    "description": "Moves the body based on velocity...",
    "defined_in": "CharacterBody3D"    // 实际定义的类 (可能是父类)
  }
}
```

**实现**: 直接查询 LinterDB，沿继承链查找方法。不需要解析器。

### code/complete

基于代码片段获取补全候选。不需要打开文档，传入代码字符串即可。

```jsonc
{
  "method": "code/complete",
  "params": {
    "snippet": "extends Node3D\n\nfunc _ready():\n\tvar body: CharacterBody3D = $Player\n\tbody.",
    "limit": 20
  }
}

// Response
{
  "result": [
    { "name": "move_and_slide", "kind": "method", "sig": "() -> bool" },
    { "name": "velocity", "kind": "property", "type": "Vector3" },
    { "name": "is_on_floor", "kind": "method", "sig": "() -> bool" },
    { "name": "floor_max_angle", "kind": "property", "type": "float" },
    ...
  ]
}
```

**实现**: 在 snippet 末尾插入 U+FFFF sentinel，构造虚拟文件，用 GDScriptParser 的 completion mode 解析，收集候选项。复用 homot-lsp 的 `lsp_completion.cpp` 逻辑但跳过 LSP 协议层。

---

## VERIFY 域 — "写完的代码有没有错？"

AI Agent 写完代码后需要验证正确性。VERIFY 域提供批量静态分析和契约检查。

### verify/lint

批量 lint 文件（支持文件和目录）。

```jsonc
{
  "method": "verify/lint",
  "params": {
    "paths": ["scripts/player.gd", "scripts/enemy.gd"],
    // 或整个目录
    // "paths": ["scripts/"]
    "severity": "error"         // "error" | "warning" | "all"
  }
}

// Response
{
  "result": {
    "summary": { "files": 5, "errors": 2, "warnings": 3 },
    "diagnostics": [
      {
        "file": "scripts/player.gd",
        "line": 15,
        "col": 8,
        "severity": "error",
        "message": "Cannot assign a value of type \"String\" as \"int\"."
      },
      {
        "file": "scripts/enemy.gd",
        "line": 42,
        "col": 1,
        "severity": "warning",
        "code": "UNUSED_VARIABLE",
        "message": "The local variable \"temp\" is declared but never used."
      }
    ]
  }
}
```

**实现**: 复用 `linter_run.cpp` 的批量分析逻辑 (pre-scan class_name → parse → analyze → collect diagnostics)。

### verify/check

即时分析代码内容（不需要文件存在于磁盘）。适合 agent 写完代码后、write_file 之前先验证。

```jsonc
{
  "method": "verify/check",
  "params": {
    "content": "extends CharacterBody3D\n\nfunc _physics_process(delta: float) -> void:\n\tvar speed: int = \"hello\"\n\tmove_and_slide()\n",
    "filename": "player.gd",       // 用于诊断显示和扩展名推断
    "severity": "all"
  }
}

// Response
{
  "result": {
    "errors": [
      {
        "line": 4,
        "col": 17,
        "message": "Cannot assign a value of type \"String\" as \"int\"."
      }
    ],
    "warnings": []
  }
}
```

**实现**: 将 content 写入临时文件或内存缓冲，用 GDScriptParser + GDScriptAnalyzer 分析，收集诊断。

### verify/contract

验证脚本是否满足架构文档中定义的接口契约。Agent 在 finalize 阶段或 coder 完成后调用。

```jsonc
{
  "method": "verify/contract",
  "params": {
    "file": "scripts/player.gd",
    "expected": {
      "extends": "CharacterBody3D",
      "methods": [
        { "name": "take_damage", "params": ["amount: float"] },
        { "name": "get_health", "returns": "float" }
      ],
      "signals": [
        { "name": "died" },
        { "name": "health_changed", "args": ["new_health: float"] }
      ],
      "properties": [
        { "name": "max_health", "type": "float" }
      ]
    }
  }
}

// Response
{
  "result": {
    "valid": false,
    "extends_match": true,
    "missing_methods": [
      { "name": "get_health", "expected_returns": "float" }
    ],
    "missing_signals": [],
    "missing_properties": [],
    "type_mismatches": [
      {
        "name": "max_health",
        "expected_type": "float",
        "actual_type": "int"
      }
    ],
    "extra_diagnostics": []
  }
}
```

**实现**: 解析目标文件的 AST，提取声明的 methods/signals/properties，与 expected 列表做 diff。

---

## Infra API 集成

HolyMolly Infra 系统的 API（HMPlayerManager, HMCameraManager 等）有两种集成方式：

### 方式 A: 引擎内置（推荐）

Infra 系统由 HolyMolly module 注册为引擎类。`homot --dump-linterdb` 时自动包含在 linterdb.json 的 `classes` 中。

**现状**: `modules/holymolly/` 已注册 HMSandbox, HMSandboxManager 等类。如果 Infra 系统也在引擎侧注册，linterdb.json 会自动包含它们。

### 方式 B: 外部 JSON 加载

如果 Infra API 定义在客户端项目的 JSON 文件中（当前方式: `Scripts/HMMainGame/Infra/api/*.json`），LSPA 可在启动时额外加载：

```bash
homot-lspa --db linterdb.json --infra-dir /path/to/infra/api/
```

LSPA 将 Infra JSON 解析后合并到 QueryEngine 的索引中，`api/search`、`api/class` 等方法统一返回。

---

## 输出格式优化

### Token Budget Aware

每个请求支持可选的 `budget` 参数，控制响应详细程度：

```jsonc
{
  "method": "api/class",
  "params": {
    "name": "Node3D",
    "budget": "compact"    // "compact" | "standard" | "verbose"
  }
}
```

| budget | 策略 | 典型 token 数 |
|--------|------|-------------|
| `compact` | 仅成员名, 用 brace expansion 分组 | ~100-300 |
| `standard` | 名字 + 签名, 省略空描述 | ~500-1500 |
| `verbose` | 完整签名 + 描述 + 文档 | ~2000+ |

**compact 格式示例**：

```
CharacterBody3D < PhysicsBody3D
P: velocity, floor_{max_angle,snap_length,stop_on_slope,...}
M: move_and_slide, is_on_{floor,wall,ceiling}, get_slide_collision
S: floor_changed
```

这种 brace expansion 格式可以在 ~50 tokens 内概览一个有 80+ 成员的类。

### 文本格式 vs JSON

默认返回结构化 JSON。但为了减少 token 消耗，LSPA 也支持纯文本格式：

```jsonc
{
  "method": "api/class",
  "params": {
    "name": "Node3D",
    "format": "text"       // "json" (default) | "text"
  }
}
```

纯文本响应更紧凑，适合直接放入 LLM 上下文。

---

## 与 HMClaw 集成

### 配置

```toml
# .hmclaw/settings.toml

[lspa]
enabled = true
command = "homot-lspa"
args = ["--db", "/path/to/linterdb.json", "--infra-dir", "/path/to/infra/api/"]
extensions = ["gd", "hm", "hmc"]
```

### HMClaw 侧实现

在 HMClaw 中新增 `hm-lspa` crate 或扩展 `hm-lsp` crate：

**方案 A: 扩展 hm-lsp**

在现有 `hm-lsp` crate 中增加 LSPA 协议支持。`LspManager` 根据服务器的 initialize 响应自动区分 LSP 和 LSPA 服务器。

**方案 B: 新建 hm-lspa crate（推荐）**

独立 crate，职责清晰：

```
crates/hm-lspa/
  src/
    lib.rs           // 公开接口
    client.rs        // LSPA JSON-RPC 客户端
    types.rs         // 请求/响应类型定义
    manager.rs       // 进程生命周期管理
```

### Tool 定义

```rust
// hm-tools/src/builtin/lspa.rs

impl Tool for LspaTool {
    fn name(&self) -> &str { "lspa" }

    fn description(&self) -> &str {
        "Engine API query & code analysis.\n\n\
         DISCOVER — query available APIs before coding:\n\
         - queryClass: Get class API summary (methods, properties, signals)\n\
         - queryClasses: Batch query multiple classes\n\
         - searchApi: Search across all engine APIs by keyword\n\
         - hierarchy: Get class inheritance chain\n\
         - catalog: List available classes by domain\n\
         - globals: Query singletons, utility functions, global enums\n\n\
         WRITE — get code assistance while coding:\n\
         - signature: Get method signature and docs by class+method name\n\
         - complete: Get completions for a code snippet\n\
         - typeof: Infer the type of an expression\n\n\
         VERIFY — validate code after writing:\n\
         - lint: Static analysis on files (type errors, syntax errors, warnings)\n\
         - check: Validate a code string without saving to disk\n\
         - contract: Verify a script implements expected interfaces"
    }

    fn input_schema(&self) -> Value {
        json!({
            "type": "object",
            "properties": {
                "operation": {
                    "type": "string",
                    "enum": [
                        "queryClass", "queryClasses", "searchApi",
                        "hierarchy", "catalog", "globals",
                        "signature", "complete", "typeof",
                        "lint", "check", "contract"
                    ]
                },
                "params": {
                    "type": "object",
                    "description": "Operation-specific parameters"
                }
            },
            "required": ["operation", "params"]
        })
    }
}
```

### Agent Prompt 更新

**lead-designer.md** 中替换工具引用：

```markdown
## 信息收集工具

使用 `lspa` 工具查询引擎 API：

- `lspa(operation="searchApi", params={query: "碰撞检测 信号", limit: 10})`
- `lspa(operation="queryClasses", params={names: ["HMPlayerManager", "HMCameraManager"], detail: "standard"})`
- `lspa(operation="globals", params={category: "singletons"})`
- `lspa(operation="hierarchy", params={name: "CharacterBody3D"})`
```

**coder.md** 中可选启用 verify：

```markdown
## 代码验证（可选）

写完文件后可调用 lspa 验证：
- `lspa(operation="lint", params={paths: ["scripts/player.gd"]})`
- `lspa(operation="check", params={content: "...", filename: "test.gd"})`
```

---

## 与 LSP 工具的关系

LSPA 和现有 LSP 工具可以共存但职责不同：

| 工具 | 场景 | 调用方 |
|------|------|--------|
| `lspa` | 引擎 API 查询 + GDScript 代码分析 | game-dev agents |
| `lsp` | 通用代码导航 (Rust/Python/etc.) | 开发者自用 |

在 game-dev 场景中，agent 只需要 `lspa` 工具。`lsp` 工具保留用于 HMClaw 自身开发等场景。

---

## 实施计划

### Phase 1 — 最小可用

**目标**: 替代 api-query，agent 能通过 LSPA 查询 API。

**homot 侧 (C++)**:
- [ ] 新建 `linter/lspa/` 目录
- [ ] `lspa_main.cpp` — 入口, 引擎 bootstrap (复用 lsp_main.cpp 逻辑)
- [ ] `lspa_server.cpp` — JSON-RPC 请求分发
- [ ] `query_engine.cpp` — 在 LinterDB 基础上实现:
  - `api/class` — 查询类信息 (复用 LinterDB::get_method_list 等)
  - `api/classes` — 批量查询
  - `api/search` — 关键词搜索 (遍历所有类做字符串匹配)
  - `api/hierarchy` — 继承链遍历 (复用 LinterDB::get_parent_class)
  - `api/catalog` — 类目录列表
  - `api/globals` — 全局查询
- [ ] `formatter.cpp` — compact/standard/verbose 输出格式化
- [ ] SCsub 构建脚本更新
- [ ] `verify/lint` — 复用 linter_run 逻辑

**HMClaw 侧 (Rust)**:
- [ ] 新建 `hm-lspa` crate (或扩展 hm-lsp)
- [ ] LSPA JSON-RPC 客户端
- [ ] `hm-tools/src/builtin/lspa.rs` — Tool 定义
- [ ] settings.toml 增加 `[lspa]` 配置段

**LHKA8-Server 侧**:
- [ ] workspace 模板 settings.toml 启用 LSPA
- [ ] agent prompt 更新 (lead-designer, game-designer 等)
- [ ] 可选: 删除 api-query provider

### Phase 2 — 代码辅助

- [ ] `code/typeof` — 表达式类型推断
- [ ] `code/signature` — 方法签名直查
- [ ] `code/complete` — 代码片段补全
- [ ] `verify/check` — 即时代码验证

### Phase 3 — 契约验证

- [ ] `verify/contract` — 接口契约校验
- [ ] 与 architecture.md 生成的契约表联动

### Phase 4 — 优化

- [ ] Infra JSON 外部加载 (`--infra-dir`)
- [ ] 搜索质量优化 (TF-IDF 或简单倒排索引)
- [ ] 响应缓存 (类信息不变，缓存格式化结果)

---

## 附录: linterdb.json 数据结构

```jsonc
{
  "classes": {
    "Node3D": {
      "parent": "Node",
      "is_abstract": false,
      "methods": [                          // MethodInfo 数组
        {
          "name": "set_transform",
          "args": [{ "name": "local", "type": 18 }],
          "return_val": { "type": 0 },
          "flags": 1,
          "is_static": false,
          "is_vararg": false,
          "instance_class": "Node3D",
          "default_arg_count": 0
        }
      ],
      "properties": { ... },               // PropertyInfo dict
      "signals": { ... },                  // SignalInfo dict
      "enums": { ... },
      "constants": { ... },
      "doc": {                              // 从 XML docs 加载
        "brief_description": "...",
        "description": "...",
        "methods": [
          { "name": "add_gizmo", "arguments": [...], "return_type": "void", "description": "..." }
        ],
        "properties": [
          { "name": "basis", "type": "Basis", "getter": "get_basis", "setter": "set_basis", "description": "..." }
        ],
        "signals": [...],
        "constants": [...],
        "tutorials": [...]
      }
    },
    // ... ~1025 classes
  },
  "builtin_types": {
    "Vector3": {
      "constructors": [...],
      "methods": [...],
      "members": [...],
      "operators": [...],
      "constants": [...],
      "doc": { ... }
    },
    // ... ~39 builtin types
  },
  "singletons": ["Input", "Engine", "OS", ...],        // 39 个单例名
  "utility_functions": { ... },                         // 114 个全局函数
  "global_enums": { ... },                              // 全局枚举
  "global_constants": { ... },                          // 全局常量
  "doc_classes": { "@GlobalScope": { ... }, ... }       // 特殊文档类
}
```
