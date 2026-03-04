# Architecture Comparison: Sync vs Direct Access

## Current Design: GDScriptLanguage Profiles + Sync

```cpp
VM → GDScriptLanguage::sandbox_profiles[profile_id] → SandboxProfile (Copy #2)
HMSandboxManager → Vector<HMSandbox*> → HMSandbox (Copy #1)
                                         ↓
                            sync_to_gdscript_profile()
```

## Proposed Design: HMSandboxManager HashMap

```cpp
VM → HMSandboxManager::active_sandboxes[profile_id] → HMSandbox (Single Copy)
```

## Detailed Comparison

| Aspect | Current (Sync) | Proposed (HashMap) |
|--------|---------------|-------------------|
| **Copies** | 2 per sandbox | 1 per sandbox ✅ |
| **Memory** | ~100 bytes extra per sandbox | Lower ✅ |
| **Synchronization** | Required | None needed ✅ |
| **Code complexity** | sync_to_gdscript_profile() | Simpler ✅ |
| **VM lookup** | O(1) HashMap | O(1) HashMap (same) |
| **Module coupling** | GDScript independent ✅ | GDScript depends on HMScript ❌ |
| **Extensibility** | Any module can use profiles ✅ | Tied to HMScript ❌ |
| **Lifecycle safety** | Profile survives HMSandbox deletion ✅ | Dangling pointer risk ❌ |
| **Conditional compilation** | Works with HMScript disabled ✅ | Requires HMScript ❌ |

## Key Differences Explained

### 1. Module Coupling ⚠️ CRITICAL ISSUE

#### Current Design
```cpp
// GDScript VM (core module)
#include "gdscript.h"  // ✅ Same module

_gdscript_sandbox_check_method_bind() {
    GDScriptLanguage *lang = GDScriptLanguage::get_singleton();  // ✅ Always available
    SandboxProfile *profile = lang->get_sandbox_profile(profile_id);
}
```

#### Proposed Design
```cpp
// GDScript VM (core module)
#include "modules/hmscript/sandbox/sandbox_manager.h"  // ❌ Cross-module dependency!

_gdscript_sandbox_check_method_bind() {
    HMSandboxManager *mgr = HMSandboxManager::get_singleton();  // ❌ May not exist!
    HMSandbox *sandbox = mgr->get_sandbox_by_id(profile_id);
}
```

**Problem:** GDScript is a **core module**, HMScript is an **optional module**.
- If HMScript is disabled in build, GDScript VM breaks
- Creates circular dependency (GDScript ← HMScript, but HMScript already uses GDScript)

### 2. Extensibility

#### Current Design: Generic Sandbox System
```cpp
// ANY module can use GDScript sandbox profiles

// Example 1: HMScript (current)
gds->set_sandbox_enabled(true, "Sandbox_abc123");

// Example 2: Future - Editor sandbox for untrusted plugins
gds->set_sandbox_enabled(true, "EditorPlugin_xyz");

// Example 3: Future - Multiplayer sandbox for client scripts
gds->set_sandbox_enabled(true, "ClientScript_player5");

// VM works with ALL of them!
GDScriptLanguage::SandboxProfile *profile = lang->get_sandbox_profile(profile_id);
```

#### Proposed Design: HMScript-Specific
```cpp
// ONLY HMSandbox instances can be sandboxed

HMSandbox *sandbox = mgr->get_sandbox_by_id(profile_id);
// ❌ What if profile_id is "EditorPlugin_xyz"? No HMSandbox exists!
```

**Problem:** Locks GDScript sandbox system to HMScript only.

### 3. Lifecycle Safety

#### Scenario: Sandbox deleted while script still running

```gdscript
# Script holds reference to instance
var script_instance = sandbox.get_root_node()

# Main code deletes sandbox
sandbox.queue_free()  # HMSandbox deleted!

# Later, script continues executing
script_instance.do_something()  # VM needs to check limits!
```

#### Current Design ✅
```cpp
// HMSandbox deleted
sandbox->~HMSandbox()  // Destructs
delete sandbox;  // Memory freed

// But GDScriptLanguage profile still exists!
GDScriptLanguage::sandbox_profiles["Sandbox_abc123"]  // ✅ Still valid

// VM can still enforce
profile->limiter.check_api_rate_limit(...)  // ✅ Works fine
```

#### Proposed Design ❌
```cpp
// HMSandbox deleted
delete sandbox;  // Memory freed

// HashMap still has pointer!
HMSandboxManager::active_sandboxes["Sandbox_abc123"] = 0xDEADBEEF  // ❌ Dangling!

// VM tries to access
HMSandbox *sandbox = mgr->get_sandbox_by_id(profile_id);
sandbox->get_limiter().check_api_rate_limit(...)  // ❌ CRASH!
```

**Solution:** Would need to:
1. Remove from HashMap on deletion (doable)
2. Handle null case in VM (adds overhead every check)
3. Keep profile alive even after HMSandbox deletion (back to 2 copies!)

### 4. Conditional Compilation

#### Current Design
```cpp
// build config: modules_enabled = ["gdscript"]  (HMScript disabled)

// GDScript still has sandbox support
GDScriptLanguage::sandbox_profiles;  // ✅ Compiles

// HMScript code simply not compiled
// No broken dependencies
```

#### Proposed Design
```cpp
// build config: modules_enabled = ["gdscript"]  (HMScript disabled)

// GDScript VM tries to include HMScript
#include "modules/hmscript/sandbox/sandbox_manager.h"  // ❌ File not found!

// Solution: Add #ifdef everywhere
#ifdef MODULE_HMSCRIPT_ENABLED
    HMSandboxManager *mgr = HMSandboxManager::get_singleton();
    if (mgr) {
        HMSandbox *sandbox = mgr->get_sandbox_by_id(profile_id);
        if (sandbox) {
            sandbox->get_limiter().check_api_rate_limit(...);
        }
    }
#endif
```

