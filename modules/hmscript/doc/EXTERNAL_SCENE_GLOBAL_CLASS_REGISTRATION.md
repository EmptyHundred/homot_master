# External Scene Global Class Registration - Design Document

## Problem Statement

When loading external tscn files (especially from `user://` paths), the ScriptServer does not register GDScript classes that use the `class_name` directive into the global class registry. This causes missing class references when scripts attempt to extend these classes.

### Current Behavior
- **In Editor**: EditorFileSystem continuously scans `res://` paths and registers all `class_name` declarations
- **At Runtime**: Global classes are loaded from cache file (`res://.godot/global_script_class_cache.cfg`) at startup
- **External Scenes**: Scripts in scenes loaded from `user://` or other non-`res://` paths are never registered

### Impact
```gdscript
# In user://mods/mod1/base_enemy.gd
class_name BaseEnemy extends Node
# ...

# In user://mods/mod1/flying_enemy.gd
class_name FlyingEnemy extends BaseEnemy  # ❌ ERROR: BaseEnemy not found!
# ...
```

The inheritance chain breaks because `BaseEnemy` is not in the global class registry.

---

## Root Cause Analysis

### 1. **Global Class Registration Flow**
```
Editor Time:
  EditorFileSystem.scan()
    → detects scripts in res://
    → calls get_global_class_name()
    → registers via ScriptServer::add_global_class()
    → saves to res://.godot/global_script_class_cache.cfg

Runtime:
  ScriptServer::init_languages()
    → loads from global_script_class_cache.cfg
    → ONLY contains res:// scripts scanned by editor
```

### 2. **Scene Loading Flow**
```
ResourceLoader::load(path)
  → PackedScene loaded
  → Scripts referenced in scene are loaded
  → GDScriptCache processes scripts
  → Scripts compiled and instantiated
  → ❌ NO global class registration happens
```

### 3. **Why Editor Works**
- EditorFileSystem constantly watches `res://` for changes
- Every script change triggers re-scan and re-registration
- Cache file is always up-to-date with `res://` scripts
- `user://` scripts are never scanned by EditorFileSystem

---

## Solution Design

### Overview
Implement runtime global class registration for external scenes by:
1. Scanning scripts when external scenes are loaded
2. Extracting `class_name` information without full compilation
3. Registering classes temporarily at runtime
4. Cleaning up when scenes are unloaded

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    HMSandbox::load()                         │
│  (Entry point for external scene loading)                   │
└───────────────────────┬─────────────────────────────────────┘
                        │
                        ↓
┌─────────────────────────────────────────────────────────────┐
│         ExternalScriptClassRegistry (NEW)                   │
│  • Scans PackedScene for script dependencies               │
│  • Extracts class_name via get_global_class_name()         │
│  • Registers classes with ScriptServer                     │
│  • Tracks registration for cleanup                         │
└───────────────────────┬─────────────────────────────────────┘
                        │
                        ↓
┌─────────────────────────────────────────────────────────────┐
│              ScriptServer::add_global_class()               │
│  (Existing API - no changes needed)                        │
└─────────────────────────────────────────────────────────────┘
```

---

## Implementation Plan

### Phase 1: Create External Script Class Registry

**File**: `modules/hmscript/sandbox/external_script_class_registry.h`

```cpp
#pragma once

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

class PackedScene;

namespace hmsandbox {

// Registry for managing global classes from external (non-res://) scenes
class ExternalScriptClassRegistry {
public:
    struct ClassInfo {
        String class_name;      // The global class name
        String script_path;     // Path to the script
        String base_type;       // Base class name
        String icon_path;       // Icon path (optional)
        bool is_abstract;       // Is abstract flag
        bool is_tool;           // Is tool script flag
        String source_context;  // Sandbox ID or scene path for tracking
    };

    // Scan a scene for scripts and register their global classes
    // Returns list of registered class names
    static PackedStringArray scan_and_register_classes(
        const Ref<PackedScene> &p_scene,
        const String &p_scene_path,
        const String &p_context_id
    );

    // Scan a directory for scripts and register their global classes
    static PackedStringArray scan_and_register_directory(
        const String &p_directory,
        const String &p_context_id
    );

    // Unregister all classes registered under a specific context
    static void unregister_context(const String &p_context_id);

