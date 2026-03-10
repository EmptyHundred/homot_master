# HolyMolly Module - GDScript API Reference

The HolyMolly module provides sandboxed GDScript execution for the Homot engine. It exposes two classes to GDScript: **HMSandbox** (a per-sandbox instance) and **HMSandboxManager** (a global singleton).

---

## HMSandbox

**Inherits:** `Node`

A sandbox instance that loads and isolates GDScript code from a directory. Each sandbox maintains its own class registry, dependency map, execution limits, and error tracking.

### Properties

| Name | Type | Default | Description |
|------|------|---------|-------------|
| `profiler_enabled` | `bool` | — | Enables or disables the profiler for this sandbox. |

### Static Methods

#### load

```gdscript
static func load(directory: String, tscn_filename: String) -> HMSandbox
```

Loads a sandbox from a directory containing `.hm` / `.hmc` scripts and a `.tscn` scene file.

**Loading workflow:**
1. Generates a unique `profile_id` (format: `Sandbox_XXXXXXXX`).
2. Scans the directory recursively for all `.hm` and `.hmc` script files.
3. Parses scripts to extract class names and registers them in a sandbox-local class registry.
4. Loads all GDScript resources.
5. Loads and instantiates the PackedScene.
6. Registers the sandbox in `HMSandboxManager`.

Returns `null` on failure.

---

#### collect_dependencies

```gdscript
static func collect_dependencies(dir_path: String) -> PackedStringArray
```

Recursively collects all `.hm` and `.hmc` file paths from the given directory. Returns an array of full script paths.

---

### Instance Methods

#### Identification & Scene Access

---

##### get_profile_id

```gdscript
func get_profile_id() -> String
```

Returns the unique sandbox profile identifier (e.g. `"Sandbox_A3F1B2C0"`).

---

##### get_packed_scene

```gdscript
func get_packed_scene() -> PackedScene
```

Returns the loaded `PackedScene` resource.

---

##### get_root_node

```gdscript
func get_root_node() -> Node
```

Returns the instantiated root node (first child of the sandbox). Returns `null` if no root node exists.

---

##### get_load_directory

```gdscript
func get_load_directory() -> String
```

Returns the directory from which this sandbox was loaded.

---

##### get_scene_filename

```gdscript
func get_scene_filename() -> String
```

Returns the filename of the loaded scene file (e.g. `"scene.tscn"`).

---

#### Execution Limits

---

##### set_timeout_ms

```gdscript
func set_timeout_ms(ms: int) -> void
```

Sets the execution timeout in milliseconds. Scripts exceeding this limit will be halted.

---

##### set_memory_limit_mb

```gdscript
func set_memory_limit_mb(mb: int) -> void
```

Sets the memory limit in megabytes for this sandbox.

---

##### set_write_ops_per_frame

```gdscript
func set_write_ops_per_frame(count: int) -> void
```

Sets the maximum number of write operations allowed per frame.

---

##### set_heavy_ops_per_frame

```gdscript
func set_heavy_ops_per_frame(count: int) -> void
```

Sets the maximum number of heavy operations allowed per frame.

---

##### reset_frame_counters

```gdscript
func reset_frame_counters() -> void
```

Resets per-frame operation counters (write ops, heavy ops).

---

#### Profiler

---

##### set_profiler_enabled

```gdscript
func set_profiler_enabled(enabled: bool) -> void
```

Enables or disables the profiler for this sandbox's profile.

---

##### is_profiler_enabled

```gdscript
func is_profiler_enabled() -> bool
```

Returns whether the profiler is enabled for this sandbox.

---

#### Error Handling

---

##### get_last_error

```gdscript
func get_last_error() -> String
```

Returns the most recent error message string. Empty if no errors.

---

##### get_all_errors

```gdscript
func get_all_errors() -> Array
```

Returns an array of all error dictionaries. Each dictionary has the following keys:

| Key | Type | Description |
|-----|------|-------------|
| `id` | `String` | Unique error identifier |
| `type` | `String` | Error type (`"sandbox"`, `"timeout"`, `"memory"`, `"gdscript"`) |
| `severity` | `String` | Severity level (default `"error"`) |
| `message` | `String` | Error message |
| `file` | `String` | Source file path |
| `line` | `int` | Line number |
| `column` | `int` | Column number |
| `stack_trace` | `String` | Stack trace if available |
| `trigger_context` | `String` | Execution context |
| `phase` | `String` | Execution phase |
| `timestamp` | `int` | When the error occurred (milliseconds) |
| `last_occurrence` | `int` | Last occurrence time (milliseconds) |
| `occurrence_count` | `int` | Number of times this error occurred |

---

##### get_error_report_markdown

```gdscript
func get_error_report_markdown() -> String
```

Returns a formatted Markdown report of all accumulated errors.

---

#### Dependencies & Class Management

---

##### get_dependencies

```gdscript
func get_dependencies() -> PackedStringArray
```

Returns an array of all dependency script paths registered in this sandbox.

---

##### get_local_classes

```gdscript
func get_local_classes() -> Dictionary
```

Returns a dictionary of all registered local classes. Format: `{ "ClassName": "script_path", ... }`.

---

##### has_script_path

