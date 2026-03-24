# homot Linter Design Document

## Overview

`homot` is a standalone linting and language server toolchain that validates `.gd`, `.hm`, `.hmc`, `.tscn`, `.tres`, and `.gdshader` files **without running the full game engine**. For GDScript, it reuses the engine's own `GDScriptParser` and `GDScriptAnalyzer`, but replaces engine subsystems (ClassDB, Engine, ScriptServer, etc.) with lightweight stubs that source their data from an embedded JSON dump (`linterdb.json`). For scene/resource and shader files, it uses custom lightweight parsers.

The goal is CI/editor-independent static analysis: given a project directory and a database dump, the linter can check every file for errors and warnings — the same checks the editor would perform for scripts, plus structural validation for scenes and shaders — without needing a display server, renderer, or any runtime resources.

## Architecture

```
 ┌───────────────────────────────────────────────────────────┐
 │                      homot binary                          │
 ├──────────────┬────────────────────────────────────────────┤
 │ homot_main   │  CLI entry point, engine bootstrap,        │
 │              │  subcommand dispatch (lint / serve)         │
 ├──────────────┼────────────────────────────────────────────┤
 │ workspace    │  Shared utilities: file collection,         │
 │              │  class_name scanning, global class registry │
 ├──────────────┼────────────────────────────────────────────┤
 │ linter_run   │  CLI lint: script + resource + shader      │
 │              │  analysis, text output                      │
 ├──────────────┼────────────────────────────────────────────┤
 │ resource_lint│  .tscn/.tres parser + validator             │
 ├──────────────┼────────────────────────────────────────────┤
 │ shader_lint  │  .gdshader structural checker               │
 ├──────────────┼────────────────────────────────────────────┤
 │ lsp/         │  Unified server (LSP + LSPA dispatch)      │
 ├──────────────┼────────────────────────────────────────────┤
 │ lspa/        │  LSPA handlers (query, verify, format)     │
 ├──────────────┼────────────────────────────────────────────┤
 │ stubs/       │  Link-time replacements for engine          │
 │              │  subsystems (see below)                     │
 ├──────────────┼────────────────────────────────────────────┤
 │ linterdb     │  JSON loader + query interface for          │
 │              │  class/method/property/signal/enum metadata │
 └──────────────┴────────────────────────────────────────────┘
         │                        │
         │ links against          │ embeds (compressed)
         ▼                        ▼
 ┌───────────────┐       ┌─────────────────┐
 │ Engine libs   │       │ linterdb.json   │
 │ (core,        │       │ (class/type     │
 │  modules)     │       │  metadata dump) │
 └───────────────┘       └─────────────────┘
```

### Key Principle: Reuse Real Code, Stub the Environment

The GDScript parser and analyzer are complex (tokenizer, AST, type inference, inheritance resolution) and tightly coupled to engine types (`Variant`, `StringName`, `MethodInfo`, etc.). Reimplementing them would be impractical and would immediately drift out of sync.

Instead, the linter:
1. **Links against** the engine static libraries (core, modules) to get the real parser, analyzer, and all supporting types.
2. **Recompiles specific engine source files** with `HOMOT_LINTER` defined, replacing heavy subsystem logic with lightweight stubs that delegate to `LinterDB`.

This means the parser/analyzer code is byte-for-byte identical to what runs in the editor. Only the environment it queries is different.

## Components

### `workspace.cpp/h` — Shared Utilities

Common functions used by lint, LSP, and LSPA:

- `collect_scripts()` — recursively collect `.gd/.hm/.hmc` files
- `collect_all_files()` — collect all lintable files (scripts + resources + shaders)
- `is_script_file()`, `is_resource_file()`, `is_shader_file()`, `is_lintable_file()` — file type checks
- `extract_class_name()`, `extract_extends()` — lightweight text extraction without full parsing
- `resolve_native_base()` — walk extends chain to find native base class
- `scan_and_register_classes()` — pre-scan scripts and register global classes
- `register_classes()` — register already-scanned classes with ScriptServerStub

### `resource_lint.cpp/h` — Scene/Resource Linter

Custom parser for Godot's text resource format (`.tscn`, `.tres`). Checks:

