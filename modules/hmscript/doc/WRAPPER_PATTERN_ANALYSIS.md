# Architecture Proposal: HMSandbox as Profile Wrapper

## Current Architecture (Two Copies)

```cpp
class HMSandbox {
    String profile_id;
    HMSandboxConfig config;         // Copy #1
    HMSandboxLimiter limiter;        // Copy #1
    HMSandboxErrorRegistry errors;   // Copy #1
};

GDScriptLanguage::sandbox_profiles["Sandbox_abc123"] = {
    config,   // Copy #2
    limiter,  // Copy #2
    errors    // Copy #2
};

// Problem: Need sync_to_gdscript_profile() to keep consistent
```

## Proposed Architecture (Wrapper Pattern)

```cpp
class HMSandbox {
    String profile_id;
    // No copies! Just the ID

    HMSandboxConfig& get_config() {
        return GDScriptLanguage::get_singleton()
            ->ensure_sandbox_profile(profile_id)->config;
    }
};

GDScriptLanguage::sandbox_profiles["Sandbox_abc123"] = {
    config,   // SINGLE SOURCE OF TRUTH
    limiter,  // SINGLE SOURCE OF TRUTH
    errors    // SINGLE SOURCE OF TRUTH
};

// No sync needed! ✅
```

## Comparison

| Aspect | Current (Two Copies) | Proposed (Wrapper) |
|--------|---------------------|-------------------|
| **Copies** | 2 per sandbox | 1 per sandbox ✅ |
| **Memory** | ~300 bytes × 2 = 600 bytes | ~300 bytes ✅ |
| **Synchronization** | Required (auto-sync) | None needed ✅ |
| **Consistency** | Must be maintained | Automatic ✅ |
| **Code complexity** | sync_to_gdscript_profile() | Simpler ✅ |
| **Access overhead** | Direct member access (fast) | Lookup on every call ⚠️ |
| **Module coupling** | Independent | Same as current ✅ |
| **Encapsulation** | HMSandbox owns state | HMSandbox is handle ⚠️ |
| **Profile deletion** | Safe (has own copy) | Crash risk ⚠️ |

## Implementation Details

### Before (Current)

```cpp
// Direct access to owned members
void HMSandbox::set_write_ops_per_frame(int p_count) {
    limiter.set_write_ops_per_frame(p_count);  // Direct member access
    sync_to_gdscript_profile();                 // Sync to GDScript
}

int HMSandbox::get_write_ops_this_frame() const {
    return limiter.get_write_ops_this_frame();  // Direct member access
}
```

### After (Wrapper)

```cpp
// Lookup on every access
void HMSandbox::set_write_ops_per_frame(int p_count) {
    get_limiter().set_write_ops_per_frame(p_count);  // Lookup + call
    // No sync needed! ✅
}

int HMSandbox::get_write_ops_this_frame() const {
    return get_limiter().get_write_ops_this_frame();  // Lookup + call
}

// Helper method
HMSandboxLimiter& HMSandbox::get_limiter() {
    GDScriptLanguage *lang = GDScriptLanguage::get_singleton();
    ERR_FAIL_NULL_V(lang, /* ??? what to return on error? */);

    GDScriptLanguage::SandboxProfile *profile = lang->ensure_sandbox_profile(profile_id);
    ERR_FAIL_NULL_V(profile, /* ??? */);

    return profile->limiter;
}
```

## Detailed Analysis

### ✅ Advantages

#### 1. Single Source of Truth
```cpp
// No sync needed - they're the same object!
sandbox->set_write_ops_per_frame(100);
// ↓
GDScriptLanguage::sandbox_profiles["Sandbox_abc123"].limiter.max_write_ops = 100
// ↓
VM checks this SAME limiter
// ✓ Always consistent!
```

#### 2. Less Memory
```
Current:
  HMSandbox: 300 bytes (config + limiter + errors)
  Profile:   300 bytes (config + limiter + errors)
  Total:     600 bytes per sandbox

Wrapper:
  HMSandbox: 0 bytes (just profile_id string, already exists)
  Profile:   300 bytes
  Total:     300 bytes per sandbox ✅

Savings: 50% memory reduction
```

#### 3. Simpler Code
```cpp
// Remove entire sync_to_gdscript_profile() method ✅
// Remove auto-sync calls in setters ✅
// Remove frame counter sync in reset_frame_counters() ✅

Lines of code removed: ~50
Complexity reduced: Significant ✅
```

#### 4. Automatic Consistency
```cpp
// User changes config
sandbox->get_config().block_class("FileAccess");

// VM immediately sees the change
// No sync delay, no consistency issues ✅
```

### ⚠️ Disadvantages

#### 1. Access Overhead

