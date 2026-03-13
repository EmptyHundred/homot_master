# homot-linter Design Document

## Overview

`homot-linter` is a standalone GDScript linter that validates `.gd`, `.hm`, and `.hmc` script files **without running the full game engine**. It reuses the engine's own `GDScriptParser` and `GDScriptAnalyzer` for parsing and semantic analysis, but replaces engine subsystems (ClassDB, Engine, ScriptServer, etc.) with lightweight stubs that source their data from a JSON dump file (`linterdb.json`).

The goal is CI/editor-independent static analysis: given a project directory and a database dump, the linter can check every script for parse errors, type errors, and warnings — the same checks the editor would perform — without needing a display server, renderer, or any runtime resources.

## Architecture

```
 ┌─────────────────────────────────────────────────────┐
 │                   homot-linter binary                │
 ├──────────────┬──────────────────────────────────────┤
 │ main_linter  │  CLI entry point, engine bootstrap,  │
 │              │  teardown                            │
 ├──────────────┼──────────────────────────────────────┤
 │ linter_run   │  Script collection, pre-scan,        │
 │              │  parse/analyze loop, result output    │
 ├──────────────┼──────────────────────────────────────┤
 │ stubs/       │  Link-time replacements for engine    │
 │              │  subsystems (see below)               │
 ├──────────────┼──────────────────────────────────────┤
 │ linterdb     │  JSON loader + query interface for    │
 │              │  class/method/property/signal/enum    │
 │              │  metadata                            │
 └──────────────┴──────────────────────────────────────┘
         │                        │
         │ links against          │ reads at runtime
         ▼                        ▼
 ┌───────────────┐       ┌─────────────────┐
 │ Engine libs   │       │ linterdb.json   │
 │ (core, scene, │       │ (class/type     │
 │  servers,     │       │  metadata dump) │
 │  modules)     │       └─────────────────┘
 └───────────────┘
```

### Key Principle: Reuse Real Code, Stub the Environment

The GDScript parser and analyzer are complex (tokenizer, AST, type inference, inheritance resolution) and tightly coupled to engine types (`Variant`, `StringName`, `MethodInfo`, etc.). Reimplementing them would be impractical and would immediately drift out of sync.

Instead, the linter:
1. **Links against** the full engine static libraries (core, scene, servers, modules) to get the real parser, analyzer, and all supporting types.
2. **Overrides specific symbols at link time** by compiling stub `.cpp` files whose `.obj` files are listed before the engine libraries in the link command. On MSVC, first-definition-wins; on GCC/MinGW, `--allow-multiple-definition` achieves the same.

This means the parser/analyzer code is byte-for-byte identical to what runs in the editor. Only the environment it queries is different.

## Components

### `linterdb.json` — The Type Database

A JSON file dumped from a running engine instance (via `--dump-linterdb` CLI flag). It captures a snapshot of the engine's type system at a point in time. Top-level structure:

| Key                  | Type   | Description |
|----------------------|--------|-------------|
| `classes`            | dict   | Full ClassDB dump — every registered native class with methods, properties, signals, enums, and constants. ~1025 classes. |
| `builtin_types`      | dict   | Variant built-in types (Vector2, AABB, etc.) with constructors, methods, members, operators, and constants. ~39 types. |
| `singletons`         | list   | Engine singleton names (Input, OS, Engine, etc.). ~39 entries. |
| `utility_functions`  | list   | Global utility functions (print, lerp, etc.). ~114 entries. |
| `global_enums`       | dict   | Global-scope enums (Error, PropertyHint, etc.). ~22 enums. |
| `global_constants`   | dict   | Global-scope constants not belonging to any enum. |

Each class entry contains:
- `parent` — parent class name (for inheritance chain walking)
- `is_abstract` — whether the class can be instantiated
- `methods[]` — array of method descriptors with full `MethodInfo` (return type, arguments, default arg count, flags, vararg/static status)
- `properties[]` — array of property descriptors with type info, getter, setter
- `signals[]` — array of signal descriptors
- `enums{}` — map of enum name to {constant_name: value}
- `constants{}` — standalone integer constants

The dump is generated once per engine version/configuration and can be committed or distributed alongside the linter.

