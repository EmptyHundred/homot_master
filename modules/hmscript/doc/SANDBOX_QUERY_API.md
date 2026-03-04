# Sandbox Query API Documentation

This document describes the newly exposed sandbox query methods for GDScript and GDScriptInstance.

## Overview

Two methods have been exposed to GDScript to allow querying sandbox status at both the script level and instance level:
- `is_sandbox_enabled()` - Check if sandbox is enabled
- `get_sandbox_profile_id()` - Get the sandbox profile ID

## API Reference

### GDScript Class Methods

These methods can be called on a GDScript resource object:

```gdscript
var script = load("res://my_script.gd")
var is_sandboxed = script.is_sandbox_enabled()  # Returns: bool
var profile_id = script.get_sandbox_profile_id()  # Returns: String
```

**Methods:**
- `bool is_sandbox_enabled()` - Returns true if the script has sandbox enabled
- `String get_sandbox_profile_id()` - Returns the sandbox profile ID (e.g., "hm_default", "Sandbox_abc123")

### Object Instance Methods

These methods can be called on any Object to query its script instance's sandbox status:

```gdscript
extends Node

func _ready():
    # Query your own sandbox status
    var is_sandboxed = is_script_instance_sandbox_enabled()  # Returns: bool
    var profile_id = get_script_instance_sandbox_profile_id()  # Returns: String

    print("Running in sandbox: ", is_sandboxed)
    print("Sandbox profile: ", profile_id)
```

**Methods:**
- `bool is_script_instance_sandbox_enabled()` - Returns true if the instance's script has sandbox enabled
- `String get_script_instance_sandbox_profile_id()` - Returns the instance's sandbox profile ID

## Use Cases

### 1. Query Script Resource Sandbox Status

```gdscript
# Check if a script file has sandbox enabled
var hm_script = load("res://scripts/player.hm")
if hm_script.is_sandbox_enabled():
    print("This is a sandboxed HMScript with profile: ", hm_script.get_sandbox_profile_id())
```

### 2. Runtime Sandbox Detection

```gdscript
# A script checking its own sandbox status at runtime
func _ready():
    if is_script_instance_sandbox_enabled():
        print("I am running in a sandbox!")
        print("My sandbox profile: ", get_script_instance_sandbox_profile_id())
    else:
        print("I am running without sandbox restrictions")
```

### 3. Conditional Behavior Based on Sandbox

```gdscript
func try_file_access():
    if is_script_instance_sandbox_enabled():
        # We're sandboxed, use restricted file access
        print("Sandboxed mode: Using res:// or user:// paths only")
        return load_from_user_directory()
    else:
        # Not sandboxed, can use any path
        return load_from_any_path()
```

### 4. Multiple Sandbox Coordination

```gdscript
# Check which sandbox profile is running
func _ready():
    var profile = get_script_instance_sandbox_profile_id()
    match profile:
        "hm_default":
            print("Running in default HMScript sandbox")
        "Sandbox_abc123":
            print("Running in sandbox instance abc123")
        "":
            print("Not sandboxed")
```

## Implementation Details

### Architecture

The implementation follows this hierarchy:

```
┌─────────────────────────────────────┐
│ GDScript (script resource)          │
│ - is_sandbox_enabled()              │
│ - get_sandbox_profile_id()          │
└─────────────────────────────────────┘
            │
            │ script loaded & instantiated
            ↓
┌─────────────────────────────────────┐
│ GDScriptInstance                    │
│ (inherits ScriptInstance)           │
│ - bool sandbox_enabled              │
│ - String sandbox_profile_id         │
│ - is_sandbox_enabled() override     │
│ - get_sandbox_profile_id() override │
└─────────────────────────────────────┘
            │
            │ accessed via
            ↓
┌─────────────────────────────────────┐
│ Object                              │
│ - is_script_instance_sandbox_enabled()      │
│ - get_script_instance_sandbox_profile_id()  │
└─────────────────────────────────────┘
```

### File Changes

**Modified Files:**
1. `modules/gdscript/gdscript.cpp` - Added GDScript class method bindings
2. `modules/gdscript/gdscript.h` - Made methods virtual overrides
3. `core/object/script_instance.h` - Added virtual methods to ScriptInstance interface
4. `core/object/object.h` - Added forwarding method declarations
5. `core/object/object.cpp` - Added forwarding method implementations and bindings

### Sandbox Flags Flow

```
Script Loading (HMScript):
  ├─ ResourceFormatLoaderHMScript::load()
  ├─ gds->set_sandbox_enabled(true, "hm_default")
  └─ GDScript stores: sandbox_enabled=true, sandbox_profile_id="hm_default"

Instance Creation:
  ├─ GDScript::instance_create()
  ├─ Copy sandbox flags from script to instance
  └─ GDScriptInstance stores: sandbox_enabled=true, sandbox_profile_id="hm_default"

Query at Runtime:
  ├─ From GDScript: script.is_sandbox_enabled()
  │   └─ Returns GDScript.sandbox_enabled
  │
  └─ From Instance: is_script_instance_sandbox_enabled()
      └─ Object → script_instance → is_sandbox_enabled()
          └─ Returns GDScriptInstance.sandbox_enabled
```

## Examples

### Example 1: Debug Sandbox Information

```gdscript
func print_sandbox_info():
    print("=== Sandbox Information ===")

    # Script level
    var script = get_script()
    print("Script sandbox: ", script.is_sandbox_enabled())
    print("Script profile: ", script.get_sandbox_profile_id())

    # Instance level
    print("Instance sandbox: ", is_script_instance_sandbox_enabled())
    print("Instance profile: ", get_script_instance_sandbox_profile_id())
```

### Example 2: Sandbox-Aware Logger

```gdscript
class SandboxLogger:
    func log(message: String):
        var prefix = ""
        if is_script_instance_sandbox_enabled():
            var profile = get_script_instance_sandbox_profile_id()
            prefix = "[SANDBOX:%s] " % profile
        else:
            prefix = "[UNSANDBOXED] "

        print(prefix + message)
```

### Example 3: Validate Sandbox Configuration

```gdscript
func validate_sandbox_setup() -> bool:
    var script = get_script()

    # Check script configuration
    if not script.is_sandbox_enabled():
        push_error("Script should be sandboxed but isn't!")
        return false

    # Check instance inherited the configuration
    if not is_script_instance_sandbox_enabled():
        push_error("Instance didn't inherit sandbox flags!")
        return false

    # Verify profile IDs match
    var script_profile = script.get_sandbox_profile_id()
    var instance_profile = get_script_instance_sandbox_profile_id()
    if script_profile != instance_profile:
        push_warning("Profile ID mismatch: %s vs %s" % [script_profile, instance_profile])

    return true
```

## Notes

- **Script vs Instance**: The script object stores the initial sandbox configuration. When a script is instantiated, the sandbox flags are copied to the instance. Querying the script returns the original configuration, while querying the instance returns the runtime configuration.

- **Non-GDScript Scripts**: The methods are part of the ScriptInstance interface, so they return default values (false, empty string) for non-GDScript script instances.

- **HMScript**: HMScript files (.hm, .hmc) are loaded as GDScript with `sandbox_enabled=true` and `sandbox_profile_id="hm_default"` by default.

- **Performance**: These are simple accessor methods with minimal overhead. They can be called frequently without performance concerns.

## See Also

- `modules/hmscript/sandbox/` - Sandbox implementation
- `modules/gdscript/gdscript_vm.cpp` - VM-level sandbox enforcement
- `test_sandbox_query.gd` - Test script demonstrating the API
