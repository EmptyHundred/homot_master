# Why Synchronization is Needed

## The Problem: Two Copies

For each sandbox with a unique ID like `"Sandbox_abc123"`, there are **2 copies** of the configuration:

### Copy #1: HMSandbox Instance
```cpp
HMSandbox *sandbox = memnew(HMSandbox);
sandbox->profile_id = "Sandbox_abc123";
sandbox->config;     // ← Copy 1
sandbox->limiter;    // ← Copy 1
sandbox->errors;     // ← Copy 1
```

**Used by:** High-level API, manual checks, sandbox management

### Copy #2: GDScriptLanguage Profile
```cpp
GDScriptLanguage::sandbox_profiles["Sandbox_abc123"] = {
    .config = ...,   // ← Copy 2
    .limiter = ...,  // ← Copy 2
    .errors = ...,   // ← Copy 2
};
```

**Used by:** GDScript VM during bytecode execution

## Why Two Copies Exist

### Reason: Architectural Separation

The VM layer is **decoupled** from the HMSandbox layer:

```
┌────────────────────────────────────────────────────────────────┐
│ Layer 1: HMSandbox (High-Level)                               │
│ - Sandbox management API                                       │
│ - Scene tree management                                        │
│ - Dependency tracking                                          │
│ - Manual script invocation (call_script_function)             │
│ - Has direct access to HMSandbox instance                     │
└────────────────────────────────────────────────────────────────┘
                              ↕ No direct connection
┌────────────────────────────────────────────────────────────────┐
│ Layer 2: GDScript VM (Low-Level)                              │
│ - Bytecode execution                                           │
│ - Every OPCODE_CALL_METHOD_BIND instruction                   │
│ - Automatic enforcement during script execution                │
│ - Only knows profile_id string, not HMSandbox instance        │
│ - Looks up profile via GDScriptLanguage::sandbox_profiles     │
└────────────────────────────────────────────────────────────────┘
```

### Why VM Can't Access HMSandbox Directly

1. **VM doesn't know about HMSandbox instances**
   - VM only has: `GDScriptInstance *p_instance`
   - Instance only has: `String sandbox_profile_id`
   - VM cannot traverse from profile_id to HMSandbox object

2. **No global HMSandbox registry by ID**
   - `HMSandboxManager` has a `Vector<HMSandbox*>` but no ID-based lookup
   - Searching the vector every VM instruction would be too slow

3. **Architectural layering**
   - GDScript VM should not depend on HMScript module structures
   - Clean separation: VM uses GDScriptLanguage, not HMSandbox

## Execution Flow Comparison

### High-Level API Call (Uses Copy #1)
```cpp
// User explicitly calls sandbox method
HMSandbox *sandbox = HMSandbox::load("res://level", "scene.tscn");
String error;
Variant result = sandbox->call_script_function(script, owner, "main", args, error);
                         ↓
         sandbox->limiter.is_timeout_exceeded()  ← Uses Copy #1
         sandbox->config.is_method_blocked(...)   ← Uses Copy #1
```

### VM Automatic Enforcement (Uses Copy #2)
```gdscript
# Inside sandboxed .hm script
func main():
    var node = Node.new()  # ← VM intercepts this
```

```cpp
// VM execution path
GDScriptVM::execute()
  → OPCODE_CALL_METHOD_BIND
  → _gdscript_sandbox_check_method_bind(p_instance, ...)
     ├─ profile_id = p_instance->get_sandbox_profile_id()  // "Sandbox_abc123"
     ├─ profile = GDScriptLanguage::get_singleton()->get_sandbox_profile(profile_id)
     │                                                      ↓
     │                          Looks up Copy #2 from global HashMap
     │
     ├─ profile->config.is_method_blocked(...)  ← Uses Copy #2
     └─ profile->limiter.check_api_rate_limit(...)  ← Uses Copy #2
```

## What Needs Synchronization

### Config Synchronization
```cpp
// User configures sandbox
sandbox->get_config().block_class("FileAccess");

// Without sync:
// - Copy #1 (HMSandbox) has FileAccess blocked ✓
// - Copy #2 (VM profile) does NOT have FileAccess blocked ✗
// - VM would allow FileAccess calls! ⚠️

// With sync:
sandbox->sync_to_gdscript_profile();
// - Copy #1 blocks FileAccess ✓
// - Copy #2 blocks FileAccess ✓
// - Consistent enforcement ✓
```

