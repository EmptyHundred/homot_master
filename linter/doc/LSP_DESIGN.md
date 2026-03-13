# homot-lsp Design Document

## Overview

`homot-lsp` is a standalone GDScript Language Server Protocol (LSP) server built on top of `homot-linter`. It reuses the engine's `GDScriptParser` and `GDScriptAnalyzer` — the same code that powers the Godot editor — but runs as a long-lived process communicating over stdin/stdout using JSON-RPC, making it usable from any LSP-capable editor (VS Code, Neovim, Emacs, etc.).

The LSP inherits the linter's architecture: link-time stub overrides, `linterdb.json` type database, and minimal engine bootstrap.

## Architecture

```
 ┌──────────────────────────────────────────────────────────┐
 │                    homot-lsp binary                       │
 ├───────────────┬──────────────────────────────────────────┤
 │ lsp_main      │  Entry point, engine bootstrap,          │
 │               │  JSON-RPC message loop                   │
 ├───────────────┼──────────────────────────────────────────┤
 │ lsp_server    │  Document state, request dispatch,       │
 │               │  diagnostics, completion, definition     │
 ├───────────────┼──────────────────────────────────────────┤
 │ lsp_transport │  JSON-RPC framing over stdin/stdout      │
 │               │  (Content-Length headers)                 │
 ├───────────────┼──────────────────────────────────────────┤
 │ lsp_protocol  │  LSP type definitions, JSON-RPC helpers  │
 ├───────────────┼──────────────────────────────────────────┤
 │ stubs/        │  Same link-time overrides as the linter  │
 └───────────────┴──────────────────────────────────────────┘
         │                        │
         │ links against          │ reads at runtime
         ▼                        ▼
 ┌───────────────┐       ┌─────────────────┐
 │ Engine libs   │       │ linterdb.json   │
 └───────────────┘       └─────────────────┘
```

## Lifecycle

```
Editor spawns homot-lsp --db linterdb.json
  │
  ├── Engine bootstrap (one-time, ~200ms)
  │     └── Same as linter: core types, TextServerDummy, GDScript module
  │
  ├── Message loop (blocking reads on stdin)
  │     │
  │     ├── initialize        → load LinterDB, scan workspace for class_name
  │     ├── initialized       → (no-op)
  │     ├── textDocument/*    → document management + analysis
  │     ├── shutdown          → flag for exit
  │     └── exit              → break loop
  │
  └── Engine teardown
```

## Transport Layer (lsp_transport)

Standard LSP base protocol over stdin/stdout:
- Binary mode on Windows (`_setmode` on stdin/stdout)
- Read: parse `Content-Length` header, read exactly N bytes, JSON parse
- Write: `JSON::stringify` → `Content-Length: N\r\n\r\n` + body + `fflush`

## Document Management (lsp_server)

Open documents are stored in a `HashMap<String, DocumentState>` keyed by URI:
```cpp
struct DocumentState {
    String uri;
    String content;   // Full source text (full sync mode)
    int version;
};
```

Full document sync (TextDocumentSyncKind = 1): on every `didChange`, the client sends the entire document text. This is simpler and the parser needs full source anyway.

## Feature: Diagnostics

**Status: Implemented**

On `didOpen` and `didChange`:
1. `GDScriptParser::parse(source, path, false)` — full parse
2. `GDScriptAnalyzer::analyze()` — type checking
3. Collect `parser.get_errors()` → LSP `Diagnostic` with severity Error
4. Collect `parser.get_warnings()` → LSP `Diagnostic` with severity Warning
5. Publish via `textDocument/publishDiagnostics` notification

Line numbers: parser uses 1-based, LSP uses 0-based. Convert with `MAX(0, line - 1)`.

## Feature: Completion

**Status: Implemented**

### How the Parser's Completion Mode Works

The `GDScriptParser` has built-in completion support via a cursor sentinel character:

1. Insert `U+FFFF` (sentinel) into the source at the cursor byte offset
2. Call `parser.parse(modified_source, path, true)` — the `true` enables `for_completion`
3. The tokenizer recognizes the sentinel and tracks cursor position
4. During parsing, when the parser encounters tokens near the cursor, it populates `completion_context` with:
   - `type` — what kind of completion (26+ modes)
   - `current_class`, `current_function`, `current_suite` — scope at cursor
   - `node` — the AST node at cursor (with resolved `DataType` after analysis)
5. Call `analyzer.analyze()` — resolves types on the AST
6. Read `parser.get_completion_context()` and generate completions

### Cursor Position to Byte Offset