    // Unregister a specific class by name (if it belongs to the context)
    static void unregister_class(
        const String &p_class_name,
        const String &p_context_id
    );

    // Check if a class is registered as external
    static bool is_external_class(const String &p_class_name);

    // Get info about a registered external class
    static ClassInfo get_class_info(const String &p_class_name);

    // Get all registered classes for a context
    static PackedStringArray get_context_classes(const String &p_context_id);

private:
    // Track which classes are registered by which context
    static HashMap<String, ClassInfo> registered_classes;
    static HashMap<String, Vector<String>> context_to_classes;

    // Extract class info from a script file without full compilation
    static ClassInfo extract_class_info(
        const String &p_script_path,
        const String &p_context_id
    );
};

} // namespace hmsandbox
```

**Key Design Decisions**:
- **Context-based tracking**: Each sandbox/scene gets a unique context ID
- **Lazy registration**: Only register when scenes are actually loaded
- **Use existing API**: Leverage `GDScriptLanguage::get_global_class_name()` to extract class info
- **Clean separation**: Independent module that can be used by sandbox or other systems

---

### Phase 2: Integrate with HMSandbox Loading

**Modify**: `modules/hmscript/sandbox/sandbox_runtime.cpp`

**In `HMSandbox::load()`**, add registration after collecting dependencies:

```cpp
HMSandbox *HMSandbox::load(const String &p_directory, const String &p_tscn_filename) {
    // ... existing code to load scene ...

    // Generate unique profile ID
    String profile_id = "Sandbox_" + generate_uuid();

    // Collect all .hm and .hmc dependencies from the directory
    PackedStringArray deps = collect_dependencies(p_directory);

    // ===== NEW: Register global classes from external scripts =====
    PackedStringArray registered_classes =
        ExternalScriptClassRegistry::scan_and_register_directory(
            p_directory,
            profile_id  // Use sandbox profile_id as context
        );
    // ================================================================

    // Update all loaded .hm/.hmc scripts to use this sandbox's unique profile_id
    for (int i = 0; i < deps.size(); i++) {
        // ... existing code ...
    }

    // ... rest of existing code ...

    return sandbox;
}
```

**In `HMSandbox::unload()`**, add cleanup:

```cpp
void HMSandbox::unload() {
    // Remove all children (which cleans up the root node)
    set_root_node(nullptr);

    // ===== NEW: Unregister global classes =====
    if (!profile_id.is_empty()) {
        ExternalScriptClassRegistry::unregister_context(profile_id);
    }
    // ===========================================

    // Clear GDScript cache for PackedScene dependencies
    if (packed_scene.is_valid()) {
        // ... existing code ...
    }
}
```

---

### Phase 3: Implement ExternalScriptClassRegistry

**File**: `modules/hmscript/sandbox/external_script_class_registry.cpp`

**Key implementation details**:

1. **scan_and_register_directory()**:
   ```cpp
   - Use DirAccess to recursively scan directory
   - For each .gd/.hm/.hmc file:
     - Call extract_class_info()
     - If class_name found, register with ScriptServer
     - Track in registered_classes map
   - Return list of registered class names
   ```

2. **extract_class_info()**:
   ```cpp
   - Get GDScriptLanguage singleton
   - Call get_global_class_name(script_path, &base, &icon, &abstract, &tool)
   - If returns non-empty string, populate ClassInfo
   - Return ClassInfo struct
   ```

3. **unregister_context()**:
   ```cpp
   - Lookup context_id in context_to_classes map
   - For each class registered under that context:
     - Call ScriptServer::remove_global_class(class_name)
     - Remove from registered_classes map
   - Clear context from context_to_classes map
   ```

---

### Phase 4: Handle Edge Cases

#### 4.1 Name Collisions
**Problem**: External script uses same class_name as a res:// script

**Solution**:
```cpp
ClassInfo ExternalScriptClassRegistry::extract_class_info(...) {
    // ... extract class info ...

    // Check if class name already exists
    if (ScriptServer::has_global_class(class_name)) {
        String existing_path = ScriptServer::get_global_class_path(class_name);

        // If it's from res://, warn but don't override
        if (existing_path.begins_with("res://")) {
            WARN_PRINT(vformat(
                "External script '%s' attempts to register class '%s' "
                "but it's already registered from '%s'. Skipping external registration.",
                p_script_path, class_name, existing_path
            ));
            return ClassInfo(); // Empty info = skip
        }

        // If it's from another external source, warn about conflict
        if (is_external_class(class_name)) {
            WARN_PRINT(vformat(
                "Class name collision: '%s' is already registered from another external source. "
                "New registration from '%s' will override.",
                class_name, p_script_path
            ));
        }
    }

    return info;
}
```

#### 4.2 Dependency Order
**Problem**: Script A extends Script B, but B is registered after A

**Solution**: Two-pass registration
```cpp
static PackedStringArray scan_and_register_directory(...) {
    // PASS 1: Collect all class info
    Vector<ClassInfo> all_classes;
    for (each script file) {
        ClassInfo info = extract_class_info(script_path, context_id);
        if (!info.class_name.is_empty()) {
            all_classes.push_back(info);
        }
    }

    // PASS 2: Register in dependency order (base classes first)
    // Use topological sort based on base_type
    Vector<ClassInfo> sorted_classes = topological_sort(all_classes);

    for (const ClassInfo &info : sorted_classes) {
        register_class_info(info);
    }

    return registered_class_names;
}
```

#### 4.3 Script Reload/Hot Reload
**Problem**: Script is modified while scene is loaded

**Solution**: Add re-registration method
```cpp
// Call this when a script file changes
static void refresh_script_class(const String &p_script_path) {
    // Find which context owns this script
    for (auto &entry : registered_classes) {
        if (entry.value.script_path == p_script_path) {
            String context_id = entry.value.source_context;

            // Re-extract class info
            ClassInfo new_info = extract_class_info(p_script_path, context_id);

            // Update registration
            if (!new_info.class_name.is_empty()) {
                register_class_info(new_info);
            } else {
                // class_name was removed, unregister
                unregister_class(entry.key, context_id);
            }
            break;
        }
    }
}
```

---

### Phase 5: Testing Strategy

#### 5.1 Unit Tests
**File**: `modules/hmscript/tests/test_external_script_class_registry.h`

```cpp
TEST_CASE("[ExternalScriptClassRegistry] Register and unregister classes") {
    // Create test scripts in user://test/
    // Register them
    // Verify ScriptServer::has_global_class() returns true
    // Unregister context
    // Verify classes are removed
}