### Limiter Synchronization
```cpp
// User sets limits
sandbox->set_write_ops_per_frame(100);

// Without sync:
// - Copy #1 limit: 100 ✓
// - Copy #2 limit: 500 (default) ✗
// - VM allows 500 operations instead of 100! ⚠️

// With auto-sync (implemented):
void HMSandbox::set_write_ops_per_frame(int p_count) {
    limiter.set_write_ops_per_frame(p_count);
    sync_to_gdscript_profile();  // ← Automatic sync
}
// - Copy #1 limit: 100 ✓
// - Copy #2 limit: 100 ✓
// - Consistent enforcement ✓
```

### Frame Counter Synchronization
```cpp
// Every frame
HMSandboxManager::frame_callback()
  → sandbox->reset_frame_counters()
      ├─ sandbox->limiter.reset_frame_counters()  // Reset Copy #1
      └─ profile->limiter.reset_frame_counters()  // Reset Copy #2

// Both copies reset synchronously every frame ✓
```

## Alternative Architectures (Not Used)

### ❌ Alternative 1: Single Copy in HMSandbox
```cpp
// VM would need to look up HMSandbox instance
_gdscript_sandbox_check_method_bind() {
    HMSandbox *sandbox = find_sandbox_by_id(profile_id);  // ⚠️ Expensive!
    sandbox->limiter.check_api_rate_limit(...);
}
```

**Problems:**
- Need global HMSandbox registry with O(1) lookup
- HMSandbox instances might be deleted while scripts still running
- Tight coupling between VM and HMScript module
- Performance: hash lookup on every VM instruction

### ❌ Alternative 2: Single Copy in GDScriptLanguage
```cpp
// HMSandbox would just be a thin wrapper
class HMSandbox {
    String profile_id;
    // Everything delegates to GDScriptLanguage::sandbox_profiles[profile_id]
};
```

**Problems:**
- HMSandbox loses ability to have its own state
- Can't track sandbox-specific metadata (scene, dependencies)
- No encapsulation of sandbox management logic
- GDScriptLanguage becomes bloated with sandbox management

### ✅ Current Design: Two Copies with Sync
```cpp
// Clean separation + automatic synchronization
HMSandbox: High-level management + API
GDScriptLanguage::SandboxProfile: VM enforcement

sync_to_gdscript_profile() keeps them consistent
```

**Benefits:**
- Clean architectural separation
- VM performance: direct HashMap lookup
- HMSandbox encapsulates high-level logic
- Automatic sync prevents inconsistency

## Synchronization Points

1. **Initial Creation** (sandbox_runtime.cpp:401)
   ```cpp
   sandbox->sync_to_gdscript_profile();
   ```

2. **Config Changes** (auto-sync in setters)
   ```cpp
   void HMSandbox::set_write_ops_per_frame(int p_count) {
       limiter.set_write_ops_per_frame(p_count);
       sync_to_gdscript_profile();  // ← Automatic
   }
   ```

3. **Frame Resets** (sandbox_runtime.cpp:154-167)
   ```cpp
   void HMSandbox::reset_frame_counters() {
       limiter.reset_frame_counters();  // Copy #1
       profile->limiter.reset_frame_counters();  // Copy #2
   }
   ```

## Performance Impact

### Sync Cost
- **Operation:** Copying struct members (config, limiter settings)
- **Frequency:** Only on configuration changes (rare)
- **Cost:** Negligible (~10 struct member copies)

### Lookup Cost (Why VM Uses Copy #2)
- **Operation:** `HashMap<String, SandboxProfile>::find(profile_id)`
- **Frequency:** Every native method call in sandboxed scripts
- **Cost:** O(1) hash lookup - very fast
- **Alternative (using Copy #1):** O(n) linear search through all sandboxes - slow!

## Summary

| Aspect | Copy #1 (HMSandbox) | Copy #2 (GDScriptLanguage) |
|--------|---------------------|----------------------------|
| **Purpose** | High-level management | VM enforcement |
| **Access** | Direct via instance pointer | HashMap lookup by ID |
| **Used by** | Sandbox API, manual checks | Every VM instruction |
| **Performance** | Direct member access | O(1) hash lookup |
| **Coupling** | Knows about scenes, nodes | Only knows scripts |

**Why sync?** Because they serve different purposes at different layers, but must have **consistent configuration** for correct behavior.

**Cost?** Minimal - sync only happens on configuration changes (rare), not during execution (frequent).

**Benefit?** Clean architecture + fast VM enforcement + correct behavior.