- **Header** — valid `[gd_scene]` or `[gd_resource]` header
- **Section structure** — `[ext_resource]`, `[sub_resource]`, `[node]`, `[resource]`, `[connection]`
- **Type validation** — node/resource `type="..."` checked against linterdb
- **Property validation** — property names checked against the type's known properties (walking inheritance)
- **Duplicate node names** — siblings with same name under one parent
- **Reference integrity** — `ExtResource("id")` and `SubResource("id")` must refer to declared resources

### `shader_lint.cpp/h` — Shader Linter (Phase 1)

Lightweight structural checker for `.gdshader` files. Phase 1 does not use the engine's 12K-line `ShaderLanguage` parser (which requires `RenderingServer`). Instead, it checks:

- **shader_type** — must be first non-comment statement; valid types: `spatial`, `canvas_item`, `particles`, `sky`, `fog`
- **Brace/parenthesis matching** — detects unclosed `{` `}` `(` `)`
- **Comment handling** — properly strips `//` and `/* */` comments, detects unterminated block comments
- **Uniform declarations** — type checking against known GLSL/Godot types, duplicate detection
- **Function detection** — identifies function definitions at top level

### `linterdb.json` — The Type Database

A JSON file embedded in the binary (compressed with zlib). Dumped from a running engine instance via `--dump-linterdb`. Top-level structure:

| Key                  | Type   | Description |
|----------------------|--------|-------------|
| `classes`            | dict   | Full ClassDB dump — every registered native class with methods, properties, signals, enums, and constants. ~1025 classes. |
| `builtin_types`      | dict   | Variant built-in types (Vector2, AABB, etc.) with constructors, methods, members, operators, and constants. ~39 types. |
| `singletons`         | list   | Engine singleton names (Input, OS, Engine, etc.). ~39 entries. |
| `utility_functions`  | list   | Global utility functions (print, lerp, etc.). ~114 entries. |
| `global_enums`       | dict   | Global-scope enums (Error, PropertyHint, etc.). ~22 enums. |
| `global_constants`   | dict   | Global-scope constants not belonging to any enum. |

### `stubs/` — Link-Time Overrides

| Stub | Overrides | Purpose |
|------|-----------|---------|
| `linterdb.h/.cpp` | — | JSON loader + query singleton |
| `classdb_stub.h/.cpp` | `ClassDB` | Delegates class/method/property queries to LinterDB; provides `StubMethodBind` |
| `script_server_stub.h/.cpp` | `ScriptServer` | Global class registry for `class_name` declarations |

Engine source files recompiled with `HOMOT_LINTER`:

| File | Override |
|------|----------|
| `core/object/class_db.cpp` | ClassDB query methods → LinterDB |
| `core/config/engine.cpp` | `Engine::has_singleton()` → LinterDB |
| `core/config/project_settings.cpp` | `has_autoload`/`get_autoload` → no-op |
| `core/io/resource_loader.cpp` | `load`/`exists`/etc. → simplified stubs |
| `core/object/script_language.cpp` | ScriptServer globals → ScriptServerStub |

## Execution Flow

```
main()
 │
 ├─ 1. Parse CLI arguments (command, --db, --help)
 │
 ├─ 2. Minimal engine bootstrap
 │     ├─ OS_Homot (minimal OS implementation)
 │     ├─ register_core_types + platform file access
 │     ├─ TextServerDummy (GDScript tokenizer dependency)
 │     ├─ GDScript module + GDScriptCache
 │     └─ HolyMolly module
 │
 ├─ 3. Load embedded linterdb (or --db override)
 │
 ├─ 4. Dispatch subcommand
 │     ├─ "lint" → run_lint()
 │     │     ├─ collect_all_files (scripts + resources + shaders)
 │     │     ├─ scan_and_register_classes (for scripts)
 │     │     ├─ GDScript lint (parse + analyze each script)
 │     │     ├─ Resource lint (validate each .tscn/.tres)
 │     │     ├─ Shader lint (check each .gdshader)
 │     │     └─ Print summary
 │     │
 │     └─ "serve" → unified server message loop
 │           ├─ initialize → load DB, scan workspace
 │           ├─ textDocument/* → LSP features
 │           ├─ api/*, verify/* → LSPA features
 │           └─ shutdown/exit
 │
 └─ 5. Engine teardown (reverse order of init)
```

