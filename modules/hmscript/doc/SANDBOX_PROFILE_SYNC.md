# Sandbox Profile Synchronization

## Overview

This document explains how sandbox profile IDs are managed and synchronized between HMSandbox instances and GDScriptLanguage profiles.

## Problem Statement

When a sandbox is loaded:
1. **HMSandbox** generates a unique profile_id (e.g., `"Sandbox_abc123"`)
2. But all `.hm/.hmc` scripts loaded by `ResourceFormatLoaderHMScript` are initially marked with profile_id `"hm_default"`
3. When scripts execute, the VM looks up the profile by the script's profile_id
4. This causes a mismatch: scripts try to use `"hm_default"` profile instead of the sandbox's unique profile

## Solution

The solution involves two synchronization mechanisms:

### 1. Profile ID Update (Script Loading Phase)

**Location:** `modules/hmscript/sandbox/sandbox_runtime.cpp:HMSandbox::load()`

**When:** After PackedScene is loaded (which loads all .hm/.hmc dependencies) but before scene instantiation (before GDScriptInstances are created)

**What:**
```cpp
// Update all loaded .hm/.hmc scripts to use this sandbox's unique profile_id
for (int i = 0; i < deps.size(); i++) {
    const String &script_path = deps[i];

    // Get the already-loaded script from ResourceCache
    Ref<Resource> res = ResourceCache::get_ref(script_path);
    if (res.is_null()) continue;

    // Cast to GDScript
    Ref<GDScript> gds = res;
    if (gds.is_null()) continue;

    // Update the script's sandbox profile_id from "hm_default" to sandbox's unique ID
    gds->set_sandbox_enabled(true, profile_id);
}
```

**Result:** All `.hm/.hmc` scripts in the sandbox now have the same profile_id as their containing sandbox.

### 2. Config/Limiter Synchronization

**Location:** `modules/hmscript/sandbox/sandbox_runtime.cpp:HMSandbox::sync_to_gdscript_profile()`

**When:**
- Initially after sandbox creation
- Whenever limits are changed via `set_timeout_ms()`, `set_write_ops_per_frame()`, etc.

**What:**
```cpp
void HMSandbox::sync_to_gdscript_profile() {
    GDScriptLanguage *lang = GDScriptLanguage::get_singleton();
    GDScriptLanguage::SandboxProfile *profile = lang->ensure_sandbox_profile(profile_id);

    // Synchronize config
    profile->config = config;

    // Synchronize limiter settings
    profile->limiter.set_timeout_ms(limiter.get_timeout_ms());
    profile->limiter.set_memory_limit_mb(limiter.get_memory_limit_bytes() / (1024 * 1024));
    profile->limiter.set_write_ops_per_frame(limiter.get_max_write_ops_per_frame());
    profile->limiter.set_heavy_ops_per_frame(limiter.get_max_heavy_ops_per_frame());
}
```

**Result:** The GDScriptLanguage profile has the same configuration as the HMSandbox, ensuring VM-level checks use the same limits.

### 3. Frame Counter Synchronization

**Location:** `modules/hmscript/sandbox/sandbox_runtime.cpp:HMSandbox::reset_frame_counters()`

**When:** Every frame via `HMSandboxManager::frame_callback()`

**What:**
```cpp
void HMSandbox::reset_frame_counters() {
    limiter.reset_frame_counters();

    // Also reset the corresponding GDScriptLanguage profile's frame counters
    GDScriptLanguage::SandboxProfile *profile = lang->get_sandbox_profile(profile_id);
    if (profile) {
        profile->limiter.reset_frame_counters();
    }
}
```

