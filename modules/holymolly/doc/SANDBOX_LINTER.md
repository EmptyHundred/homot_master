# Sandbox Linter (`--lint-sandbox`)

The sandbox linter performs static analysis on all files within a sandbox directory,
covering GDScript files (`.hm`, `.hmc`) as well as scene and resource files
(`.tscn`, `.tres`).

## Usage

```bash
homot --lint-sandbox <directory>            # Text output
homot --lint-sandbox <directory> --lint-json # JSON output
```

**Exit code:** `0` (PASS) if no errors, `1` (FAIL) if any errors found.
Warnings do not affect the exit code.

## GDScript Linting (`.hm`, `.hmc`)

Uses the full GDScript analyzer (`GDScriptLanguage::validate`) for:

- Syntax errors
- Type checking
- Undefined reference detection
- Code style warnings

## Scene & Resource Linting (`.tscn`, `.tres`)

The scene/resource linter validates `.tscn` (scene) and `.tres` (text resource)
files without loading them into the engine. Checks are organized into two layers.

### Layer 1 -- Format & Structure Validation

These checks detect structural errors in the text file format. They are reported
as **errors**.

| Check | Description |
|-------|-------------|
| Header validation | First tag must be `gd_scene` or `gd_resource`. |
| Format version | Rejects files with `format` > 4 (unsupported). |
| `gd_resource` type | The `type` field is required in `gd_resource` headers. |
| `ext_resource` fields | Each `ext_resource` must have `path`, `type`, and `id`. |
| `sub_resource` fields | Each `sub_resource` must have `type` and `id`. |
| Duplicate IDs | No two `ext_resource` (or `sub_resource`) tags may share the same `id`. |
| Node `name` field | Every `[node]` tag must have a `name` field. |
| Connection fields | Every `[connection]` must have `from`, `to`, `signal`, and `method`. |
| Editable `path` | Every `[editable]` must have a `path` field. |
| Root node uniqueness | A scene must have exactly one root node (node without `parent`). |
| ExtResource references | `ExtResource("id")` in values must reference a declared `ext_resource`. |
| SubResource references | `SubResource("id")` in values must reference a declared `sub_resource`. |
| Parse errors | Any syntax error in tags or property values. |
| Unknown tags | Tags other than the expected ones are flagged. |
| Wrong file structure | e.g. `[node]` inside a `.tres` file, unexpected tags after `[resource]`. |

### Layer 2 -- Type System Validation

These checks use the engine's `ClassDB` to verify types, properties, and signals.
Most are reported as **warnings** to account for dynamic/script-defined members.

| Check | Severity | Description |
|-------|----------|-------------|
| Node type exists | Error | `type="CharacterBody3D"` -- verifies the class is registered. |
| Node type is a Node | Error | The class must inherit from `Node`. |
| `sub_resource` type exists | Error | Verifies the class is registered in `ClassDB`. |
| `sub_resource` type is Resource | Error | The class must inherit from `Resource`. |
| `gd_resource` type exists | Error | Verifies the resource type in the header. |
| `gd_resource` type is Resource | Error | The header type must inherit from `Resource`. |
| Node property exists | Warning | Checks `ClassDB::has_property` for the node's type. Skipped when the node has a `script` or `instance`. |
| `sub_resource` property exists | Warning | Checks properties against the sub-resource's type. |
| Resource property exists | Warning | Checks properties of the `[resource]` section against the header type. |
| Signal exists | Warning | `[connection signal=X from=Y]` -- checks `ClassDB::has_signal` on the source node's class. Skipped for instanced or script-typed nodes. |
| Node parent path | Warning | Verifies that the `parent` field references a previously declared node path. |
| Connection node paths | Warning | Verifies that `from` and `to` paths reference declared nodes. |

#### Property validation skip rules

Property validation is skipped in these cases to avoid false positives:

- Node has a `script` property (script may define exported variables)
- Node has an `instance` or `instance_placeholder` (inherited properties unknown)
- Property name starts with `metadata/` or `editor/` (dynamic/editor-only)

## Output Formats

### Text (default)

```
ERROR: player.tscn:5: Unknown node type 'CharacterBdy2D'.
WARNING: player.tscn:8: Property 'speeed' not found in class 'Node2D'.
ERROR: main.tscn:12: ExtResource references undeclared id '3'.

--- Lint Summary ---
Directory: /path/to/sandbox
Files scanned: 15
Errors: 2
Warnings: 1
Result: FAIL
```

### JSON (`--lint-json`)

```json
[
  {
    "file": "player.tscn",
    "errors": [
      {"line": 5, "column": 0, "message": "Unknown node type 'CharacterBdy2D'."}
    ],
    "warnings": [
      {"line": 8, "message": "Property 'speeed' not found in class 'Node2D'."}
    ]
  }
]
```

## Common AI-Generated File Errors Detected

The linter is particularly effective at catching mistakes common in AI-generated
`.tscn` / `.tres` files:

- **Typos in class names**: `Characterbody2D` instead of `CharacterBody2D`
- **Wrong resource reference IDs**: `ExtResource("5")` when only IDs 1-3 exist
- **Duplicate IDs**: Two `ext_resource` entries with the same `id`
- **Missing required fields**: `[node]` without `name`, `[connection]` without `signal`
- **Wrong property names**: `texure` instead of `texture` on a `Sprite2D`
- **Format errors**: Malformed values, unclosed brackets, bad syntax
- **Structural mistakes**: `[node]` tags in `.tres` files, wrong section ordering