## Build System

The linter is built as a second `Program` target within the engine's SCons build, not as a standalone project.

### Build Command

```
scons platform=windows target=template_debug linter=yes
```

Produces `bin/linter/homot.<platform>.<target>.<arch>.exe`.

### How It Works

The `SCsub` creates a cloned environment with `HOMOT_LINTER` defined. Engine source files listed in `engine_override_sources` are recompiled into the `override/` directory with this define active, providing stub implementations that the linker uses instead of the library versions. `/OPT:REF` strips unreferenced code for minimal binary size.

## Limitations and Future Work

### Current Limitations

- **No resource loading**: `preload()` and `load()` calls cannot resolve actual resources. The analyzer may report false positives for preloaded scripts or resources.
- **No autoloads**: Project autoloads are not available. Scripts referencing autoload singletons will see errors.
- **No GDExtension classes**: Only classes registered via the engine's `ClassDB` at dump time are available.
- **Resource lint property false positives**: Dynamically-added or mixin properties may trigger false "property not found" warnings.
- **Shader lint: syntax only**: Phase 1 only checks structure. Full semantic analysis (built-in variables, function signatures) would require RenderingServer integration or a shader-specific linterdb dump.

### Potential Improvements

- **Autoload support**: Parse `project.godot` to extract autoload entries and register them.
- **Incremental linting**: Cache parse results and only re-analyze changed files.
- **Cross-script resolution**: Use `GDScriptCache` more fully to resolve `preload()` dependencies.
- **Output formats**: JSON/SARIF output for CI integration.
- **Shader Phase 2**: Dump shader built-in definitions into linterdb for full semantic checking.
- **Resource deeper checks**: Signal connection validation, NodePath validity.

## File Inventory

```
linter/
├── doc/
│   ├── LINTER_DESIGN.md           ← this file
│   ├── LSP_DESIGN.md              ← LSP/LSPA design details
│   └── USAGE.md                   ← user-facing usage guide
├── stubs/
│   ├── linterdb.h/.cpp            ← JSON loader + query singleton
│   ├── classdb_stub.h/.cpp        ← ClassDB + StubMethodBind overrides
│   └── script_server_stub.h/.cpp  ← ScriptServer + global class registry
├── lsp/
│   ├── lsp_server.h/.cpp          ← Unified server (LSP + LSPA dispatch)
│   ├── lsp_transport.h/.cpp       ← JSON-RPC over stdin/stdout
│   ├── lsp_protocol.h             ← LSP types and JSON-RPC helpers
│   ├── lsp_completion.h/.cpp      ← textDocument/completion handler
│   ├── lsp_definition.h/.cpp      ← textDocument/definition handler
│   ├── lsp_hover.h/.cpp           ← textDocument/hover handler
│   ├── lsp_signature_help.h/.cpp  ← textDocument/signatureHelp handler
│   └── lsp_utils.h/.cpp           ← Symbol resolution utilities
├── lspa/
│   ├── query_engine.h/.cpp        ← LSPA DISCOVER handlers (api/*)
│   ├── verifier.h/.cpp            ← LSPA VERIFY handlers (verify/*)
│   └── formatter.h/.cpp           ← LSPA output formatting
├── vscode/
│   ├── package.json               ← VS Code extension manifest
│   ├── extension.js               ← Spawns 'homot serve' via vscode-languageclient
│   ├── language-configuration.json
│   └── syntaxes/gdscript.tmLanguage.json
├── homot_main.cpp                 ← CLI entry point + engine bootstrap
├── workspace.h/.cpp               ← Shared utilities (file collection, class scanning)
├── linter_run.h/.cpp              ← CLI lint logic (all file types)
├── resource_lint.h/.cpp           ← .tscn/.tres linter
├── shader_lint.h/.cpp             ← .gdshader linter
├── linterdb.json                  ← Pre-generated type database dump
├── linterdb_embedded.gen.cpp      ← Auto-generated embedded linterdb data
├── embed_linterdb.py              ← Script to compress + embed linterdb.json
├── SConstruct                     ← Build manifest / documentation
└── SCsub                          ← SCons build script
```
