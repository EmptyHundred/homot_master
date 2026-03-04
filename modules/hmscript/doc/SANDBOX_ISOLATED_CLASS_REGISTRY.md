# Sandbox Isolated Class Registry - Design Document

## Problem Statement

When loading multiple sandboxes, each may contain scripts with the same `class_name`. Using a global registry would cause conflicts:

```gdscript
# Sandbox A: user://mods/modA/enemy.gd
class_name Enemy extends Node

# Sandbox B: user://mods/modB/enemy.gd
class_name Enemy extends Node  # ❌ CONFLICT! Which Enemy?
```

**Requirements**:
1. Each sandbox has its own isolated class namespace
2. Scripts within a sandbox can extend other classes in the same sandbox
3. Scripts can still extend engine classes (Node, Resource, etc.)
4. No conflicts between sandboxes with identical class names
5. Clean separation - unloading a sandbox doesn't affect others

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                 GDScript Class Resolution                   │
│  (Modified to check sandbox-local registry first)           │
└───────────────────┬─────────────────────────────────────────┘
                    │
        ┌───────────┴───────────┐
        │                       │
        ▼                       ▼
┌──────────────────┐   ┌──────────────────┐
│ Sandbox Registry │   │ Global Registry  │
│   (per-sandbox)  │   │  (ScriptServer)  │
│                  │   │                  │
│  • Local classes │   │  • res:// classes│
│  • Isolated      │   │  • Engine classes│
│  • Auto-cleanup  │   │  • Shared        │
└──────────────────┘   └──────────────────┘
```

**Resolution Order**:
1. Check sandbox-local registry (if script belongs to a sandbox)
2. Fall back to global ScriptServer registry
3. This allows extending both local and engine classes

---

## Design: Two-Layer Registry System

### Layer 1: Per-Sandbox Class Registry

Each `HMSandbox` maintains its own class registry:

```cpp
// In HMSandbox
class SandboxClassRegistry {
    HashMap<String, ClassInfo> local_classes;

    struct ClassInfo {
        String class_name;
        String script_path;
        String base_type;
        Ref<GDScript> script_ref;
    };
};
```

### Layer 2: Modified GDScript Resolution

Hook into GDScript's class resolution to check sandbox registries:

```cpp
// Pseudo-code for GDScript class lookup
Ref<Script> resolve_class(const String &class_name, GDScriptInstance *context) {
    // 1. If context belongs to a sandbox, check sandbox registry first
    if (context && context->get_sandbox_profile_id() != "") {
        HMSandbox *sandbox = find_sandbox_by_profile(context->get_sandbox_profile_id());
        if (sandbox) {
            Ref<Script> script = sandbox->lookup_local_class(class_name);
            if (script.is_valid()) {
                return script;  // Found in sandbox!
            }
        }
    }

    // 2. Fall back to global registry
    return ScriptServer::get_global_class_script(class_name);
}
```

---

## Implementation Plan

### Phase 1: Add Sandbox Class Registry

**File**: `modules/hmscript/sandbox/sandbox_class_registry.h`

```cpp
#pragma once

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"

namespace hmsandbox {

// Local class registry for a single sandbox
class SandboxClassRegistry {
public:
    struct ClassInfo {
        String class_name;       // Local class name (e.g., "Enemy")
        String script_path;      // Full path to script file
        String base_type;        // Base class name (may be local or global)
        String icon_path;        // Optional icon
        bool is_abstract;
        bool is_tool;
        Ref<GDScript> cached_script;  // Keep script loaded
    };

private:
    HashMap<String, ClassInfo> name_to_class;  // class_name -> ClassInfo
    HashMap<String, String> path_to_name;      // script_path -> class_name

public:
    SandboxClassRegistry() = default;
    ~SandboxClassRegistry() = default;

    // Register a class in this sandbox
    void register_class(const ClassInfo &p_info);

    // Unregister a specific class
    void unregister_class(const String &p_class_name);

    // Clear all registrations
    void clear();

    // Lookup by class name
    bool has_class(const String &p_class_name) const;
    ClassInfo get_class_info(const String &p_class_name) const;
    Ref<GDScript> get_class_script(const String &p_class_name) const;

    // Lookup by script path
    bool has_script_path(const String &p_path) const;
    String get_class_name_for_path(const String &p_path) const;