```cpp
// Every access requires lookup
int HMSandbox::get_write_ops_this_frame() const {
    GDScriptLanguage *lang = GDScriptLanguage::get_singleton();  // Singleton lookup
    GDScriptLanguage::SandboxProfile *profile =
        lang->ensure_sandbox_profile(profile_id);                 // HashMap lookup
    return profile->limiter.get_write_ops_this_frame();          // Member access
}
```

**Cost per access:**
- Singleton: ~5 cycles (cached pointer)
- HashMap lookup: ~20-30 cycles (hash + comparison)
- Total: ~30-40 cycles per access

**Is this a problem?**

Let's check usage frequency:

```cpp
// High frequency (called every frame)
sandbox->reset_frame_counters();  // 1 lookup/frame

// Low frequency (called rarely - setup time)
sandbox->set_write_ops_per_frame(100);  // ~10 lookups during setup
sandbox->get_config().block_class(...);  // ~50 lookups during setup

// Medium frequency (manual script invocation)
sandbox->call_script_function(...);  // Checks limiter a few times
```

**Verdict:** Overhead is acceptable. Most accesses are rare (setup) or infrequent (per-frame).

#### 2. Error Handling Complexity

```cpp
// Current (owned members)
HMSandboxConfig& HMSandbox::get_config() {
    return config;  // Always valid ✅
}

// Wrapper (lookup)
HMSandboxConfig& HMSandbox::get_config() {
    GDScriptLanguage *lang = GDScriptLanguage::get_singleton();
    ERR_FAIL_NULL_V(lang, ???);  // ❌ Can't return null reference!

    GDScriptLanguage::SandboxProfile *profile = lang->ensure_sandbox_profile(profile_id);
    ERR_FAIL_NULL_V(profile, ???);  // ❌ Can't return null reference!

    return profile->config;
}
```

**Solution: Use static dummy for error cases**

```cpp
class HMSandbox {
    static HMSandboxConfig dummy_config;
    static HMSandboxLimiter dummy_limiter;
    static HMSandboxErrorRegistry dummy_errors;

public:
    HMSandboxConfig& get_config() {
        GDScriptLanguage *lang = GDScriptLanguage::get_singleton();
        ERR_FAIL_NULL_V(lang, dummy_config);

        GDScriptLanguage::SandboxProfile *profile =
            lang->ensure_sandbox_profile(profile_id);
        ERR_FAIL_NULL_V(profile, dummy_config);

        return profile->config;
    }
};
```

**Problem with dummy:** Silent failures. User might configure dummy_config without realizing it.

**Alternative: Return pointer instead**

```cpp
HMSandboxConfig* HMSandbox::get_config() {
    GDScriptLanguage *lang = GDScriptLanguage::get_singleton();
    ERR_FAIL_NULL_V(lang, nullptr);

    GDScriptLanguage::SandboxProfile *profile =
        lang->ensure_sandbox_profile(profile_id);
    return profile ? &profile->config : nullptr;
}

// Usage changes:
// Before: sandbox->get_config().block_class(...)
// After:  sandbox->get_config()->block_class(...)  // Pointer instead
```

#### 3. Profile Deletion Risk

```cpp
// If profile is deleted while sandbox exists
GDScriptLanguage::get_singleton()->remove_sandbox_profile(sandbox->get_profile_id());

// Now sandbox's lookups fail
HMSandboxConfig *cfg = sandbox->get_config();  // Returns nullptr ❌

// Current design: HMSandbox has its own copy, still works ✅
```

**Mitigation:** Don't provide profile deletion API. As discussed earlier, profiles should live forever (or until all instances gone).

#### 4. Less Encapsulation

```cpp
// Current: HMSandbox is self-contained
HMSandbox *sandbox = create_sandbox();
// Sandbox owns its state, can be moved, copied, etc.

// Wrapper: HMSandbox is just a handle
HMSandbox *sandbox = create_sandbox();
// Sandbox is a "view" into GDScriptLanguage state
// State lives elsewhere, sandbox is not self-contained
```

**Impact:** Philosophical more than practical. The wrapper pattern is valid design.

## Performance Deep Dive

### Lookup Cost Breakdown

```cpp
HMSandboxLimiter& HMSandbox::get_limiter() {
    // 1. Singleton access: ~5 cycles (static member)
    GDScriptLanguage *lang = GDScriptLanguage::get_singleton();

    // 2. HashMap lookup: ~25 cycles
    //    - Hash string: ~15 cycles
    //    - Bucket lookup: ~5 cycles
    //    - Comparison: ~5 cycles
    GDScriptLanguage::SandboxProfile *profile =
        lang->sandbox_profiles.find(profile_id);

    return profile->limiter;
}
```

**Total: ~30 cycles per access**

### Caching Optimization

To reduce overhead, cache the profile pointer:

```cpp
class HMSandbox {
    String profile_id;
    mutable GDScriptLanguage::SandboxProfile *cached_profile = nullptr;

    GDScriptLanguage::SandboxProfile* get_profile() const {
        if (!cached_profile) {
            GDScriptLanguage *lang = GDScriptLanguage::get_singleton();
            ERR_FAIL_NULL_V(lang, nullptr);
            cached_profile = lang->ensure_sandbox_profile(profile_id);
        }
        return cached_profile;
    }

public:
    HMSandboxLimiter& get_limiter() {
        return get_profile()->limiter;  // Now just ~5 cycles!
    }
};
```

**With caching: ~5 cycles per access** (same as direct member access!)

**Risk:** Cache invalidation
- If profile moved in HashMap (rehashing)
- If profile deleted
- Need to handle carefully

## Migration Path

### Step 1: Change Accessor Methods

```cpp
// In sandbox_runtime.h
class HMSandbox {
private:
    // Keep these for now (backward compatibility)
    HMSandboxConfig config;
    HMSandboxLimiter limiter;
    HMSandboxErrorRegistry errors;

public:
    // Change to lookup-based accessors
    HMSandboxConfig& get_config() {
        GDScriptLanguage::SandboxProfile *profile = get_profile();
        return profile ? profile->config : config;  // Fallback
    }

private:
    GDScriptLanguage::SandboxProfile* get_profile() {
        // Implementation...
    }
};
```

### Step 2: Remove Sync Methods

```cpp
// Remove these:
// void sync_to_gdscript_profile();  ❌
// Auto-sync calls in setters ❌
```

### Step 3: Remove Member Variables

```cpp
// In sandbox_runtime.h
class HMSandbox {
private:
    // HMSandboxConfig config;  ❌ Remove
    // HMSandboxLimiter limiter;  ❌ Remove
    // HMSandboxErrorRegistry errors;  ❌ Remove
};
```

### Step 4: Add Error Handling

```cpp
HMSandboxConfig& HMSandbox::get_config() {
    static HMSandboxConfig dummy;
    GDScriptLanguage::SandboxProfile *profile = get_profile();
    ERR_FAIL_NULL_V(profile, dummy);
    return profile->config;
}
```

## Recommendation

### ✅ Use Wrapper Pattern IF:

1. **You prioritize simplicity** over micro-optimizations
2. **Memory matters** (saving 300 bytes per sandbox)
3. **You want to remove sync complexity**
4. **Profile deletion is not a concern** (profiles live forever)
5. **Lookup overhead is acceptable** (~30 cycles)

### ✅ Keep Current Design IF:

1. **You want maximum encapsulation** (sandbox owns state)
2. **You want zero access overhead** (direct member access)
3. **You want robustness** (sandbox works even if profile deleted)
4. **You're okay with sync complexity** (already implemented and working)

## My Recommendation

**Go with the Wrapper Pattern!** Here's why:

### Pros That Matter Most:
1. ✅ **Much simpler** - removes ~50 lines of sync code
2. ✅ **Guaranteed consistency** - impossible to get out of sync
3. ✅ **Less memory** - 50% reduction per sandbox
4. ✅ **Clearer semantics** - HMSandbox is clearly a "view" not an owner

### Cons That Don't Matter:
1. ⚠️ Lookup overhead - Only ~30 cycles, not in hot path
2. ⚠️ Less encapsulation - Wrapper pattern is still good design
3. ⚠️ Error handling - Solvable with static dummy
4. ⚠️ Profile deletion - We already decided not to delete profiles

### With Caching Optimization:
```cpp
mutable GDScriptLanguage::SandboxProfile *cached_profile = nullptr;
```

You get **best of both worlds**:
- ✅ Single source of truth
- ✅ Near-zero access overhead (cached)
- ✅ Simple implementation

## Implementation Recommendation

Use **cached wrapper pattern**:

```cpp
class HMSandbox : public Node {
    String profile_id;
    mutable GDScriptLanguage::SandboxProfile *cached_profile = nullptr;

    // Scene management (stays the same)
    Ref<PackedScene> packed_scene;
    PackedStringArray dependencies;
    // ...

private:
    GDScriptLanguage::SandboxProfile* ensure_profile() const {
        if (!cached_profile) {
            GDScriptLanguage *lang = GDScriptLanguage::get_singleton();
            ERR_FAIL_NULL_V(lang, nullptr);
            cached_profile = lang->ensure_sandbox_profile(profile_id);
        }
        return cached_profile;
    }

public:
    HMSandboxConfig& get_config() {
        static HMSandboxConfig dummy;
        GDScriptLanguage::SandboxProfile *profile = ensure_profile();
        ERR_FAIL_NULL_V(profile, dummy);
        return profile->config;
    }

    // Similar for get_limiter(), get_error_registry()
};
```

**Result:**
- Single source of truth ✅
- Minimal overhead (~5 cycles with cache) ✅
- Simple implementation ✅
- Automatic consistency ✅

This is **cleaner, simpler, and more maintainable** than the current two-copy design!