LSP sends (line, character) both 0-based. To insert the sentinel:
```
Split source into lines
Navigate to line[p_line]
Insert U+FFFF at character position p_character
Rejoin into single string
```

### Completion Context Types Handled

| Parser CompletionType | LSP behavior |
|---|---|
| `COMPLETION_IDENTIFIER` | List locals, class members, global classes, native classes, keywords, utility functions |
| `COMPLETION_ATTRIBUTE` | Resolve base type → list its methods, properties, signals, constants |
| `COMPLETION_ATTRIBUTE_METHOD` | Same as ATTRIBUTE but methods only |
| `COMPLETION_METHOD` | Same as IDENTIFIER but functions only |
| `COMPLETION_TYPE_NAME` | List available types (native classes, global classes, builtin types) |
| `COMPLETION_ANNOTATION` | List GDScript annotations (@export, @onready, etc.) |
| `COMPLETION_CALL_ARGUMENTS` | Enum values for typed parameters |
| `COMPLETION_NONE` | No completions |

### Querying Completions

For **IDENTIFIER** completion (most common — typing in open scope):
- Walk `current_suite` locals (variables, constants, parameters, iterators)
- Walk `current_class` members (variables, constants, functions, signals, enums, classes)
- Walk inheritance chain via LinterDB (native methods, properties, signals)
- Add global classes from ScriptServerStub
- Add native class names from LinterDB
- Add GDScript keywords and utility functions

For **ATTRIBUTE** completion (after `.`):
- The `context.node` is a `SubscriptNode` with `base` expression
- Read `base->datatype` after analysis to determine the type
- If `DataType::NATIVE` → query LinterDB for the native class's members
- If `DataType::CLASS` → walk the script class AST members
- If `DataType::BUILTIN` → query `Variant` API for the builtin type's members

### CompletionItem Mapping

```
Engine CODE_COMPLETION_KIND  →  LSP CompletionItemKind
─────────────────────────────────────────────────────
FUNCTION                     →  Function (3)
MEMBER                       →  Field (5)
VARIABLE                     →  Variable (6)
CONSTANT                     →  Constant (21)
CLASS                        →  Class (7)
SIGNAL                       →  Event (23)
ENUM                         →  Enum (13)
PLAIN_TEXT                   →  Keyword (14)
```

## Feature: Go-to-Definition

**Status: Planned (Phase 2)**

After parse+analyze, every `IdentifierNode` has:
- `source` enum: where the identifier was defined (LOCAL_VARIABLE, MEMBER_FUNCTION, NATIVE_CLASS, etc.)
- Pointer to the source node (e.g., `variable_source`, `function_source`)

Implementation:
1. Parse + analyze the file normally
2. Walk the AST to find the `IdentifierNode` at the cursor position (match start_line/start_column/end_column)
3. Read `identifier->source`:
   - Local/member sources → return the source node's file + start_line
   - Global class → look up in `class_to_path` map
   - Native class → no location available (return null)
4. Return LSP `Location { uri, range }`

### AST Node Finding

To find a node at (line, col):
- Start from `parser.get_tree()` (root ClassNode)
- Walk members → for functions, walk `body` (SuiteNode) → statements
- Compare `node->start_line`, `node->start_column`, `node->end_line`, `node->end_column`
- Return the most specific (deepest) node containing the position

## Feature: Hover

**Status: Planned (Phase 3)**

Same node-finding as go-to-definition, but returns the node's `DataType` formatted as a string instead of a location.

## Limitations

Same as the linter:
- No resource loading (`preload()` cannot resolve actual scripts)
- No autoloads
- No GDExtension classes
- Cross-script type inference is limited

Additionally:
- No incremental parsing — full re-parse on every change
- No rename/refactoring support
- No signature help (planned)
- No workspace-wide diagnostics (only open files)

## File Inventory

```
linter/lsp/
├── lsp_main.cpp        — Entry point + engine bootstrap + message loop
├── lsp_server.h/.cpp   — Document management, request dispatch, features
├── lsp_transport.h/.cpp — JSON-RPC over stdin/stdout
└── lsp_protocol.h      — LSP types and JSON-RPC helpers

linter/vscode/
├── package.json                 — VS Code extension manifest
├── extension.js                 — Spawns homot-lsp via vscode-languageclient
├── language-configuration.json  — Bracket/comment/indent rules
└── syntaxes/
    └── gdscript.tmLanguage.json — TextMate grammar for syntax highlighting
```

## Build

```
scons platform=windows target=template_debug linter=yes
```

Produces `bin/linter/homot-lsp.exe` (and `bin/linter/homot-linter.exe`).