    // Get all registered classes
    PackedStringArray get_all_class_names() const;

    // Debug
    void print_registry() const;
};

} // namespace hmsandbox
```

---

### Phase 2: Integrate Registry into HMSandbox

**Modify**: `modules/hmscript/sandbox/sandbox_runtime.h`

```cpp
class HMSandbox : public Node {
    GDCLASS(HMSandbox, Node);

private:
    String profile_id;
    Ref<PackedScene> packed_scene;
    Node *root_node;
    PackedStringArray dependencies;
    SandboxProfile *profile;

    // ✨ NEW: Sandbox-local class registry
    SandboxClassRegistry class_registry;

public:
    // ✨ NEW: Class lookup API
    bool has_local_class(const String &p_class_name) const;
    Ref<GDScript> lookup_local_class(const String &p_class_name) const;
    SandboxClassRegistry &get_class_registry() { return class_registry; }
    const SandboxClassRegistry &get_class_registry() const { return class_registry; }

    // Existing methods...
};
```

---

### Phase 3: Scan and Register Classes on Load

**Modify**: `modules/hmscript/sandbox/sandbox_runtime.cpp`

Add a helper function to scan and register classes:

```cpp
// Helper: Scan directory and register all classes
static void scan_and_register_classes(
    HMSandbox *p_sandbox,
    const String &p_directory
) {
    Ref<DirAccess> dir = DirAccess::open(p_directory);
    if (dir.is_null()) {
        return;
    }

    GDScriptLanguage *lang = GDScriptLanguage::get_singleton();
    if (!lang) {
        return;
    }

    dir->list_dir_begin();
    String file_name = dir->get_next();

    while (!file_name.is_empty()) {
        if (file_name.begins_with(".")) {
            file_name = dir->get_next();
            continue;
        }

        String full_path = p_directory.path_join(file_name);

        if (dir->current_is_dir()) {
            // Recursively scan subdirectories
            scan_and_register_classes(p_sandbox, full_path);
        } else if (file_name.ends_with(".gd") || file_name.ends_with(".hm") || file_name.ends_with(".hmc")) {
            // Extract class info using GDScriptLanguage::get_global_class_name()
            String base_type, icon_path;
            bool is_abstract = false, is_tool = false;

            String class_name = lang->get_global_class_name(
                full_path,
                &base_type,
                &icon_path,
                &is_abstract,
                &is_tool
            );

            if (!class_name.is_empty()) {
                // Load the script (will be cached)
                Ref<GDScript> script = ResourceLoader::load(
                    full_path,
                    "",
                    ResourceFormatLoader::CACHE_MODE_REUSE
                );

                if (script.is_valid()) {
                    // Register in sandbox-local registry
                    SandboxClassRegistry::ClassInfo info;
                    info.class_name = class_name;
                    info.script_path = full_path;
                    info.base_type = base_type;
                    info.icon_path = icon_path;
                    info.is_abstract = is_abstract;
                    info.is_tool = is_tool;
                    info.cached_script = script;

                    p_sandbox->get_class_registry().register_class(info);

                    print_verbose(vformat(
                        "Sandbox '%s': Registered local class '%s' from '%s'",
                        p_sandbox->get_profile_id(),
                        class_name,
                        full_path
                    ));
                }
            }
        }

        file_name = dir->get_next();
    }

    dir->list_dir_end();
}
```

**In `HMSandbox::load()`**:

```cpp
HMSandbox *HMSandbox::load(const String &p_directory, const String &p_tscn_filename) {
    String full_path = p_directory.path_join(p_tscn_filename);

    // Load the scene
    Error err = OK;
    Ref<Resource> resource = ResourceLoader::load(
        full_path,
        "",
        ResourceFormatLoader::CACHE_MODE_IGNORE,
        &err
    );
    ERR_FAIL_COND_V(err != OK || resource.is_null(), nullptr);

    Ref<PackedScene> scene = resource;
    ERR_FAIL_COND_V(scene.is_null(), nullptr);

    // Generate unique profile ID
    String profile_id = "Sandbox_" + generate_uuid();

    // Collect dependencies
    PackedStringArray deps = collect_dependencies(p_directory);

    // Create sandbox instance
    HMSandbox *sandbox = memnew(HMSandbox);
    sandbox->set_profile_id(profile_id);
    sandbox->set_name(profile_id);

    // ✨ NEW: Scan and register all classes in the directory
    // This MUST happen before scene instantiation so that scripts can find their base classes
    scan_and_register_classes(sandbox, p_directory);

    // Now configure scripts to use this sandbox profile
    for (int i = 0; i < deps.size(); i++) {
        Ref<Resource> res = ResourceCache::get_ref(deps[i]);
        if (res.is_null()) continue;

        Ref<GDScript> gds = res;
        if (gds.is_valid()) {
            gds->set_sandbox_enabled(true, profile_id);
        }
    }

    // Ensure GDScriptLanguage profile exists
    GDScriptLanguage *lang = GDScriptLanguage::get_singleton();
    SandboxProfile *profile_ptr = nullptr;
    if (lang) {
        profile_ptr = lang->ensure_sandbox_profile(profile_id);
    }

    // Instantiate the scene
    Node *instance = scene->instantiate();
    ERR_FAIL_COND_V(!instance, nullptr);

    // Configure sandbox
    sandbox->set_profile(profile_ptr);
    sandbox->set_packed_scene(scene);
    sandbox->set_root_node(instance);
    sandbox->set_load_directory(p_directory);
    sandbox->set_scene_filename(p_tscn_filename);
    sandbox->set_dependencies(deps);

    // Register with manager
    if (hm_sandbox_manager) {
        hm_sandbox_manager->register_sandbox(sandbox);
    }

    return sandbox;
}
```

**In `HMSandbox::unload()`**:

```cpp
void HMSandbox::unload() {
    // ✨ NEW: Clear local class registry
    class_registry.clear();

    // Remove all children
    set_root_node(nullptr);

    // Clear GDScript cache
    if (packed_scene.is_valid()) {
        PackedStringArray deps = get_dependencies();
        for (int i = 0; i < deps.size(); i++) {
            String actual_path = deps[i];
            if (actual_path.ends_with(".hm") || actual_path.ends_with(".hmc")) {
                GDScriptCache::remove_script(actual_path);
            }
        }
        set_packed_scene(Ref<PackedScene>());
    }
}
```

---

### Phase 4: Hook GDScript Class Resolution

This is the **critical part** - we need to intercept GDScript's class name resolution.

**Option A: Modify GDScriptAnalyzer** (Recommended)

The GDScriptAnalyzer resolves class names during compilation. We need to hook into `resolve_class_identifier()`.

**File to modify**: `modules/gdscript/gdscript_analyzer.cpp`

Find the function that resolves class names (likely `reduce_identifier()` or similar), and add:

```cpp
// In GDScriptAnalyzer::resolve_class_identifier() or similar
GDScriptParser::DataType resolve_class_identifier(const String &p_name, const GDScriptParser::Node *p_source) {
    // ✨ NEW: Check if we're analyzing a script that belongs to a sandbox
    if (parser && parser->script_path != "") {
        // Find which sandbox owns this script (if any)
        HMSandbox *sandbox = find_sandbox_for_script(parser->script_path);

        if (sandbox) {
            // Check sandbox-local registry first
            if (sandbox->has_local_class(p_name)) {
                Ref<GDScript> local_script = sandbox->lookup_local_class(p_name);
                if (local_script.is_valid()) {
                    // Return a DataType pointing to this local script
                    GDScriptParser::DataType type;
                    type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
                    type.kind = GDScriptParser::DataType::SCRIPT;
                    type.script_type = local_script;
                    type.script_path = local_script->get_path();
                    type.is_constant = true;
                    return type;
                }
            }
        }
    }

    // Fall back to global registry (existing code)
    return resolve_class_from_global_registry(p_name);
}
```

**Critical requirement**: We need a way to map from a script path to its owning sandbox.

---

### Phase 5: Sandbox-Script Mapping Service

**File**: `modules/hmscript/sandbox/sandbox_manager.h` and `.cpp`

Add a global mapping service:

```cpp
// In HMSandboxManager
class HMSandboxManager : public Object {
    // ... existing members ...

private:
    HashMap<String, HMSandbox *> script_path_to_sandbox;

public:
    // Map a script path to its owning sandbox
    void register_script_path(const String &p_path, HMSandbox *p_sandbox);
    void unregister_script_path(const String &p_path);