TEST_CASE("[ExternalScriptClassRegistry] Handle name collisions") {
    // Register a class from res://
    // Try to register same class name from user://
    // Verify warning is printed
    // Verify res:// class is preserved
}

TEST_CASE("[ExternalScriptClassRegistry] Dependency order") {
    // Create ScriptA extends ScriptB
    // Create ScriptB extends Node
    // Register in wrong order
    // Verify topological sort fixes it
}
```

#### 5.2 Integration Tests
**File**: `modules/hmscript/tests/test_sandbox_external_classes.gd`

```gdscript
# Test inheritance across external scripts
func test_external_class_inheritance():
    # Load sandbox from user://test_mods/inheritance_test/
    # Verify BaseClass is registered globally
    # Verify DerivedClass can extend BaseClass
    # Instantiate DerivedClass and verify it works
    # Unload sandbox
    # Verify classes are unregistered
```

---

## Alternative Approaches Considered

### Alternative 1: Automatic Registration on Script Load
**Approach**: Hook into GDScript compilation to auto-register any class_name

**Pros**:
- Automatic, no explicit registration needed
- Works for all script loading scenarios

**Cons**:
- No control over registration lifecycle
- Harder to clean up when scripts are unloaded
- May register classes that shouldn't be global
- **Rejected**: Too invasive, harder to manage

### Alternative 2: Extend Global Class Cache to Support Multiple Paths
**Approach**: Allow multiple cache files (res://.godot/..., user://.godot/...)

**Pros**:
- Consistent with editor behavior
- Persistent across restarts

**Cons**:
- Requires modifying ProjectSettings and ScriptServer significantly
- Cache persistence may not be desired for dynamic mods
- **Rejected**: Too complex for the use case

### Alternative 3: Virtual File System with Path Aliasing
**Approach**: Map user:// paths to res:// equivalents virtually

**Pros**:
- Transparent to existing systems

**Cons**:
- Requires VFS layer, very complex
- May break other systems expecting real paths
- **Rejected**: Over-engineered

---

## Implementation Checklist

### Core Implementation
- [ ] Create `external_script_class_registry.h`
- [ ] Create `external_script_class_registry.cpp`
- [ ] Implement `scan_and_register_directory()`
- [ ] Implement `extract_class_info()` using `get_global_class_name()`
- [ ] Implement `unregister_context()`
- [ ] Implement topological sort for dependency order
- [ ] Add collision detection and warnings

### Integration
- [ ] Modify `HMSandbox::load()` to call registration
- [ ] Modify `HMSandbox::unload()` to call unregistration
- [ ] Add `registered_class_names` field to `HMSandbox`
- [ ] Update `sandbox_runtime.h` with new includes

### Edge Cases
- [ ] Handle name collision with res:// classes
- [ ] Handle name collision between external classes
- [ ] Handle circular dependencies gracefully
- [ ] Handle script reload/hot reload (if needed)
- [ ] Handle invalid script paths
- [ ] Handle scripts without class_name

### Testing
- [ ] Write unit tests for registry operations
- [ ] Write integration tests for sandbox loading
- [ ] Test inheritance across external scripts
- [ ] Test name collision scenarios
- [ ] Test cleanup on unload
- [ ] Test with multiple sandboxes loaded simultaneously
- [ ] Test with deeply nested inheritance chains

### Documentation
- [ ] Add API documentation comments
- [ ] Update sandbox documentation with new behavior
- [ ] Add troubleshooting guide for class conflicts
- [ ] Document performance implications

### Performance Considerations
- [ ] Benchmark registration time for large mod directories
- [ ] Consider caching extracted class info
- [ ] Profile memory usage with many registered classes
- [ ] Add option to disable auto-registration if needed

---

## Migration Path

### For Existing Code
1. **No breaking changes**: Existing sandboxes will automatically register classes
2. **Optional opt-out**: Add flag to `HMSandbox` to disable auto-registration if needed
3. **Logging**: Add verbose logging (disabled by default) for debugging registration

### For Users
1. **Transparent**: External scripts will "just work" with class_name
2. **Clear errors**: If collision occurs, clear warning message indicates the issue
3. **Documentation**: Update user-facing docs to explain global class behavior

---

## Future Enhancements

### 1. Persistent External Class Cache
- Save external class info to user://.godot/external_class_cache.cfg
- Load on startup to avoid re-scanning
- Invalidate when mod directory changes

### 2. Namespace Support
- Prefix external classes with namespace (e.g., `mod1::BaseEnemy`)
- Avoid collisions between mods
- Requires syntax extension

### 3. Class Versioning
- Track class version in registration
- Warn when loading scripts that depend on different versions
- Support compatibility layers

### 4. Performance Optimization
- Parallel scanning of large mod directories
- Incremental updates instead of full re-scan
- Lazy registration on first access

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Name collisions between mods | High | Medium | Clear warnings, res:// takes precedence |
| Performance impact on large mods | Medium | Medium | Benchmark and optimize, consider lazy registration |
| Memory leaks from improper cleanup | Low | High | Thorough testing, RAII patterns |
| Breaking changes to existing code | Low | High | Careful integration, backward compatibility |
| Race conditions with threaded loading | Low | High | Mutex protection if needed |

---

## Success Criteria

1. ✅ External scripts with `class_name` can extend each other
2. ✅ No impact on existing res:// class registration
3. ✅ Clean cleanup when sandboxes are unloaded
4. ✅ Clear error messages for conflicts
5. ✅ Performance overhead < 100ms for typical mod directory
6. ✅ Zero memory leaks
7. ✅ All tests passing

---

## Summary

This design provides a robust solution for registering global classes from external scenes without modifying core Godot behavior. It integrates seamlessly with the existing HMSandbox system while maintaining flexibility for future enhancements.

**Key Benefits**:
- ✅ Solves the inheritance problem for external scripts
- ✅ Minimal changes to existing codebase
- ✅ Clear separation of concerns
- ✅ Extensible for future needs
- ✅ Proper lifecycle management

**Next Steps**:
1. Review and approve design
2. Implement Phase 1 (registry structure)
3. Implement Phase 2 (integration)
4. Implement Phase 3 (core logic)
5. Implement Phase 4 (edge cases)
6. Write tests
7. Performance profiling
8. Documentation
