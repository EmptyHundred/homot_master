# HMScript GDScript API Documentation

This document describes the GDScript APIs provided by the HMScript sandbox system. These APIs allow you to load, manage, and interact with sandboxed scripts in Godot.

## Table of Contents

- [Overview](#overview)
- [HMSandbox Class](#hmsandbox-class)
- [HMSandboxManager Class](#hmsandboxmanager-class)
- [Usage Examples](#usage-examples)

---

## Overview

The HMScript sandbox system provides two main classes for GDScript:

- **HMSandbox**: Represents a single sandboxed environment with its own scripts, scene, and resource limits
- **HMSandboxManager**: Global manager for creating and managing multiple sandbox instances

All sandboxed scripts run with isolated class registries and configurable resource limits (timeout, memory, operation counts).

---

## HMSandbox Class

`HMSandbox` extends `Node` and represents a single sandbox instance with its own isolated environment.

### Static Methods

#### `load(directory: String, tscn_filename: String) -> HMSandbox`

Loads a sandboxed scene from the specified directory.

**Parameters:**
- `directory` (String): The directory path containing the scene and its scripts
- `tscn_filename` (String): The name of the `.tscn` scene file to load

**Returns:** A configured `HMSandbox` instance with a unique profile ID, or `null` on failure

**Description:**
This method performs the complete sandbox loading process:
1. Generates a unique sandbox ID (e.g., "Sandbox_a1b2c3d4")
2. Registers the sandbox in the global manager
3. Pre-scans all `.hm` and `.hmc` scripts to register class names
4. Loads and compiles all dependency scripts
5. Configures scripts with the sandbox profile ID
6. Loads and instantiates the PackedScene
7. Sets up resource limiters and error tracking

**Example:**
```gdscript
var sandbox = HMSandbox.load("res://mods/my_mod", "main.tscn")
if sandbox:
    add_child(sandbox)
    print("Sandbox loaded with ID: ", sandbox.get_profile_id())
```

#### `collect_dependencies(dir_path: String) -> PackedStringArray`

Recursively collects all `.hm` and `.hmc` script files from a directory.

**Parameters:**
- `dir_path` (String): The directory path to scan

**Returns:** Array of absolute script paths found in the directory tree

**Example:**
```gdscript
var scripts = HMSandbox.collect_dependencies("res://mods/my_mod")
print("Found ", scripts.size(), " scripts")
for script_path in scripts:
    print("  - ", script_path)
```

---

### Instance Methods

#### `get_profile_id() -> String`

Returns the unique identifier for this sandbox instance.

**Returns:** String like "Sandbox_a1b2c3d4"

**Example:**
```gdscript
var sandbox_id = sandbox.get_profile_id()
print("Sandbox ID: ", sandbox_id)
```

---

#### `get_packed_scene() -> PackedScene`

Returns the PackedScene resource loaded by this sandbox.

**Returns:** The PackedScene resource, or null if not loaded

**Example:**
```gdscript
var scene = sandbox.get_packed_scene()
if scene:
    print("Scene state: ", scene.get_state())
```

---

#### `get_root_node() -> Node`

Returns the root node instantiated from the sandbox's scene.

**Returns:** The root Node, or null if no scene is instantiated

**Example:**
```gdscript
var root = sandbox.get_root_node()
if root:
    print("Root node: ", root.name)
    print("Root type: ", root.get_class())
```

---

#### `get_load_directory() -> String`

Returns the directory path from which this sandbox was loaded.

**Returns:** The directory path string

**Example:**
```gdscript
var dir = sandbox.get_load_directory()
print("Loaded from: ", dir)
```

---

#### `get_scene_filename() -> String`

Returns the scene filename (e.g., "main.tscn") loaded by this sandbox.

**Returns:** The scene filename string

**Example:**
```gdscript
var filename = sandbox.get_scene_filename()
print("Scene file: ", filename)
```

---

### Resource Limit Methods

#### `set_timeout_ms(ms: int)`

Sets the maximum execution time allowed for script calls.

**Parameters:**
- `ms` (int): Timeout in milliseconds

**Example:**
```gdscript
sandbox.set_timeout_ms(1000)  # 1 second timeout
```

---

#### `set_memory_limit_mb(mb: int)`

Sets the maximum memory usage allowed for this sandbox.

**Parameters:**
- `mb` (int): Memory limit in megabytes

**Example:**
```gdscript
sandbox.set_memory_limit_mb(100)  # 100 MB limit
```

---

#### `set_write_ops_per_frame(count: int)`

Sets the maximum number of write operations allowed per frame.

**Parameters:**
- `count` (int): Maximum write operations per frame

**Example:**
```gdscript
sandbox.set_write_ops_per_frame(50)
```

---

#### `set_heavy_ops_per_frame(count: int)`

Sets the maximum number of heavy operations allowed per frame.

**Parameters:**
- `count` (int): Maximum heavy operations per frame

**Example:**
```gdscript
sandbox.set_heavy_ops_per_frame(10)
```

---

#### `reset_frame_counters()`

Resets the per-frame operation counters. Should be called once per frame.

**Example:**
```gdscript
func _process(_delta):
    sandbox.reset_frame_counters()
```

---

### Error Handling Methods

#### `get_last_error() -> String`

Returns the most recent error message from the sandbox.

**Returns:** Error message string, or empty string if no errors

**Example:**
```gdscript
var error = sandbox.get_last_error()
if not error.is_empty():
    print("Last error: ", error)
```

---

#### `get_all_errors() -> Array`

Returns all errors recorded by the sandbox.

**Returns:** Array of error dictionaries with fields like `type`, `message`, `file`, `line`, etc.

**Example:**
```gdscript
var errors = sandbox.get_all_errors()
for error in errors:
    print("Error [%s]: %s" % [error.type, error.message])
    if error.has("file"):
        print("  at %s:%d" % [error.file, error.line])
```

---

#### `get_error_report_markdown() -> String`

Returns a formatted markdown report of all sandbox errors.

**Returns:** Markdown-formatted error report string

**Example:**
```gdscript
var report = sandbox.get_error_report_markdown()
print(report)
# Or save to file for debugging:
var file = FileAccess.open("user://sandbox_errors.md", FileAccess.WRITE)
file.store_string(report)
file.close()
```

---

### Dependency Methods

#### `get_dependencies() -> PackedStringArray`

Returns all script paths registered as dependencies for this sandbox.

**Returns:** Array of script file paths

**Example:**
```gdscript
var deps = sandbox.get_dependencies()
print("Dependencies:")
for dep in deps:
    print("  - ", dep)
```

---

#### `get_local_classes() -> Dictionary`

Returns all classes registered in the sandbox's local class registry.

**Returns:** Dictionary mapping class names to class information

**Example:**
```gdscript
var classes = sandbox.get_local_classes()
for class_name in classes:
    print("Class: ", class_name)
    var info = classes[class_name]
    print("  Script: ", info.script_path)
    print("  Base: ", info.base_type)
```

---

#### `has_script_path(path: String) -> bool`

Checks if a script path is registered in this sandbox.

**Parameters:**
- `path` (String): Script file path to check

**Returns:** `true` if the path is registered, `false` otherwise

**Example:**
```gdscript
if sandbox.has_script_path("res://mods/my_mod/player.hm"):
    print("Player script is registered")
```

---

#### `get_script_path_for_class(class_name: String) -> String`

Returns the script file path for a registered class name.

**Parameters:**
- `class_name` (String): The class name to look up

**Returns:** Script file path, or empty string if not found

**Example:**
```gdscript
var path = sandbox.get_script_path_for_class("Player")
if not path.is_empty():
    print("Player class defined in: ", path)
```

---

#### `unload()`

Unloads the sandbox, clearing all resources and caches.

**Description:**
- Clears the local class registry
- Removes the root node and all children
- Clears GDScript cache for all dependencies
- Releases the PackedScene reference

**Example:**
```gdscript
sandbox.unload()
sandbox.queue_free()
```

---

## HMSandboxManager Class

`HMSandboxManager` extends `Object` and provides global management of multiple sandbox instances.

### Instance Methods

#### `load_sandbox(directory: String, tscn_filename: String) -> HMSandbox`

Convenience wrapper for `HMSandbox.load()`. Creates and returns a new sandbox instance.

**Parameters:**
- `directory` (String): The directory path containing the scene and its scripts
- `tscn_filename` (String): The name of the `.tscn` scene file to load

**Returns:** A configured `HMSandbox` instance, or `null` on failure

**Example:**
```gdscript
var manager = get_node("/root/HMSandboxManager")  # Access global singleton
var sandbox = manager.load_sandbox("res://mods/mod1", "main.tscn")
```

---

#### `register_sandbox(sandbox: HMSandbox)`

Manually registers a sandbox instance with the manager.

**Parameters:**
- `sandbox` (HMSandbox): The sandbox instance to register

**Description:**
Usually called automatically by `HMSandbox.load()`. Only needed if creating sandboxes manually.

**Example:**
```gdscript
var sandbox = HMSandbox.new()
sandbox.set_profile_id("custom_id")
manager.register_sandbox(sandbox)
```

---

#### `unregister_sandbox(sandbox: HMSandbox)`

Unregisters a sandbox instance and unloads its resources.

**Parameters:**
- `sandbox` (HMSandbox): The sandbox instance to unregister

**Description:**
Removes the sandbox from the manager's registry and calls `sandbox.unload()`.

**Example:**
```gdscript
manager.unregister_sandbox(sandbox)
```

---

#### `find_sandbox_by_profile_id(profile_id: String) -> HMSandbox`

Finds a registered sandbox by its unique profile ID.

**Parameters:**
- `profile_id` (String): The sandbox profile ID to search for

**Returns:** The `HMSandbox` instance, or `null` if not found

**Example:**
```gdscript
var sandbox = manager.find_sandbox_by_profile_id("Sandbox_a1b2c3d4")
if sandbox:
    print("Found sandbox: ", sandbox.get_scene_filename())
```

---

#### `find_sandbox_by_script_path(script_path: String) -> HMSandbox`

Finds the sandbox that contains a specific script file.

**Parameters:**
- `script_path` (String): The script file path to search for

**Returns:** The `HMSandbox` instance containing the script, or `null` if not found

**Description:**
Searches all registered sandboxes to find which one has the given script path in its class registry.

**Example:**
```gdscript
var script_path = "res://mods/mod1/player.hm"
var sandbox = manager.find_sandbox_by_script_path(script_path)
if sandbox:
    print("Script belongs to sandbox: ", sandbox.get_profile_id())
```

---

#### `remove_script_cache(script_path: String)`

Clears the GDScript cache for a specific script file.

**Parameters:**
- `script_path` (String): The script file path to remove from cache

**Description:**
Forces the script to be reloaded from disk on next access. Useful for hot-reloading during development.

**Example:**
```gdscript
manager.remove_script_cache("res://mods/mod1/player.hm")
# Now reload the script
```

---

## Usage Examples

### Example 1: Loading and Running a Mod

```gdscript
extends Node

var sandbox: HMSandbox

func _ready():
    # Load the mod
    sandbox = HMSandbox.load("res://mods/example_mod", "main.tscn")

    if not sandbox:
        push_error("Failed to load mod")
        return

    # Configure resource limits
    sandbox.set_timeout_ms(2000)        # 2 second timeout
    sandbox.set_memory_limit_mb(50)     # 50 MB memory limit
    sandbox.set_heavy_ops_per_frame(20) # Limit heavy operations

    # Add to scene tree
    add_child(sandbox)

    # Get the mod's root node
    var mod_root = sandbox.get_root_node()
    if mod_root:
        print("Mod loaded: ", mod_root.name)

func _process(_delta):
    # Reset frame counters every frame
    if sandbox:
        sandbox.reset_frame_counters()

func _exit_tree():
    # Clean up
    if sandbox:
        sandbox.unload()
```

---

### Example 2: Managing Multiple Mods

```gdscript
extends Node

var manager: HMSandboxManager
var active_mods: Array[HMSandbox] = []

func _ready():
    # Get global manager
    manager = get_node("/root/HMSandboxManager")

    # Load multiple mods
    load_mod("res://mods/mod1", "main.tscn")
    load_mod("res://mods/mod2", "main.tscn")
    load_mod("res://mods/mod3", "main.tscn")

func load_mod(directory: String, scene_file: String):
    var sandbox = manager.load_sandbox(directory, scene_file)

    if sandbox:
        # Configure limits
        sandbox.set_timeout_ms(1000)
        sandbox.set_memory_limit_mb(100)

        # Add to scene
        add_child(sandbox)
        active_mods.append(sandbox)

        print("Loaded mod: ", sandbox.get_profile_id())
    else:
        push_error("Failed to load mod from: ", directory)

func unload_mod(profile_id: String):
    var sandbox = manager.find_sandbox_by_profile_id(profile_id)
    if sandbox:
        active_mods.erase(sandbox)
        manager.unregister_sandbox(sandbox)
        sandbox.queue_free()
        print("Unloaded mod: ", profile_id)

func _process(_delta):
    # Reset all mod frame counters
    for sandbox in active_mods:
        sandbox.reset_frame_counters()
```

---

### Example 3: Error Handling and Debugging

```gdscript
extends Node

var sandbox: HMSandbox

func _ready():
    sandbox = HMSandbox.load("res://mods/test_mod", "main.tscn")

    if sandbox:
        add_child(sandbox)
        check_sandbox_health()

func check_sandbox_health():
    # Check for errors
    var last_error = sandbox.get_last_error()
    if not last_error.is_empty():
        print("Last error: ", last_error)

    # Get all errors
    var all_errors = sandbox.get_all_errors()
    if all_errors.size() > 0:
        print("Total errors: ", all_errors.size())

        # Generate error report
        var report = sandbox.get_error_report_markdown()
        print("=== Error Report ===")
        print(report)

        # Save to file
        var file = FileAccess.open("user://mod_errors.md", FileAccess.WRITE)
        if file:
            file.store_string(report)
            file.close()
            print("Error report saved to user://mod_errors.md")

func get_mod_info():
    if not sandbox:
        return

    print("=== Mod Information ===")
    print("Profile ID: ", sandbox.get_profile_id())
    print("Directory: ", sandbox.get_load_directory())
    print("Scene: ", sandbox.get_scene_filename())

    # List all registered classes
    var classes = sandbox.get_local_classes()
    print("\nRegistered Classes:")
    for class_name in classes:
        var path = sandbox.get_script_path_for_class(class_name)
        print("  - ", class_name, " (", path, ")")

    # List dependencies
    var deps = sandbox.get_dependencies()
    print("\nDependencies (", deps.size(), " scripts):")
    for dep in deps:
        print("  - ", dep)
```

---

### Example 4: Inspecting Mod Classes

```gdscript
extends Node

func inspect_mod(mod_directory: String):
    # Collect all scripts without loading
    var scripts = HMSandbox.collect_dependencies(mod_directory)

    print("=== Mod Scripts ===")
    print("Found ", scripts.size(), " scripts in ", mod_directory)

    for script_path in scripts:
        print("\nScript: ", script_path)

    # Now load the mod
    var sandbox = HMSandbox.load(mod_directory, "main.tscn")
    if sandbox:
        print("\n=== Loaded Classes ===")
        var classes = sandbox.get_local_classes()

        for class_name in classes:
            var info = classes[class_name]
            print("\nClass: ", class_name)
            print("  Base Type: ", info.base_type)
            print("  Script Path: ", info.script_path)
            print("  Is Abstract: ", info.is_abstract)
            print("  Is Tool: ", info.is_tool)

        # Cleanup
        sandbox.unload()
        sandbox.queue_free()
```

---

## Notes

### Script File Extensions

The sandbox system recognizes two file extensions for sandboxed scripts:
- `.hm` - HMScript source files
- `.hmc` - HMScript compiled files

Both are treated as GDScript files internally but run with sandbox restrictions.

### Profile IDs

Each sandbox instance gets a unique profile ID in the format `Sandbox_XXXXXXXX` where X is a hexadecimal digit. This ID is used to:
- Track the sandbox in the global manager
- Configure sandbox profiles for script execution
- Isolate class registries between sandboxes

### Resource Limits

Resource limits are enforced during script execution:
- **Timeout**: Maximum execution time for a single script call
- **Memory**: Maximum memory usage (checked periodically)
- **Write Ops**: Operations that modify state (checked per frame)
- **Heavy Ops**: CPU-intensive operations (checked per frame)

Frame counters must be reset every frame using `reset_frame_counters()` or by the global manager's `frame_callback()`.

### Error Types

Common error types reported by the sandbox:
- `"sandbox"` - General sandbox errors
- `"timeout"` - Execution timeout exceeded
- `"memory"` - Memory limit exceeded
- `"gdscript"` - Script execution errors

### Class Registry

Each sandbox maintains an isolated class registry that:
- Stores class names and their script paths
- Enables base class resolution during compilation
- Prevents class name conflicts between sandboxes
- Allows scripts to reference other scripts in the same mod

---

## See Also

- [SANDBOX_QUERY_API.md](SANDBOX_QUERY_API.md) - C++ query APIs for sandbox introspection
- [SANDBOX_ISOLATED_CLASS_REGISTRY.md](SANDBOX_ISOLATED_CLASS_REGISTRY.md) - Class registry implementation details
- [design.md](../design.md) - Overall architecture and design