    // Lookup which sandbox owns a script
    HMSandbox *find_sandbox_for_script(const String &p_script_path);
};
```

**Usage in `HMSandbox::load()`**:

```cpp
// After registering classes
if (hm_sandbox_manager) {
    // Register all script paths with the manager
    PackedStringArray class_names = sandbox->get_class_registry().get_all_class_names();
    for (int i = 0; i < class_names.size(); i++) {
        auto info = sandbox->get_class_registry().get_class_info(class_names[i]);
        hm_sandbox_manager->register_script_path(info.script_path, sandbox);
    }
}
```

**Cleanup in `HMSandbox::unload()`**:

```cpp
if (hm_sandbox_manager) {
    PackedStringArray class_names = class_registry.get_all_class_names();
    for (int i = 0; i < class_names.size(); i++) {
        auto info = class_registry.get_class_info(class_names[i]);
        hm_sandbox_manager->unregister_script_path(info.script_path);
    }
}
```

---

### Phase 6: GDScript Integration Helper

Create a helper function accessible from GDScript code:

**File**: `modules/gdscript/gdscript_utility_functions.cpp`

Add a new utility function:

```cpp
// Helper function callable from GDScript analyzer
static HMSandbox *find_sandbox_for_script(const String &p_script_path) {
    // Access the global sandbox manager
    HMSandboxManager *manager = hmsandbox::get_global_sandbox_manager();
    if (!manager) {
        return nullptr;
    }
    return manager->find_sandbox_for_script(p_script_path);
}
```

---

## Alternative Approach: Runtime Resolution (Simpler)

If modifying the analyzer is too invasive, we can use **runtime resolution** instead:

### Concept
Instead of resolving at compile time, resolve at runtime when creating script instances.

**Modify**: `modules/gdscript/gdscript.cpp` in `GDScript::instance_create()`

```cpp
ScriptInstance *GDScript::instance_create(Object *p_this) {
    // ... existing code to create GDScriptInstance ...

    GDScriptInstance *instance = memnew(GDScriptInstance);
    instance->owner = p_this;
    instance->script = Ref<GDScript>(this);
    instance->sandbox_enabled = sandbox_enabled;
    instance->sandbox_profile_id = sandbox_profile_id;

    // ✨ NEW: If this script belongs to a sandbox, resolve base class from sandbox
    if (!sandbox_profile_id.is_empty() && base.is_valid()) {
        // Check if base class should be resolved from sandbox registry
        HMSandbox *sandbox = find_sandbox_by_profile_id(sandbox_profile_id);
        if (sandbox) {
            String base_name = base->get_global_name();
            if (!base_name.is_empty() && sandbox->has_local_class(base_name)) {
                // Re-resolve base class from sandbox registry
                Ref<GDScript> local_base = sandbox->lookup_local_class(base_name);
                if (local_base.is_valid()) {
                    instance->script->base = local_base;
                }
            }
        }
    }

    // ... rest of instance creation ...
}
```

**Pros**:
- Simpler - no analyzer modifications
- Works at runtime

**Cons**:
- Base class resolution happens at instantiation, not compilation
- May have edge cases with static analysis

---

## Complete Flow Diagram

```
┌─────────────────────────────────────────┐
│    HMSandbox::load(directory)           │
└────────────────┬────────────────────────┘
                 │
                 ├──> 1. Load PackedScene
                 │
                 ├──> 2. Generate unique profile_id
                 │
                 ├──> 3. scan_and_register_classes()
                 │         │
                 │         ├─> Scan directory recursively
                 │         ├─> For each .gd/.hm file:
                 │         │     ├─> Extract class_name via get_global_class_name()
                 │         │     ├─> Load script via ResourceLoader
                 │         │     └─> Register in sandbox->class_registry
                 │         │
                 │         └─> Register script paths with HMSandboxManager
                 │
                 ├──> 4. Configure scripts with sandbox profile
                 │
                 ├──> 5. Instantiate scene
                 │         │
                 │         └─> GDScript class resolution:
                 │               ├─> Check sandbox-local registry
                 │               └─> Fall back to global registry
                 │
                 └──> 6. Return configured sandbox


