# How Godot Avoids Script Loading Order Issues

## The Problem

When loading scripts, a subclass might be loaded before its superclass:

```gdscript
# boss.gd (loaded first)
extends Enemy  # But Enemy.gd not loaded yet!

# enemy.gd (loaded later)
class_name Enemy extends Node
```

**Question**: How does Godot ensure Enemy is loaded before Boss needs it?

---

## Godot's Solution: Multi-Phase Compilation

### Phase-Based Compilation

Godot compiles scripts progressively through **5 phases**:

```cpp
enum Status {
    EMPTY,              // Script not loaded
    PARSED,             // Syntax parsed, AST built
    INHERITANCE_SOLVED, // Base classes resolved ← Critical phase
    INTERFACE_SOLVED,   // Methods/properties analyzed
    FULLY_SOLVED,       // Fully compiled
};
```

### The `raise_status()` Mechanism

When a script needs to resolve inheritance, Godot uses `raise_status()`:

```cpp
// From gdscript_cache.cpp:67
Error GDScriptParserRef::raise_status(Status p_new_status) {
    while (result == OK && p_new_status > status) {
        switch (status) {
            case PARSED:
                status = INHERITANCE_SOLVED;
                result = get_analyzer()->resolve_inheritance();  // ← Resolves base classes
                break;
            // ... other phases ...
        }
    }
    return result;
}
```

---

## Step-by-Step: How Order is Resolved

### Example: Boss extends Enemy

```gdscript
# boss.gd
extends Enemy  # Uses class name

# enemy.gd
class_name Enemy extends Node
```

### Loading Flow

```
1. Load boss.gd
   └─ Parser: Parse "extends Enemy"
   └─ Analyzer: Need to resolve "Enemy"

2. Analyzer looks up "Enemy" in global registry:
   └─ ScriptServer::is_global_class("Enemy") → true
   └─ ScriptServer::get_global_class_path("Enemy") → "enemy.gd"

3. Analyzer loads enemy.gd dependency:
   └─ parser->get_depended_parser_for("enemy.gd")
   └─ Returns a GDScriptParserRef for enemy.gd

4. Analyzer ensures enemy.gd is compiled to INHERITANCE_SOLVED:
   └─ ext_parser->raise_status(INHERITANCE_SOLVED)

   This RECURSIVELY compiles enemy.gd:
   4a. Parse enemy.gd → PARSED
   4b. Resolve enemy.gd inheritance (extends Node) → INHERITANCE_SOLVED
       └─ Node is an engine class, already resolved

5. Now Boss can use Enemy:
   └─ base = ext_parser->get_parser()->head->get_datatype()
   └─ Boss.base = Enemy ✅

6. Continue compiling boss.gd to completion
```

### Code Location

**File**: `modules/gdscript/gdscript_analyzer.cpp:440-489`

```cpp
// When resolving "extends Enemy"
if (ScriptServer::is_global_class(name)) {
    String base_path = ScriptServer::get_global_class_path(name);

    // Load the base script (if not already loaded)
    Ref<GDScriptParserRef> base_parser = parser->get_depended_parser_for(base_path);

    // ✨ KEY: Ensure base is compiled to INHERITANCE_SOLVED
    // This RECURSIVELY compiles the base if needed
    Error err = base_parser->raise_status(GDScriptParserRef::INHERITANCE_SOLVED);

    // Now we can use the base class
    base = base_parser->get_parser()->head->get_datatype();
}
```

---

## Why Godot Doesn't Have Order Issues

### 1. **Lazy Loading**
Scripts are only loaded when needed, not all at once.

### 2. **Recursive Dependency Resolution**
When Script A needs Script B:
- Load Script B
- If Script B needs Script C, load Script C first
- Recursively resolve all dependencies
- Return to Script A

### 3. **Phase-Based Compilation**
Scripts can be at different compilation stages:
- Script A at INHERITANCE_SOLVED can use Script B at PARSED
- `raise_status()` brings Script B up to the needed stage