### `stubs/linterdb.h/.cpp` — LinterDB Singleton

The `LinterDB` class loads and indexes `linterdb.json` at startup. It provides a query interface that mirrors `ClassDB`'s API:

- **Class queries**: `class_exists`, `get_parent_class`, `is_parent_class`, `is_abstract`
- **Method queries**: `has_method`, `get_method_data`, `get_method_info`, `get_method_list` — all walk the inheritance chain unless `p_no_inheritance` is set
- **Property queries**: `has_property`, `get_property_data`, `get_property_list`
- **Signal queries**: `has_signal`, `get_signal_info`, `get_signal_list`
- **Enum queries**: `has_enum`, `get_enum_list`, `get_enum_constants`
- **Integer constant queries**: `has_integer_constant`, `get_integer_constant`, `get_integer_constant_enum`
- **Singleton queries**: `has_singleton`

`LinterDB` is a plain C++ singleton (not an `Object`), created/destroyed explicitly by the linter main.

### `stubs/classdb_stub.h/.cpp` — ClassDB Link-Time Override

Provides replacement definitions for `ClassDB` static methods. The real `ClassDB` populates itself via `GDREGISTER_CLASS` macros during engine init; the stub versions delegate every call to `LinterDB`.

Key detail: `ClassDB::get_method()` returns a `MethodBind*`. The analyzer inspects `MethodBind` to determine vararg status, static status, argument count, and return type. The stub provides `StubMethodBind` — a `MethodBind` subclass that stores metadata only, with no-op `call`/`ptrcall`/`validated_call`. Instances are created lazily and cached in a `HashMap<String, StubMethodBind*>`.

### `stubs/engine_stub.cpp` — Engine::has_singleton Override

Replaces `Engine::has_singleton()` to delegate to `LinterDB::has_singleton()`. This lets the analyzer recognize singleton access patterns (e.g., `Input.is_action_pressed()`).

### `stubs/project_settings_stub.cpp` — ProjectSettings Override

Stubs `has_autoload()` and `get_autoload()` to always return false/empty. Autoloads are project-specific and not available in the standalone linter context.

### `stubs/resource_loader_stub.cpp` — ResourceLoader Override

- `exists()` — delegates to `FileAccess::exists()` (real filesystem check)
- `load()` — always returns null with `ERR_UNAVAILABLE` (no actual resource loading)
- `get_resource_type()` — infers type from file extension (`.gd` → `"GDScript"`, `.tscn` → `"PackedScene"`, etc.)
- `path_remap()` — identity (no remapping)

### `stubs/script_server_stub.h/.cpp` — ScriptServer Override

Maintains a registry of global classes (`class_name` declarations) that the linter populates during pre-scan. Provides `is_global_class`, `get_global_class_path`, and `get_global_class_native_base` so the analyzer can resolve cross-script type references.

## Execution Flow

```
main()
 │
 ├─ 1. Parse CLI arguments (--dir, --db, --help)
 │
 ├─ 2. Minimal engine bootstrap
 │     ├─ Thread::make_main_thread()
 │     ├─ OS::initialize()
 │     ├─ register_core_types/drivers/settings/singletons
 │     ├─ register_server_types + TextServerDummy
 │     ├─ register_scene_types
 │     ├─ initialize_modules (CORE, SERVERS, SCENE levels)
 │     └─ GDScriptCache singleton
 │
 ├─ 3. run_linter(db_path, dir_path)
 │     │
 │     ├─ 3a. Load LinterDB from JSON (if --db provided)
 │     │       └─ Populates class/method/property/signal/enum/singleton data
 │     │
 │     ├─ 3b. Recursively collect .gd/.hm/.hmc files
 │     │
 │     ├─ 3c. Pre-scan: extract class_name and extends declarations
 │     │       ├─ Lightweight text scan (first 50 lines, no full parse)
 │     │       ├─ Resolve native base for each class_name (walk extends chain)
 │     │       └─ Register global classes via ScriptServerStub
 │     │
 │     ├─ 3d. For each script:
 │     │       ├─ GDScriptParser::parse(source, path)
 │     │       ├─ GDScriptAnalyzer::analyze()
 │     │       ├─ Collect and print errors
 │     │       └─ Collect and print warnings (DEBUG_ENABLED builds)
 │     │
 │     └─ 3e. Print summary (script count, error count, warning count)
 │
 └─ 4. Engine teardown (reverse order of init)
```