**Problem:** Pollutes GDScript VM code with conditional compilation.

## Hybrid Solution: Registration Pattern

We could use a **registration pattern** to avoid direct dependency:

```cpp
// In GDScript (core module)
class GDScriptLanguage {
    // Abstract interface
    class ISandboxProvider {
        virtual HMSandboxConfig* get_config(const String &p_id) = 0;
        virtual HMSandboxLimiter* get_limiter(const String &p_id) = 0;
    };

    ISandboxProvider *external_provider = nullptr;
    HashMap<String, SandboxProfile> sandbox_profiles;  // Fallback

public:
    void register_sandbox_provider(ISandboxProvider *p_provider);
};

// In HMScript module
class HMSandboxProvider : public GDScriptLanguage::ISandboxProvider {
    HashMap<String, HMSandbox*> sandboxes;

    virtual HMSandboxConfig* get_config(const String &p_id) override {
        HMSandbox *sb = sandboxes.get(p_id);
        return sb ? &sb->get_config() : nullptr;
    }
};

// VM code
_gdscript_sandbox_check_method_bind() {
    GDScriptLanguage *lang = GDScriptLanguage::get_singleton();

    // Try external provider first
    if (lang->external_provider) {
        HMSandboxConfig *cfg = lang->external_provider->get_config(profile_id);
        if (cfg) {
            cfg->is_method_blocked(...);
            return;
        }
    }

    // Fallback to internal profiles
    SandboxProfile *profile = lang->get_sandbox_profile(profile_id);
    profile->config.is_method_blocked(...);
}
```

**Pros of Hybrid:**
- ✅ No sync needed if using external provider
- ✅ GDScript still independent (uses interface)
- ✅ Backward compatible (fallback to profiles)
- ✅ Extensible (any provider can register)

**Cons of Hybrid:**
- ❌ More complex (interface + registration)
- ❌ Indirection overhead (virtual call)
- ❌ Still needs null checks
- ❌ Lifecycle issues remain

## Performance Analysis

### Current: Sync Overhead
```
Configuration change (rare):
  set_write_ops_per_frame(100)
    └─ sync_to_gdscript_profile()  // ~10 struct member copies

Cost: ~50 CPU cycles, happens rarely (setup time)
```

### Current: Runtime Overhead
```
Every VM native call:
  HashMap<String, SandboxProfile>::find(profile_id)
    └─ Hash computation + lookup

Cost: ~20-30 CPU cycles per call
```

### Proposed: No Sync
```
Configuration change: None! ✅
```

### Proposed: Runtime Overhead
```
Every VM native call:
  HashMap<String, HMSandbox*>::find(profile_id)
    └─ Hash computation + lookup
    └─ Pointer dereference to get_config()

Cost: ~25-35 CPU cycles per call (slightly slower due to extra indirection)
```

**Verdict:** Nearly identical runtime performance. Sync cost is negligible (only on setup).

## Recommendation

### ✅ Keep Current Design IF:
- You want GDScript sandbox to be **generic** (not HMScript-specific)
- You plan to use sandbox profiles for **other purposes** (editor, multiplayer, mods)
- You want to keep **modules independent**
- You want **build flexibility** (GDScript without HMScript)
- You don't mind small sync overhead

### ✅ Switch to HashMap Design IF:
- HMScript is **always enabled** in your builds
- You **don't need** generic sandbox system
- You want **simpler mental model** (one source of truth)
- You can solve **lifecycle issues** (null checks, cleanup)
- You accept **tight coupling** between modules

## My Opinion

**Keep the current design.** Here's why:

1. **Architectural Purity** 🏗️
   - GDScript sandbox system is properly layered
   - No circular dependencies
   - Clean module boundaries

2. **Future-Proof** 🔮
   - If Godot ever wants sandbox support in core GDScript (not just HMScript)
   - If other modules want to sandbox GDScript (multiplayer, editor, mods)
   - System is already designed for this

3. **Sync Cost is Minimal** 💰
   - Only happens during setup
   - ~10 struct copies once per sandbox creation
   - ~10 struct copies on rare config changes
   - Not in hot path (VM execution)

4. **Safety** 🛡️
   - Profile survives sandbox deletion
   - No dangling pointer risks
   - No null checks in hot path

5. **Proven Pattern** ✅
   - This is how Godot separates concerns
   - Similar to how ResourceLoader is extensible
   - Matches Godot's architecture philosophy

## If You Still Want to Change

If you strongly prefer HashMap approach, I recommend:

1. **Add HashMap lookup in HMSandboxManager** ✅
   ```cpp
   HashMap<String, HMSandbox*> sandboxes_by_id;
   ```

2. **Keep GDScriptLanguage profiles as fallback** ✅
   ```cpp
   // VM tries manager first, falls back to profiles
   ```

3. **Use registration pattern** ✅
   ```cpp
   GDScriptLanguage::register_sandbox_provider(&hm_sandbox_manager);
   ```

4. **Careful lifecycle management** ⚠️
   ```cpp
   HMSandbox::~HMSandbox() {
       hm_sandbox_manager->unregister_sandbox(profile_id);
   }
   ```

This gives you single-copy benefits while maintaining safety and extensibility.

## Conclusion

The current design has **intentional architectural benefits** that outweigh the small sync overhead. However, if you have specific requirements that make tight coupling acceptable, the HashMap approach is feasible with careful implementation.

**My recommendation:** Keep current design, it's well-architected for Godot's module system.