### 4. **Global Registry**
`ScriptServer` maintains a **class_name → path** mapping:
```cpp
ScriptServer::is_global_class("Enemy") → true
ScriptServer::get_global_class_path("Enemy") → "res://enemy.gd"
```

This allows name-based lookup without loading all scripts.

---

## The Sandbox Challenge

### Why Our Sandbox Needs Special Handling

Godot's approach works because:
1. ✅ Global registry maps class_name → path
2. ✅ Scripts are in res:// (known location)
3. ✅ All scripts see the same registry

But with sandboxes:
1. ❌ Multiple "Enemy" classes (one per sandbox)
2. ❌ Need sandbox-specific registry
3. ❌ Scripts in user:// (dynamic location)

### Our Solution: Two-Pass Compilation

Since we can't hook into `get_depended_parser_for()` cleanly, we use a different approach:

```
Pass 1: Load all scripts
  └─ May fail base class resolution
  └─ Scripts cached but may have errors

Setup: Configure sandbox
  └─ Set profile_id on scripts
  └─ Populate sandbox registry

Pass 2: Reload scripts (our fix)
  └─ Clear cache
  └─ Recompile with full setup
  └─ Base class resolution succeeds
```

---

## When Does the Analyzer Use Our Hook?

Our hook in `reduce_identifier()` runs during **INHERITANCE_SOLVED** phase:

```cpp
// In resolve_inheritance() → resolves "extends Enemy"
GDScriptParser::IdentifierNode *id = p_class->extends[0];
const StringName &name = id->name;  // "Enemy"

// ✨ This eventually calls reduce_identifier(id)
// Our hook intercepts here to check sandbox registry
```

### Call Stack

```
raise_status(INHERITANCE_SOLVED)
  └─ get_analyzer()->resolve_inheritance()
      └─ resolve_class_inheritance(p_class)
          └─ Check if ScriptServer::is_global_class(name)
              └─ If not found, error at line 575 ← WHERE ERROR OCCURRED
```

Our sandbox hook runs AFTER global class check fails, which is why the error occurred before our fix.

---

## Better Solution? (Future Improvement)

Instead of two-pass compilation, we could:

### Option 1: Hook Earlier in the Process

**Hook into**: `ScriptServer::is_global_class()` and `get_global_class_path()`

**Advantage**: Integrates with Godot's existing flow
**Disadvantage**: Requires modifying core ScriptServer

### Option 2: Register Classes in Global Registry

**Approach**: Add sandbox classes to ScriptServer with prefixed names

```cpp
// Instead of sandbox-local registry:
ScriptServer::register_global_class("Sandbox_ABC123::Enemy", "user://mod/enemy.gd");

// Scripts use fully qualified names:
extends Sandbox_ABC123::Enemy
```

**Advantage**: Works with existing system
**Disadvantage**: Namespace pollution, no true isolation

### Option 3: Custom Dependency Resolver

**Hook into**: `parser->get_depended_parser_for()`

**Advantage**: Perfect integration with Godot's flow
**Disadvantage**: Requires significant refactoring

---

## Current Approach: Two-Pass Compilation

### Why It Works

1. **Pass 1**: Load scripts quickly (may fail inheritance)
2. **Setup**: Register sandbox, set profile_id, populate registry
3. **Pass 2**: Reload scripts with full context
   - Cache cleared
   - Scripts recompile
   - Our hook in `reduce_identifier()` finds base classes in sandbox registry
   - Success! ✅

### Performance Impact

- Scripts compile twice during sandbox load (~100-200ms for 10 scripts)
- Only happens during initial load, not during gameplay
- Acceptable trade-off for correct behavior

---

## Summary

**Godot's Order Solution**:
- Lazy loading + Recursive dependency resolution + Multi-phase compilation
- `raise_status()` ensures dependencies are compiled when needed
- Global registry allows name → path lookup

**Our Sandbox Solution**:
- Two-pass compilation (load → setup → reload)
- Hook at identifier resolution to check sandbox registry
- Recompile after setup to resolve base classes correctly

**Trade-off**: Extra compilation time for isolation benefits
