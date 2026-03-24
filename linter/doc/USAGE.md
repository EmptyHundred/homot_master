# homot Usage Guide

## Overview

`homot` is a unified GDScript toolchain providing three capabilities in a single binary:

| Command | Description |
|---------|-------------|
| `homot lint` | Static analysis for `.gd`, `.hm`, `.hmc` scripts |
| `homot lsp` | Language Server Protocol server (editor integration) |
| `homot lspa` | Language Server Protocol for Agents (AI agent integration) |

The binary embeds a compressed copy of `linterdb.json` (~2 MB), so **no external database file is needed**. Use `--db` to override with a custom database.

## Quick Start

```bash
# Lint scripts
homot lint scripts/
homot lint player.gd enemy.gd scripts/ai/

# Start LSP server (for editors like VS Code)
homot lsp

# Start LSPA server (for AI agents like HMClaw)
homot lspa

# Override embedded database
homot --db custom_linterdb.json lint scripts/
```

## Command-Line Reference

```
Usage: homot <command> [options] [args...]

Commands:
  lint <path> [<path>...]   Lint .gd/.hm/.hmc scripts
  lsp                       Start GDScript Language Server (stdio)
  lspa                      Start Language Server for Agents (stdio)

Global Options:
  --db <path>    Override embedded linterdb with external JSON file
  --help, -h     Show this help message
```

### Exit Codes

| Code | Meaning |
|------|---------|
| `0`  | Success (lint: no errors; lsp/lspa: clean shutdown) |
| `1`  | Errors found, or invalid arguments |

---

## Lint

Runs the same `GDScriptParser` and `GDScriptAnalyzer` as the Godot editor. Checks include:

- **Syntax errors** — missing colons, unclosed parentheses, invalid tokens
- **Type errors** — assigning `String` to `int`, wrong return types, invalid argument types
- **Unknown identifiers** — calling methods that don't exist on the class
- **Property type mismatches** — assigning wrong types to typed properties
- **Signal usage** — connecting to signals, emitting with correct arguments
- **Enum validation** — using enum values correctly
- **Cross-file class resolution** — `class_name` declarations are pre-scanned so scripts can reference each other
- **Warnings** — unused variables, shadowed variables, etc. (requires `DEBUG_ENABLED` builds)

### Output Format

```
  OK: path/to/script.gd
  SKIP (empty): path/to/empty.gd
  ERROR: path/to/script.gd:3:14: Cannot assign a value of type "String" as "int".
  WARN:  path/to/script.gd:10: [UNUSED_VARIABLE] The local variable "x" is declared but never used.

=== Lint Summary ===
Scripts:  42
Errors:   3
Warnings: 7
```

Error format: `ERROR: <file>:<line>:<column>: <message>` — compatible with CI annotation parsers and editor problem matchers.

### CI Integration

```bash
homot lint project/scripts/
if [ $? -ne 0 ]; then
    echo "Lint errors found!"
    exit 1
fi
```

```yaml
# GitHub Actions
- name: Lint GDScript
  run: ./homot lint project/scripts/
```

---

## LSP

`homot lsp` is a Language Server providing real-time GDScript diagnostics, completions, go-to-definition, hover, and signature help. Communicates over stdin/stdout JSON-RPC.

### Features

- **Diagnostics** — Real-time parse errors, type errors, and warnings
- **Completion** — Context-aware autocompletion for identifiers, methods, properties, signals, constants, annotations, keywords
- **Go-to-Definition** — Jump to variable declarations, function definitions, signal declarations, class files (Ctrl+click / F12)
- **Hover** — Type information and documentation on mouse-over
- **Signature Help** — Parameter hints when calling functions

### VS Code Extension

A VS Code extension is included at `linter/vscode/`. Install:

```bash
cd linter/vscode && npm install
npm install -g @vscode/vsce
vsce package
code --install-extension homot-lsp-*.vsix
```

Extension settings:

| Setting | Description |
|---------|-------------|
| `homotLsp.serverPath` | Path to `homot` binary. If empty, looks next to the extension or in `bin/linter/`. |
| `homotLsp.dbPath` | *(Deprecated)* No longer needed — DB is embedded. Still works as override. |

---

## LSPA — Language Server Protocol for Agents

`homot lspa` is designed for AI agent workflows. It provides three capability domains:

### DISCOVER — API 查询