## Build System

The linter is built as a second `Program` target within the engine's SCons build, not as a standalone project. This is necessary because the engine's core types cannot be decoupled.

### Enabling the Build

Add to root `SConstruct` (after the platform SCsub):

```python
if env.get("linter", False):
    SConscript("linter/SCsub")
```

### Build Command

```
scons platform=windows target=editor dev_build=yes linter=yes
```

Produces `bin/homot-linter` (or `bin/homot-linter.exe` on Windows).

### Link-Time Symbol Override

The `SCsub` creates a cloned environment and builds `main_linter.cpp` + `stubs/*.cpp` as a separate program. The stub `.obj` files are listed before the engine static libraries in the linker input:

- **MSVC**: First definition wins — stub symbols take precedence over the same symbols in `core.lib`.
- **GCC/MinGW**: `-Wl,--allow-multiple-definition` — same effect.

This means the real `ClassDB::class_exists()` in `core.lib` is silently replaced by the stub in `classdb_stub.obj`.

## linterdb.json Generation

The dump is produced by running the engine with a `--dump-linterdb` flag (added to the engine's CLI handler). This iterates over:

1. `ClassDB::get_class_list()` — dumps every registered class with full method/property/signal/enum metadata
2. `Engine::get_singleton_list()` — dumps singleton names
3. `Variant` built-in type introspection — dumps constructors, methods, members, operators
4. `Utility functions` — dumps global functions
5. `Global enums/constants` — dumps from ClassDB's global scope

The dump should be regenerated when the engine version changes or when custom modules add/remove classes.

## Limitations and Future Work

### Current Limitations

- **No resource loading**: `preload()` and `load()` calls cannot resolve actual resources. The analyzer may report false positives for preloaded scripts or resources.
- **No autoloads**: Project autoloads are not available. Scripts referencing autoload singletons will see errors.
- **No GDExtension classes**: Only classes registered via the engine's `ClassDB` at dump time are available. GDExtension classes loaded at runtime are missing.
- **Single-pass analysis**: Scripts are analyzed independently. Cross-script type inference through `preload()` chains is limited.
- **Bootstrap overhead**: The engine bootstrap (`register_*_types`) is heavyweight. Startup time is dominated by type registration, not actual linting.

### Potential Improvements

- **Autoload support**: Parse `project.godot` to extract autoload entries and register them in the stub.
- **Incremental linting**: Cache parse results and only re-analyze changed files.
- **Cross-script resolution**: Use `GDScriptCache` more fully to resolve `preload()` dependencies between scripts in the linted directory.
- **Output formats**: JSON/SARIF output for CI integration.
- **Standalone binary**: Investigate statically linking only the required subset of engine code to reduce binary size and startup time.
- **Builtin type stubs**: The current `LinterDB` loads `builtin_types`, `utility_functions`, and `global_enums` from JSON but the stub layer does not yet feed these into the analyzer. These are currently served by the real engine code via the bootstrap. A fully standalone linter would need stubs for `Variant::get_utility_function_*` and related APIs.

## File Inventory

```
linter/
├── doc/
│   └── DESIGN.md              ← this file
├── stubs/
│   ├── linterdb.h/.cpp        ← JSON loader + query singleton
│   ├── classdb_stub.h/.cpp    ← ClassDB + StubMethodBind overrides
│   ├── engine_stub.h/.cpp     ← Engine::has_singleton override
│   ├── project_settings_stub.h/.cpp  ← ProjectSettings stubs
│   ├── resource_loader_stub.h/.cpp   ← ResourceLoader stubs
│   └── script_server_stub.h/.cpp     ← ScriptServer + global class registry
├── main_linter.cpp            ← CLI entry point + engine bootstrap
├── linter_run.h/.cpp          ← Lint logic (collect, pre-scan, analyze)
├── linterdb.json              ← Pre-generated type database dump
├── SConstruct                 ← Build manifest / documentation
└── SCsub                      ← Actual SCons build script
```
