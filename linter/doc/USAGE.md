# homot Usage Guide

## Overview

`homot` is a unified toolchain for linting and language server support, packaged as a single binary:

| Command | Description |
|---------|-------------|
| `homot lint` | Static analysis for `.gd`, `.hm`, `.hmc`, `.tscn`, `.tres`, `.gdshader` files |
| `homot serve` | Unified Language Server (LSP + LSPA) over stdin/stdout |

The binary embeds a compressed copy of `linterdb.json` (~2 MB), so **no external database file is needed**. Use `--db` to override with a custom database.

## Quick Start

```bash
# Lint scripts, scenes, resources, and shaders
homot lint scripts/
homot lint player.gd enemy.gd scripts/ai/
homot lint main.tscn assets/theme.tres shaders/

# Start unified server (for editors and AI agents)
homot serve

# Override embedded database
homot --db custom_linterdb.json lint scripts/

# Lint with base project context (live scan)
homot lint --project /path/to/base-project dynamic_scripts/

# Export base project class info, then lint with it
homot dump-project /path/to/base-project -o base_classdb.json
homot lint --project-db base_classdb.json dynamic_scripts/
```

## Command-Line Reference

```
Usage: homot <command> [options] [args...]

Commands:
  lint <path> [<path>...]               Lint .gd/.hm/.hmc/.tscn/.tres/.gdshader files
  serve                                  Start unified Language Server (LSP + LSPA, stdio)
  dump-project <dir> [-o output.json]    Export project class info to JSON

Global Options:
  --db <path>           Override embedded linterdb with external JSON file
  --project <path>      Load project context (class_names, autoloads) from a
                        Godot project directory or project.godot file
  --project-db <path>   Load project class info from a previously exported JSON
                        (created by dump-project)
  --help, -h            Show this help message
```

### Exit Codes

| Code | Meaning |
|------|---------|
| `0`  | Success (lint: no errors; serve: clean shutdown) |
| `1`  | Errors found, or invalid arguments |

---

## Lint

### GDScript (.gd, .hm, .hmc)

Runs the same `GDScriptParser` and `GDScriptAnalyzer` as the Godot editor. Checks include:

- **Syntax errors** — missing colons, unclosed parentheses, invalid tokens
- **Type errors** — assigning `String` to `int`, wrong return types, invalid argument types
- **Unknown identifiers** — calling methods that don't exist on the class
- **Property type mismatches** — assigning wrong types to typed properties
- **Signal usage** — connecting to signals, emitting with correct arguments
- **Enum validation** — using enum values correctly
- **Cross-file class resolution** — `class_name` declarations are pre-scanned so scripts can reference each other
- **Warnings** — unused variables, shadowed variables, etc. (requires `DEBUG_ENABLED` builds)

### Scene/Resource Files (.tscn, .tres)

Parses the Godot text resource format and validates:

- **Header format** — valid `[gd_scene]` or `[gd_resource]` header
- **Node/resource types** — checks that `type="..."` exists in linterdb
- **Property names** — validates property names against the declared type's definition
- **Duplicate node names** — detects sibling nodes with the same name under one parent
- **Resource references** — verifies `ExtResource("id")` and `SubResource("id")` refer to declared resources
- **Section structure** — unknown or malformed section tags

### Shader Files (.gdshader)

Lightweight structural and syntax checks:

- **shader_type declaration** — must be first statement; must be one of `spatial`, `canvas_item`, `particles`, `sky`, `fog`
- **Brace/parenthesis matching** — unclosed `{` `}` `(` `)` detection
- **Uniform validation** — type checking against known GLSL/Godot types, duplicate uniform detection
- **Block comment termination** — unterminated `/* ... */` detection

### Output Format

```
Found 15 file(s): 13 scripts, 1 resources, 1 shaders.
  OK: path/to/script.gd
  SKIP (empty): path/to/empty.gd
  ERROR: path/to/script.gd:3:14: Cannot assign a value of type "String" as "int".
  WARN:  path/to/script.gd:10: [UNUSED_VARIABLE] The local variable "x" is declared but never used.
  ERROR: path/to/scene.tscn:5: Unknown node type "FooBar".
  WARN:  path/to/scene.tscn:12: Property "nonexistent" not found on type "Node3D".
  OK: path/to/shader.gdshader
  ERROR: path/to/bad.gdshader:4: Duplicate uniform "color".

=== Lint Summary ===
Files:    15 (scripts: 13, resources: 1, shaders: 1)
Errors:   3
Warnings: 2
```

Error format: `ERROR: <file>:<line>[:<column>]: <message>` — compatible with CI annotation parsers and editor problem matchers.

### CI Integration

```bash
homot lint project/
if [ $? -ne 0 ]; then
    echo "Lint errors found!"
    exit 1
fi
```

```yaml
# GitHub Actions
- name: Lint Project
  run: ./homot lint project/
```

---

## Serve (Unified LSP + LSPA)

`homot serve` starts a single JSON-RPC server over stdin/stdout that handles both:
- **Standard LSP methods** (`textDocument/*`) — for editors like VS Code
- **LSPA methods** (`api/*`, `verify/*`, `code/*`) — for AI agents like HMClaw

Clients only call the methods they care about. A VS Code extension sends `textDocument/*`, while an AI agent sends `api/*` and `verify/*`. Both can connect to the same server.

### LSP Features (for Editors)

