# homot Server Design Document

## Overview

`homot serve` starts a unified Language Server that handles both standard LSP methods (for editors) and LSPA methods (for AI agents) in a single process. It reuses the engine's `GDScriptParser` and `GDScriptAnalyzer` — the same code that powers the Godot editor — plus custom linters for `.tscn`/`.tres` and `.gdshader` files.

The server communicates over stdin/stdout using JSON-RPC 2.0 with Content-Length framing (standard LSP base protocol). It inherits the linter's architecture: link-time stub overrides, embedded `linterdb.json` type database, and minimal engine bootstrap.

## Architecture

```
 ┌──────────────────────────────────────────────────────────────┐
 │                      homot serve                              │
 ├───────────────┬──────────────────────────────────────────────┤
 │ homot_main    │  Engine bootstrap, JSON-RPC message loop      │
 ├───────────────┼──────────────────────────────────────────────┤
 │ lsp_server    │  Unified dispatch: LSP + LSPA methods         │
 │               │  Document state, workspace scanning           │
 │               │  Diagnostics (scripts, resources, shaders)    │
 ├───────────────┼──────────────────────────────────────────────┤
 │ LSP handlers  │  completion, definition, hover, sig_help      │
 ├───────────────┼──────────────────────────────────────────────┤
 │ LSPA handlers │  query_engine (api/*), verifier (verify/*)    │
 ├───────────────┼──────────────────────────────────────────────┤
 │ workspace     │  Shared file collection + class scanning      │
 ├───────────────┼──────────────────────────────────────────────┤
 │ lsp_transport │  JSON-RPC framing over stdin/stdout           │
 ├───────────────┼──────────────────────────────────────────────┤
 │ lsp_protocol  │  LSP type definitions, JSON-RPC helpers       │
 ├───────────────┼──────────────────────────────────────────────┤
 │ stubs/        │  Same link-time overrides as the linter       │
 └───────────────┴──────────────────────────────────────────────┘
```

## Lifecycle

```
Editor/Agent spawns: homot serve
  │
  ├── Engine bootstrap (one-time, ~200ms)
  │     └── core types, TextServerDummy, GDScript module, embedded linterdb
  │
  ├── Message loop (blocking reads on stdin)
  │     │
  │     ├── initialize        → scan workspace, register classes
  │     │                       returns LSP + LSPA capabilities
  │     ├── initialized       → (no-op)
  │     │
  │     │  --- LSP methods (for editors) ---
  │     ├── textDocument/didOpen|didChange|didClose|didSave
  │     ├── textDocument/completion
  │     ├── textDocument/signatureHelp
  │     ├── textDocument/definition
  │     ├── textDocument/hover
  │     ├── workspace/didChangeWatchedFiles
  │     │
  │     │  --- LSPA methods (for AI agents) ---
  │     ├── api/class|classes|search|hierarchy|catalog|globals
  │     ├── verify/lint|check
  │     ├── code/typeof|signature|complete  (stubs)
  │     │
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

## Document Management

Open documents are stored in a `HashMap<String, DocumentState>` keyed by URI:
```cpp
struct DocumentState {
    String uri;
    String content;   // Full source text (full sync mode)
    int version;
};
```

Full document sync (TextDocumentSyncKind = 1): on every `didChange`, the client sends the entire document text.

## Diagnostics

On `didOpen`, `didChange`, and `didSave`, the server routes diagnostics based on file type:

| File type | Linter used | Diagnostic source |
|-----------|-------------|-------------------|
| `.gd`, `.hm`, `.hmc` | GDScriptParser + GDScriptAnalyzer | `"gdscript"` |
| `.tscn`, `.tres` | `resource_lint` | `"resource"` |
| `.gdshader` | `shader_lint` | `"gdshader"` |

Line numbers: parser uses 1-based, LSP uses 0-based. Convert with `MAX(0, line - 1)`.

## Feature: Completion

**Status: Implemented** (GDScript only)

The `GDScriptParser` has built-in completion support via a cursor sentinel character (`U+FFFF`). The server inserts it at the cursor position, re-parses with `for_completion=true`, then reads the `completion_context` to generate items.

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

## Feature: Go-to-Definition

**Status: Implemented** (GDScript only)

Ctrl+click or F12 on any identifier jumps to its definition. The server walks the AST to find the deepest `IdentifierNode` at the cursor position, then resolves its source (parameter, local variable, member, function, signal, class, etc.) to a file location.

## Feature: Hover

**Status: Implemented** (GDScript only)

Mouse-over any identifier to see its type and declaration kind. Reuses the same AST walker as go-to-definition.

## Feature: Signature Help

**Status: Implemented** (GDScript only)

Parameter hints when calling functions, triggered by `(` and `,`.

## Unified Initialize Response

The `initialize` response includes both standard LSP capabilities and LSPA-specific fields:

```jsonc
{
  "capabilities": {
    "textDocumentSync": { "openClose": true, "change": 1, "save": true },
    "completionProvider": { "triggerCharacters": [".", "@"] },
    "signatureHelpProvider": { "triggerCharacters": ["(", ","] },
    "definitionProvider": true,
    "hoverProvider": true
  },
  "serverInfo": { "name": "homot-lsp", "version": "0.1.0" },
  "lspaCapabilities": {
    "discover": ["class", "classes", "search", "hierarchy", "catalog", "globals"],
    "write": ["typeof", "signature", "complete"],
    "verify": ["lint", "check", "contract"]
  },
  "lspaStats": {
    "classes": 1025,
    "builtin_types": 39,
    "singletons": 39,
    "utility_functions": 114
  }
}
```

## Limitations

Same as the linter:
- No resource loading (`preload()` cannot resolve actual scripts)
- No autoloads
- No GDExtension classes
- Cross-script type inference is limited

Additionally:
- No incremental parsing — full re-parse on every change
- No rename/refactoring support
- No workspace-wide diagnostics (only open files)
- Completion/definition/hover only for GDScript (not resources or shaders)

## File Inventory

```
linter/lsp/
├── lsp_server.h/.cpp          ← Unified server (LSP + LSPA dispatch)
├── lsp_transport.h/.cpp       ← JSON-RPC over stdin/stdout
├── lsp_protocol.h             ← LSP types and JSON-RPC helpers
├── lsp_completion.h/.cpp      ← textDocument/completion handler
├── lsp_definition.h/.cpp      ← textDocument/definition handler
├── lsp_hover.h/.cpp           ← textDocument/hover handler
├── lsp_signature_help.h/.cpp  ← textDocument/signatureHelp handler
└── lsp_utils.h/.cpp           ← Symbol resolution utilities

linter/lspa/
├── query_engine.h/.cpp        ← LSPA DISCOVER handlers (api/*)
├── verifier.h/.cpp            ← LSPA VERIFY handlers (verify/*)
└── formatter.h/.cpp           ← LSPA output formatting

linter/vscode/
├── package.json               ← VS Code extension manifest
├── extension.js               ← Spawns 'homot serve' via vscode-languageclient
├── language-configuration.json
└── syntaxes/
    └── gdscript.tmLanguage.json
```

## Build

```
scons platform=windows target=template_debug linter=yes
```

Produces `bin/linter/homot.<platform>.<target>.<arch>.exe`.