```gdscript
func has_script_path(path: String) -> bool
```

Returns `true` if the given script path is registered in this sandbox.

---

##### get_script_path_for_class

```gdscript
func get_script_path_for_class(class_name: String) -> String
```

Returns the script path for the given class name. Returns an empty string if not found.

---

##### unload

```gdscript
func unload() -> void
```

Cleans up the sandbox: clears the class registry, removes child nodes, purges GDScript cache entries for all dependencies, and clears the dependencies map.

---

## HMSandboxManager

**Inherits:** `Object`

Global singleton (registered as `HMSandboxManager` in the engine) that manages all sandbox instances. Access it directly by name in GDScript.

### Properties

| Name | Type | Default | Description |
|------|------|---------|-------------|
| `default_profiler_enabled` | `bool` | — | Controls whether the default `"hm_default"` profile has profiling enabled. |

### Methods

---

#### load_sandbox

```gdscript
func load_sandbox(directory: String, tscn_filename: String) -> HMSandbox
```

Creates and registers a new sandbox. Wrapper around `HMSandbox.load()`.

---

#### register_sandbox

```gdscript
func register_sandbox(sandbox: HMSandbox) -> void
```

Registers a sandbox instance in the manager by its `profile_id`. Prints a warning if the `profile_id` is already registered.

---

#### unregister_sandbox

```gdscript
func unregister_sandbox(sandbox: HMSandbox) -> void
```

Removes a sandbox from the manager and calls `sandbox.unload()` to clean up resources.

---

#### find_sandbox_by_profile_id

```gdscript
func find_sandbox_by_profile_id(profile_id: String) -> HMSandbox
```

Looks up a sandbox by its unique profile ID. Returns `null` if not found.

---

#### find_sandbox_by_script_path

```gdscript
func find_sandbox_by_script_path(script_path: String) -> HMSandbox
```

Searches all registered sandboxes for one that contains the given script path. Returns the first match, or `null` if not found.

---

#### remove_script_cache

```gdscript
func remove_script_cache(script_path: String) -> void
```

Removes a script from the GDScript cache.

---

#### set_default_profiler_enabled

```gdscript
func set_default_profiler_enabled(enabled: bool) -> void
```

Sets the enabled state for the `"hm_default"` SandboxProfile. Creates the profile if it doesn't exist.

---

#### is_default_profiler_enabled

```gdscript
func is_default_profiler_enabled() -> bool
```

Returns the enabled state for the `"hm_default"` profile. Returns `true` if the profile has not been created yet.

---

#### get_cached_script_paths

```gdscript
func get_cached_script_paths() -> PackedStringArray
```

Returns all GDScript resource paths currently in the Godot `ResourceCache`.

---

## Security: Default Blocklist

The sandbox enforces a default blocklist to prevent untrusted scripts from accessing dangerous APIs.

### Blocked Classes

| Category | Classes |
|----------|---------|
| File System | `FileAccess`, `DirAccess` |
| OS | `OS` |
| Networking | `HTTPClient`, `HTTPRequest`, `StreamPeerTCP`, `StreamPeerTLS`, `TCPServer`, `UDPServer`, `PacketPeerUDP`, `WebSocketPeer` |
| Threading | `Thread`, `Mutex`, `Semaphore`, `WorkerThreadPool` |
| Engine Internals | `NativeExtension`, `ResourceLoader`, `ResourceSaver` |
| Editor | `EditorInterface`, `EditorPlugin`, `EditorScript`, `ProjectSettings` |

### Blocked Methods (all classes)

| Class | Blocked Methods |
|-------|----------------|
| `Object` | `call`, `callv`, `set`, `set_deferred`, `call_deferred`, `free` |
| `ClassDB` | `instantiate`, `instance`, `get_class_list` |
| `Engine` | `get_singleton`, `register_singleton`, `unregister_singleton` |

### Allowed Paths

Only `res://` and `user://` paths are accessible.

---

## Usage Example

```gdscript
# Load a sandbox from a directory
var sandbox := HMSandbox.load("res://sandboxes/my_game", "scene.tscn")

# Configure execution limits
sandbox.set_timeout_ms(5000)
sandbox.set_memory_limit_mb(128)
sandbox.set_write_ops_per_frame(500)
sandbox.set_heavy_ops_per_frame(100)

# Inspect loaded classes
var classes: Dictionary = sandbox.get_local_classes()
print("Loaded classes: ", classes)

# Access the instantiated scene root
var root: Node = sandbox.get_root_node()
if root:
    add_child(root)

# Check for errors
if sandbox.get_last_error() != "":
    print("Error: ", sandbox.get_last_error())
    var all_errors: Array = sandbox.get_all_errors()
    for err in all_errors:
        print("[%s] %s at %s:%d" % [err.type, err.message, err.file, err.line])

# Use the manager singleton to find sandboxes
var found := HMSandboxManager.find_sandbox_by_script_path("res://sandboxes/my_game/enemy.hm")

# Toggle profiling
HMSandboxManager.default_profiler_enabled = true
sandbox.profiler_enabled = true

# Cleanup
sandbox.unload()
HMSandboxManager.unregister_sandbox(sandbox)
```
