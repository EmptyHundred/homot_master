# homot-linter Usage Guide

## Overview

`homot-linter` is a standalone GDScript linter that checks `.gd`, `.hm`, and `.hmc` scripts for parse errors, type errors, and warnings — the same checks the Godot editor performs — without running the engine.

It requires a `linterdb.json` file containing a snapshot of the engine's type system (classes, methods, properties, signals, enums, singletons). This file is generated once per engine version using the `--dump-linterdb` flag on the main engine binary.

## Quick Start

```bash
# Lint a single file
homot-linter --db linterdb.json player.gd

# Lint a directory (recursive)
homot-linter --db linterdb.json scripts/

# Lint multiple targets at once
homot-linter --db linterdb.json player.gd enemy.gd scripts/ai/
```

## Command-Line Reference

```
Usage: homot-linter [options] <path> [<path> ...]
```

### Arguments

| Argument | Description |
|----------|-------------|
| `<path>` | One or more files or directories to lint. Directories are scanned recursively for `.gd`, `.hm`, and `.hmc` files. |

### Options

| Option | Description |
|--------|-------------|
| `--db <path>` | Path to `linterdb.json`. Required for resolving engine types (classes, methods, properties, etc.). Without it, only basic parse checks work. |
| `--help`, `-h` | Show help message and exit. |

## Generating linterdb.json

The linter database is a JSON snapshot of the engine's registered classes, methods, properties, signals, enums, singletons, and utility functions. Generate it by running the engine with:

```bash
homot --dump-linterdb
```

This writes `linterdb.json` to the current directory. The file is ~7MB and should be regenerated when:
- The engine version changes
- Custom modules (e.g., holymolly) add or remove classes
- GDExtension plugins change registered types

The file can be committed to version control or distributed alongside the linter binary.

## Output Format

The linter prints results to **stdout**. Each script gets one of:

```
  OK: path/to/script.gd
  SKIP (empty): path/to/empty.gd
  ERROR: path/to/script.gd:3:14: Cannot assign a value of type "String" as "int".
  WARN:  path/to/script.gd:10: [UNUSED_VARIABLE] The local variable "x" is declared but never used.
```

Error format: `ERROR: <file>:<line>:<column>: <message>`
Warning format: `WARN: <file>:<line>: [<code>] <message>`

A summary is printed at the end:

```
=== Lint Summary ===
Scripts:  42
Errors:   3
Warnings: 7
```

### Exit Codes

| Code | Meaning |
|------|---------|
| `0`  | No errors found (warnings are allowed). |
| `1`  | One or more errors found, or invalid arguments. |

## What It Checks

The linter runs the same `GDScriptParser` and `GDScriptAnalyzer` as the Godot editor. This includes:

- **Syntax errors** — missing colons, unclosed parentheses, invalid tokens
- **Type errors** — assigning `String` to `int`, wrong return types, invalid argument types
- **Unknown identifiers** — calling methods that don't exist on `self`
- **Property type mismatches** — assigning wrong types to typed properties
- **Signal usage** — connecting to signals, emitting with correct arguments
- **Enum validation** — using enum values correctly
- **Cross-file class resolution** — `class_name` declarations are pre-scanned so scripts can reference each other

### Warnings

Warnings (unused variables, shadowed variables, etc.) are only available in `DEBUG_ENABLED` builds (`target=editor` or `target=template_debug`). They use the same warning codes as the Godot editor.

## Building

The linter is built as a secondary target within the engine's SCons build system.

### Prerequisites

The root `SConstruct` must include the linter SCsub (already configured):

```python
if env.get("linter", False):
    SConscript("linter/SCsub")
```

### Build Commands

```bash
# Recommended for distribution (~61MB)
scons platform=windows target=template_debug linter=yes

# With editor libs (~145MB, reuses editor build artifacts)
scons platform=windows target=editor linter=yes

# Development with debug symbols (~220MB)
scons platform=windows target=editor dev_build=yes linter=yes
```

The binary is output to `bin/homot-linter.<platform>.<target>.<arch>.exe`.

`target=template_debug` is recommended for distribution: it strips `TOOLS_ENABLED` code from engine libraries (the biggest size reduction) while keeping `DEBUG_ENABLED` for GDScript warnings.

## CI Integration

### Basic CI check

```bash
homot-linter --db linterdb.json project/scripts/
exit_code=$?
if [ $exit_code -ne 0 ]; then
    echo "Lint errors found!"
    exit 1
fi
```

### GitHub Actions example

```yaml
- name: Lint GDScript
  run: |
    ./homot-linter --db linterdb.json project/scripts/
```

### Parsing output

Errors follow the pattern `ERROR: <file>:<line>:<col>: <message>`, which is compatible with most CI annotation parsers and editor problem matchers.

## Known Limitations

- **No `preload()`/`load()` resolution** — Resources cannot be loaded at lint time. Scripts using `preload()` for type references may produce false positives.
- **No autoloads** — Project autoload singletons are not registered. References to autoloads will be unresolved.
- **No GDExtension classes** — Only classes present in the engine at `--dump-linterdb` time are available.
- **Method calls on native-typed variables** — Calling a nonexistent method on a variable typed as a native class (e.g., `var n: Node; n.fake()`) is not flagged. This is a GDScript analyzer limitation, not specific to the linter.