**Result:** Both the HMSandbox limiter and the GDScriptLanguage profile limiter have their per-frame counters reset synchronously.

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│ HMSandbox::load()                                                   │
├─────────────────────────────────────────────────────────────────────┤
│ 1. Generate unique profile_id: "Sandbox_abc123"                    │
│ 2. Load PackedScene (loads all .hm/.hmc dependencies)              │
│    ├─ ResourceFormatLoaderHMScript marks them as:                  │
│    │  sandbox_enabled=true, sandbox_profile_id="hm_default"        │
│    │                                                                │
│ 3. Update all loaded scripts' profile_id                           │
│    ├─ For each .hm/.hmc in deps:                                   │
│    │  └─ gds->set_sandbox_enabled(true, "Sandbox_abc123")          │
│    │                                                                │
│ 4. Create HMSandbox instance                                       │
│    ├─ profile_id = "Sandbox_abc123"                                │
│    ├─ config (HMSandboxConfig)                                     │
│    └─ limiter (HMSandboxLimiter)                                   │
│                                                                     │
│ 5. Sync to GDScriptLanguage profile                                │
│    └─ sandbox->sync_to_gdscript_profile()                          │
│       ├─ Ensure profile exists in GDScriptLanguage                 │
│       ├─ Copy config from HMSandbox to profile                     │
│       └─ Copy limiter settings from HMSandbox to profile           │
│                                                                     │
│ 6. Instantiate scene (creates GDScriptInstances)                   │
│    └─ Each instance gets: sandbox_profile_id="Sandbox_abc123"     │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│ Runtime Execution                                                   │
├─────────────────────────────────────────────────────────────────────┤
│ GDScriptInstance executes code                                      │
│ ↓                                                                   │
│ VM calls native method (e.g., Node.new())                          │
│ ↓                                                                   │
│ _gdscript_sandbox_check_method_bind()                              │
│ ├─ Get profile_id from instance: "Sandbox_abc123"                  │
│ ├─ Lookup profile in GDScriptLanguage::sandbox_profiles            │
│ │  └─ Returns profile with synchronized config/limiter             │
│ ├─ profile->config.is_method_blocked(...) ← Security check         │
│ ├─ profile->limiter.check_api_rate_limit(WRITE) ← Quota check     │
│ └─ Allow or block the call                                         │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│ Frame Reset                                                         │
├─────────────────────────────────────────────────────────────────────┤
│ Every frame: HMSandboxManager::frame_callback()                    │
│ ↓                                                                   │
│ For each active sandbox:                                           │
│   sandbox->reset_frame_counters()                                  │
│   ├─ Reset HMSandbox's limiter counters                            │
│   └─ Reset GDScriptLanguage profile's limiter counters             │
└─────────────────────────────────────────────────────────────────────┘
```

## Two-Level Architecture

The system maintains sandbox state at two levels:

### Level 1: HMSandbox (High-Level)
- **Purpose:** Sandbox management, scene tree, dependency tracking
- **Contains:** config, limiter, errors, PackedScene, root node, dependencies
- **Used by:** HMSandbox::call_script_function(), sandbox management code

### Level 2: GDScriptLanguage Profile (VM-Level)
- **Purpose:** Per-opcode enforcement during script execution
- **Contains:** config, limiter, errors (synchronized from HMSandbox)
- **Used by:** GDScript VM opcodes (OPCODE_CALL_METHOD_BIND, etc.)

**Why two levels?**
- HMSandbox provides high-level sandbox management and API
- GDScriptLanguage profiles provide low-level VM enforcement
- They must stay synchronized for consistent behavior

## Configuration Flow

```
User configures sandbox:
  sandbox->set_write_ops_per_frame(1000)
  ↓
  limiter.set_write_ops_per_frame(1000)  ← Updates HMSandbox limiter
  ↓
  sync_to_gdscript_profile()              ← Automatically syncs
  ↓
  profile->limiter.set_write_ops_per_frame(1000)  ← Updates VM profile

Result: Both limiters now have the same configuration
```

## File Modifications

### Core Changes
1. **modules/hmscript/sandbox/sandbox_runtime.h**
   - Added: `void sync_to_gdscript_profile();`

2. **modules/hmscript/sandbox/sandbox_runtime.cpp**
   - Added includes: `core/io/resource.h`, `modules/gdscript/gdscript.h`
   - Implemented profile ID update in `HMSandbox::load()`
   - Implemented `sync_to_gdscript_profile()`
   - Modified setters to auto-sync: `set_timeout_ms()`, `set_write_ops_per_frame()`, etc.
   - Modified `reset_frame_counters()` to also reset GDScriptLanguage profile

## Testing

See [test_sandbox_profile_update.gd](test_sandbox_profile_update.gd) for test script.

### Manual Testing Steps

1. **Create test sandbox:**
   ```
   res://test_sandbox/
     ├── scene.tscn
     ├── script1.hm
     └── script2.hm
   ```

2. **Load sandbox:**
   ```gdscript
   var sandbox = HMSandbox.load("res://test_sandbox", "scene.tscn")
   print("Sandbox profile ID: ", sandbox.get_profile_id())  # e.g., "Sandbox_abc123"
   ```

3. **Verify script profile IDs:**
   ```gdscript
   var script1 = load("res://test_sandbox/script1.hm")
   print("Script1 profile ID: ", script1.get_sandbox_profile_id())  # Should match sandbox
   ```

4. **Verify synchronization:**
   ```gdscript
   sandbox.set_write_ops_per_frame(500)
   # Both sandbox limiter and GDScriptLanguage profile now have limit=500
   ```

5. **Test multiple sandboxes:**
   ```gdscript
   var sandbox1 = HMSandbox.load("res://level1", "scene.tscn")  # "Sandbox_111"
   var sandbox2 = HMSandbox.load("res://level2", "scene.tscn")  # "Sandbox_222"

   # Each sandbox's scripts have different profile IDs
   # Each operates independently with separate quotas
   ```

## Benefits

1. **Correct Profile Lookup:** Scripts use the correct sandbox profile during execution
2. **Independent Sandboxes:** Multiple sandboxes with unique profiles work independently
3. **Automatic Synchronization:** Config changes automatically sync to VM-level enforcement
4. **Consistent Enforcement:** High-level and VM-level checks use the same limits
5. **Per-Sandbox Limits:** Each sandbox can have different timeout/memory/API quotas

## See Also

- [SANDBOX_QUERY_API.md](SANDBOX_QUERY_API.md) - Query API for sandbox status
- `modules/hmscript/design.md` - Overall sandbox design
- `modules/hmscript/sandbox/` - Sandbox implementation