| Method | Description |
|--------|-------------|
| `api/class` | 查询单个类的信息（properties, methods, signals） |
| `api/classes` | 批量查询多个类 |
| `api/search` | 跨所有类的关键词搜索 |
| `api/hierarchy` | 获取类继承链（向上或向下） |
| `api/catalog` | 按域（3d, 2d, ui, physics, audio）列出类目录 |
| `api/globals` | 查询全局函数、单例、枚举、常量 |

### WRITE — 代码辅助

| Method | Description |
|--------|-------------|
| `code/typeof` | 推断表达式类型 |
| `code/signature` | 查询方法签名 |
| `code/complete` | 代码片段补全 |

### VERIFY — 代码验证

| Method | Description |
|--------|-------------|
| `verify/lint` | 批量 lint 文件 |
| `verify/check` | 验证代码字符串（不需要文件存在于磁盘） |
| `verify/contract` | 验证脚本是否满足接口契约 |

### Protocol

JSON-RPC 2.0 over stdin/stdout, Content-Length 头分帧。无状态，无需文档生命周期管理。

```jsonc
// Initialize
{"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}}

// Query a class
{"jsonrpc": "2.0", "id": 2, "method": "api/class", "params": {"name": "CharacterBody3D", "detail": "standard"}}

// Search APIs
{"jsonrpc": "2.0", "id": 3, "method": "api/search", "params": {"query": "collision body", "limit": 10}}

// Verify code
{"jsonrpc": "2.0", "id": 4, "method": "verify/check", "params": {"content": "extends Node3D\n...", "filename": "test.gd"}}

// Shutdown
{"jsonrpc": "2.0", "id": 99, "method": "shutdown", "params": {}}
```

### Detail Levels

`api/class` 和 `api/classes` 支持 `detail` 参数控制输出详细程度：

| Level | 内容 | 典型 token 数 |
|-------|------|-------------|
| `names_only` | 仅成员名列表 | ~200 |
| `standard` | 名字 + 签名，省略空描述 | ~800 |
| `full` | 完整签名 + 描述 + 文档 | ~2000+ |

### HMClaw 集成

```toml
# .hmclaw/settings.toml
[lspa]
enabled = true
command = "homot"
args = ["lspa"]
extensions = ["gd", "hm", "hmc"]
```

完整协议规范参见 [LSPA_DESIGN.md](../../../docs/LSPA_DESIGN.md)。

---

## Building

### Prerequisites

Root `SConstruct` must include:

```python
if env.get("linter", False):
    SConscript("linter/SCsub")
```

### Updating the Embedded linterdb

当引擎版本变更或自定义模块有变动时，需要重新生成嵌入数据：

```bash
# 1. 用编辑器构建生成 linterdb.json
homot --dump-linterdb

# 2. 复制到 linter 目录
cp linterdb.json linter/linterdb.json

# 3. 重新构建（SCsub 自动检测并重新生成嵌入数据）
scons platform=windows target=template_debug linter=yes
```

也可以手动生成嵌入数据：

```bash
python linter/embed_linterdb.py linter/linterdb.json linter/linterdb_embedded.gen.cpp
```

### Build Commands

```bash
# Debug build (~18 MB, with warnings support)
scons platform=windows target=template_debug linter=yes

# Release build (~16 MB, smallest)
scons platform=windows target=template_release linter=yes
```

输出: `bin/linter/homot.<platform>.<target>.<arch>.exe`

`target=template_debug` 推荐用于分发：剥离 `TOOLS_ENABLED` 代码（最大尺寸优化）同时保留 `DEBUG_ENABLED` 以支持 GDScript 警告。

### Architecture

```
homot (single binary, ~18 MB)
├── Embedded linterdb (~2 MB compressed, ~18 MB decompressed)
├── Engine bootstrap (minimal OS, core types, GDScript module)
├── Subcommand: lint → linter_run.cpp
├── Subcommand: lsp  → lsp/lsp_server.cpp
└── Subcommand: lspa → lspa/lspa_server.cpp + query_engine.cpp
```

## Known Limitations

- **No `preload()`/`load()` resolution** — Resources cannot be loaded at lint time. Scripts using `preload()` for type references may produce false positives.
- **No autoloads** — Project autoload singletons are not registered. References to autoloads will be unresolved.
- **No GDExtension classes** — Only classes present in the engine at `--dump-linterdb` time are available.