┌─────────────────────────────────────────┐
│    Script extends LocalClass            │
└────────────────┬────────────────────────┘
                 │
                 ├──> GDScriptAnalyzer::resolve_class()
                 │         │
                 │         ├─> find_sandbox_for_script(this_script_path)
                 │         │
                 │         ├─> sandbox->has_local_class("LocalClass")?
                 │         │      ├─ Yes -> Return sandbox local script
                 │         │      └─ No  -> Check global ScriptServer
                 │         │
                 │         └─> Return resolved script
                 │
                 └──> Compilation succeeds with correct base class
```

---

## Edge Cases & Solutions

### 1. **Cross-Sandbox Inheritance**
**Problem**: SandboxA's script tries to extend SandboxB's class

```gdscript
# In SandboxA:
extends SomethingFromSandboxB  # Should this work?
```

**Solution**: **Deny by design**
- Each sandbox is isolated
- If needed, expose classes via global registry explicitly
- Add warning if resolution fails

### 2. **Name Collision with Global Classes**
**Problem**: Sandbox class has same name as res:// class

```gdscript
# In sandbox: user://mods/mod1/node.gd
class_name Node extends RefCounted  # Conflicts with engine Node!
```

**Solution**: **Sandbox registry takes precedence**
- For scripts IN the sandbox: use sandbox registry first
- For scripts OUTSIDE the sandbox: use global registry
- Warn if shadowing an engine class

### 3. **Circular Dependencies**
**Problem**: ClassA extends ClassB, ClassB extends ClassA

**Solution**: **Detect during registration**
```cpp
void SandboxClassRegistry::register_class(const ClassInfo &p_info) {
    // Check for circular dependency
    String current_base = p_info.base_type;
    HashSet<String> visited;
    visited.insert(p_info.class_name);

    while (!current_base.is_empty()) {
        if (visited.has(current_base)) {
            ERR_PRINT(vformat(
                "Circular dependency detected: %s -> %s",
                p_info.class_name, current_base
            ));
            return;
        }
        visited.insert(current_base);

        if (!has_class(current_base)) {
            break;  // Base is in global registry or external
        }

        current_base = get_class_info(current_base).base_type;
    }

    // No circular dependency, safe to register
    name_to_class[p_info.class_name] = p_info;
    path_to_name[p_info.script_path] = p_info.class_name;
}
```

### 4. **Hot Reload**
**Problem**: Script modified while sandbox is running

**Solution**: **Re-scan specific file**
```cpp
void HMSandbox::reload_script(const String &p_script_path) {
    // Extract updated class info
    GDScriptLanguage *lang = GDScriptLanguage::get_singleton();
    String base_type, icon_path;
    bool is_abstract, is_tool;

    String class_name = lang->get_global_class_name(
        p_script_path, &base_type, &icon_path, &is_abstract, &is_tool
    );

    if (class_name.is_empty()) {
        // class_name was removed - unregister
        if (class_registry.has_script_path(p_script_path)) {
            String old_name = class_registry.get_class_name_for_path(p_script_path);
            class_registry.unregister_class(old_name);
        }
    } else {
        // Re-register with updated info
        Ref<GDScript> script = ResourceLoader::load(p_script_path);
        if (script.is_valid()) {
            SandboxClassRegistry::ClassInfo info;
            info.class_name = class_name;
            info.script_path = p_script_path;
            info.base_type = base_type;
            info.cached_script = script;

            class_registry.register_class(info);  // Will update existing
        }
    }
}
```

---

## Testing Strategy

### Unit Tests

**File**: `modules/hmscript/tests/test_sandbox_class_registry.h`

```cpp
TEST_CASE("[SandboxClassRegistry] Register and lookup classes") {
    SandboxClassRegistry registry;

    ClassInfo info;
    info.class_name = "TestClass";
    info.script_path = "user://test/test.gd";
    info.base_type = "Node";

    registry.register_class(info);

    CHECK(registry.has_class("TestClass"));
    CHECK(registry.get_class_info("TestClass").script_path == "user://test/test.gd");
}