- **Diagnostics** — Real-time parse errors, type errors, and warnings for `.gd`, `.hm`, `.hmc`, `.tscn`, `.tres`, `.gdshader`
- **Completion** — Context-aware autocompletion for identifiers, methods, properties, signals, constants, annotations, keywords
- **Go-to-Definition** — Jump to variable declarations, function definitions, signal declarations, class files (Ctrl+click / F12)
- **Hover** — Type information and documentation on mouse-over
- **Signature Help** — Parameter hints when calling functions

### LSPA Features (for AI Agents)

#### DISCOVER — API 查询

| Method | Description |
|--------|-------------|
| `api/class` | 查询单个类的信息（properties, methods, signals） |
| `api/classes` | 批量查询多个类 |
| `api/search` | 跨所有类的关键词搜索 |
| `api/hierarchy` | 获取类继承链（向上或向下） |
| `api/catalog` | 按域（3d, 2d, ui, physics, audio）列出类目录 |
| `api/globals` | 查询全局函数、单例、枚举、常量 |

#### WRITE — 代码辅助

| Method | Description |
|--------|-------------|
| `code/typeof` | 推断表达式类型 |
| `code/signature` | 查询方法签名 |
| `code/complete` | 代码片段补全 |

#### VERIFY — 代码验证

| Method | Description |
|--------|-------------|
| `verify/lint` | 批量 lint 文件 |
| `verify/check` | 验证代码字符串（不需要文件存在于磁盘） |
| `verify/contract` | 验证脚本是否满足接口契约 |

### LSPA Protocol

JSON-RPC 2.0 over stdin/stdout, Content-Length 头分帧。无状态，无需文档生命周期管理。

```jsonc
// Initialize (returns both LSP capabilities and LSPA capabilities)
{"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}}

// Query a class (LSPA)
{"jsonrpc": "2.0", "id": 2, "method": "api/class", "params": {"name": "CharacterBody3D", "detail": "standard"}}

// Search APIs (LSPA)
{"jsonrpc": "2.0", "id": 3, "method": "api/search", "params": {"query": "collision body", "limit": 10}}

// Verify code (LSPA)
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

### VS Code Extension

A VS Code extension is included at `linter/vscode/`. Install:

```bash
cd linter/vscode && npm install
npm install -g @vscode/vsce
vsce package
code --install-extension homot-lsp-*.vsix
```

The extension launches `homot serve` and supports `.gd`, `.hm`, `.hmc`, `.tscn`, `.tres`, `.gdshader` files.

Extension settings:

| Setting | Description |
|---------|-------------|
| `homotLsp.serverPath` | Path to `homot` executable. If empty, auto-discovers in extension dir or `bin/linter/`. |
| `homotLsp.dbPath` | (Optional) Path to external linterdb.json override. DB is embedded by default. |

### HMClaw 集成

```toml
# .hmclaw/settings.toml
[lspa]
enabled = true
command = "homot"
args = ["serve"]
extensions = ["gd", "hm", "hmc", "tscn", "tres", "gdshader"]
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
├── Subcommand: lint → linter_run.cpp + resource_lint.cpp + shader_lint.cpp
├── Subcommand: serve → lsp/lsp_server.cpp (unified LSP + LSPA dispatch)
│   ├── LSP handlers: completion, definition, hover, signature_help
│   └── LSPA handlers: lspa/query_engine.cpp, lspa/verifier.cpp, lspa/formatter.cpp
└── Subcommand: dump-project → workspace.cpp (project classdb export)
```

## Project ClassDB Export / Import

When linting dynamic scripts that run on top of a base Godot project, the linter needs knowledge of the base project's classes. Two approaches are available:

### Approach 1: Live scan with `--project`

```bash
homot lint --project /path/to/base-project dynamic_scripts/
```

This scans the base project directory on every invocation, extracting `class_name` declarations and autoload singletons.

### Approach 2: Cached export with `dump-project` + `--project-db`

```bash
# One-time: export base project class info
homot dump-project /path/to/base-project -o base_classdb.json

# Every time: lint with the exported info
homot lint --project-db base_classdb.json dynamic_scripts/

# Also works with serve:
homot serve --project-db base_classdb.json
```

This is useful when:
- The base project is large and re-scanning is slow
- You want reproducible CI builds with a checked-in classdb
- The base project isn't always available at the same path

### Path remapping

If the base project moved since the export, combine `--project` with `--project-db` to remap paths:

```bash
homot lint --project-db base_classdb.json --project /new/path/to/base-project dynamic_scripts/
```

### Exported JSON format

```json
{
  "format_version": 1,
  "project_root": "/original/path/to/project",
  "classes": {
    "MyClass": {
      "path": "/original/path/to/project/scripts/my_class.gd",
      "extends": "Node3D",
      "native_base": "Node3D"
    }
  },
  "autoloads": [
    { "name": "GameManager", "path": "res://autoloads/game_manager.gd", "is_singleton": true }
  ]
}
```

---

## Known Limitations

- **No `preload()`/`load()` resolution** — Resources cannot be loaded at lint time. Scripts using `preload()` for type references may produce false positives.
- **Autoloads require `--project` or `--project-db`** — Without these flags, autoload singletons are not registered and references will be unresolved.
- **No GDExtension classes** — Only classes present in the engine at `--dump-linterdb` time are available.
- **Resource lint: property false positives** — Some dynamically-added properties (e.g. from mixins or runtime-registered classes) may trigger false "property not found" warnings in `.tscn`/`.tres` files.
- **Shader lint: syntax only** — The `.gdshader` linter performs structural checks only (Phase 1). Full semantic analysis (built-in variable types, function signatures) requires RenderingServer integration.
