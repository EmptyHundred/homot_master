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

**Status: Implemented**

Ctrl+click or F12 on any identifier jumps to its definition.

### How It Works

1. Parse + analyze the file normally (no sentinel needed)
2. Walk the AST recursively to find the deepest `IdentifierNode` at cursor position
3. Read `identifier->source` enum to determine origin:

| Source | Resolution |
|---|---|
| `FUNCTION_PARAMETER` | `parameter_source->start_line` in same file |
| `LOCAL_VARIABLE`, `MEMBER_VARIABLE`, `INHERITED_VARIABLE`, `STATIC_VARIABLE` | `variable_source->start_line` in same file |
| `LOCAL_CONSTANT`, `MEMBER_CONSTANT` | `constant_source->start_line` in same file |
| `MEMBER_FUNCTION` | `function_source->start_line` in same file |
| `MEMBER_SIGNAL` | `signal_source->start_line` in same file |
| `MEMBER_CLASS` | `datatype.class_type->start_line` in same file |
| `LOCAL_ITERATOR`, `LOCAL_BIND` | `bind_source->start_line` in same file |
| `NATIVE_CLASS` | No location (returns null) |
| Global class name (fallback) | Looked up in `class_to_path` map |

4. Return LSP `Location { uri, range }` with 0-based line conversion

### AST Node Finding

The tree walker recursively descends through:
- `ClassNode::members` → functions, variables, inner classes
- `FunctionNode::body` → `SuiteNode`
- `SuiteNode::statements` → all statement types (if, for, while, match, return, assignment, expressions)
- Expression nodes → call arguments, binary/unary/ternary operands, subscript base/attribute

At each level, `_node_contains_position()` checks whether the node's `start_line..end_line` / `start_column..end_column` range contains the cursor, and prefers the deepest (most specific) match.

### Supported Patterns

- `var x = ...` → jump from usage of `x` to its `var` declaration
- `func foo():` → jump from call `foo()` to its `func` definition
- `signal my_sig` → jump from `my_sig.emit()` to signal declaration
- `class_name Foo` → jump from `Foo` usage in another file to the class file
- Function parameters → jump to the parameter in the function signature
- `for i in range(10):` → jump from usage of `i` to the `for` statement
- Inner classes, constants, enums → jump to their declaration

## Feature: Hover

**Status: Implemented**

Mouse-over any identifier to see its type and declaration kind.

### How It Works

Reuses the same AST walker as go-to-definition (`_find_identifier_at_position`):
1. Parse + analyze the file
2. Find the `IdentifierNode` at cursor position
3. Format the hover text based on `identifier->source` and `identifier->datatype`

### Hover Text Format

Displayed as a GDScript markdown code block:

| Source | Hover text |
|---|---|
| Parameter | `(parameter) name: Type` |
| Local variable | `(local variable) name: Type` |
| Local constant | `(local constant) name: Type` |
| Iterator (for loop) | `(iterator) name: Type` |
| Member variable | `(property) name: Type` |
| Member constant | `(constant) name: Type` |
| Member function | `func name(param: Type, ...) -> ReturnType` (full signature) |
| Signal | `(signal) name` |
| Inner class | `(class) name` |
| Native class | `(native class) name` |

For functions, the full signature is reconstructed from the `FunctionNode` — parameter names, types, and return type.

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