TEST_CASE("[SandboxClassRegistry] Circular dependency detection") {
    // Create two classes that reference each other
    // Verify registration fails with error
}
```

### Integration Tests

**File**: `modules/hmscript/tests/test_sandbox_isolation.gd`

```gdscript
func test_isolated_class_registration():
    # Create two sandboxes with same class name
    var sandbox_a = HMSandbox.load("user://test/mod_a", "scene.tscn")
    var sandbox_b = HMSandbox.load("user://test/mod_b", "scene.tscn")

    # Both should succeed despite name collision
    assert_not_null(sandbox_a)
    assert_not_null(sandbox_b)

    # Each should have their own Enemy class
    assert_true(sandbox_a.has_local_class("Enemy"))
    assert_true(sandbox_b.has_local_class("Enemy"))

    # But they should be different scripts
    var enemy_a = sandbox_a.lookup_local_class("Enemy")
    var enemy_b = sandbox_b.lookup_local_class("Enemy")
    assert_ne(enemy_a.get_path(), enemy_b.get_path())

    # Cleanup
    sandbox_a.unload()
    sandbox_b.unload()

func test_local_inheritance():
    # Load sandbox with BaseClass and DerivedClass
    var sandbox = HMSandbox.load("user://test/inheritance", "scene.tscn")

    # Instantiate derived class
    var derived_script = sandbox.lookup_local_class("DerivedClass")
    var instance = derived_script.new()

    # Verify it properly extends BaseClass
    assert_true(instance is BaseClass)  # This would normally fail without isolation

    sandbox.unload()
```

---

## Performance Considerations

### Registration Time
- **Impact**: O(n) where n = number of scripts
- **Mitigation**:
  - Cache `get_global_class_name()` results
  - Parallel scanning for large directories
  - Lazy registration on first access

### Lookup Time
- **Impact**: O(1) hash map lookup per class resolution
- **Optimization**: Keep Ref<GDScript> cached to avoid reloading

### Memory
- **Impact**: Each sandbox holds references to all its scripts
- **Mitigation**:
  - Clear cache when unloading
  - Use weak references if possible
  - Share script data between instances

---

## Implementation Checklist

### Phase 1: Core Registry
- [ ] Create `sandbox_class_registry.h` and `.cpp`
- [ ] Implement `register_class()`, `unregister_class()`, `lookup()`
- [ ] Add circular dependency detection
- [ ] Unit tests for registry operations

### Phase 2: Sandbox Integration
- [ ] Add `SandboxClassRegistry` member to `HMSandbox`
- [ ] Implement `has_local_class()` and `lookup_local_class()`
- [ ] Add `scan_and_register_classes()` helper
- [ ] Call registration in `HMSandbox::load()`
- [ ] Call cleanup in `HMSandbox::unload()`

### Phase 3: Manager Mapping
- [ ] Add script-to-sandbox mapping in `HMSandboxManager`
- [ ] Implement `register_script_path()` and `find_sandbox_for_script()`
- [ ] Register paths during sandbox load
- [ ] Unregister paths during sandbox unload

### Phase 4: GDScript Hook (Choose one approach)
- [ ] **Approach A**: Modify `GDScriptAnalyzer::resolve_class_identifier()`
- [ ] **Approach B**: Modify `GDScript::instance_create()` for runtime resolution
- [ ] Add sandbox registry check before global registry
- [ ] Test with inheritance scenarios

### Phase 5: Edge Cases
- [ ] Handle name collisions with warnings
- [ ] Prevent cross-sandbox inheritance
- [ ] Detect circular dependencies
- [ ] Support hot reload (optional)

### Phase 6: Testing
- [ ] Unit tests for registry
- [ ] Integration tests for isolation
- [ ] Performance benchmarks
- [ ] Test with multiple sandboxes
- [ ] Test inheritance chains

---

## Summary

This design provides **true isolation** between sandboxes:

✅ **Isolated Namespaces**: Each sandbox has its own class registry
✅ **No Conflicts**: Multiple sandboxes can have classes with same names
✅ **Local Inheritance**: Scripts can extend other classes in same sandbox
✅ **Global Fallback**: Can still extend engine classes (Node, Resource, etc.)
✅ **Clean Separation**: Unloading one sandbox doesn't affect others
✅ **Automatic**: Registration happens transparently during load

**Key Difference from Previous Design**:
- Previous: Used global ScriptServer (conflicts possible)
- This: Per-sandbox registry with isolated namespaces (no conflicts)

**Recommended Implementation Order**:
1. Start with Phase 1-3 (registry + integration)
2. Test manually before adding GDScript hooks
3. Implement Phase 4 Approach B (runtime resolution) first - simpler
4. If needed, upgrade to Approach A (compile-time resolution) later

Would you like me to start implementing any specific phase?
