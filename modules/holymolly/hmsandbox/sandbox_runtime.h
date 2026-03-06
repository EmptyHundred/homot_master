/**************************************************************************/
/*  sandbox_runtime.h                                                     */
/**************************************************************************/

#pragma once

#include "sandbox_class_registry.h"
#include "sandbox_config.h"
#include "sandbox_error.h"
#include "sandbox_limiter.h"

#include "core/string/ustring.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"
#include "scene/main/node.h"

class Script;
class Resource;
class PackedScene;

namespace hmsandbox {

class HMSandboxManager;
struct SandboxProfile; // Forward declaration

// 轻量运行时聚合器，将配置、限流和错误仓库组合在一起。
// 不直接修改 GDScript 内部，只作为 HMScript 等上层入口的工具类。
// 可实例化，每个实例代表一个独立的沙盒环境。
class HMSandbox : public Node {
	GDCLASS(HMSandbox, Node);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	HMSandbox();
	~HMSandbox();

	void set_profile_id(const String &p_id);
	String get_profile_id() const;

	void set_packed_scene(const Ref<PackedScene> &p_scene);
	Ref<PackedScene> get_packed_scene() const;

	void set_root_node(Node *p_node);
	Node *get_root_node() const;

	void set_load_directory(const String &p_directory);
	String get_load_directory() const;

	void set_scene_filename(const String &p_filename);
	String get_scene_filename() const;

	// Wrapper accessors - lookup GDScriptLanguage profile by profile_id
	HMSandboxConfig &get_config();
	const HMSandboxConfig &get_config() const;

	HMSandboxLimiter &get_limiter();
	const HMSandboxLimiter &get_limiter() const;

	HMSandboxErrorRegistry &get_error_registry();
	const HMSandboxErrorRegistry &get_error_registry() const;

	void set_timeout_ms(int p_ms);
	void set_memory_limit_mb(int p_mb);
	void set_write_ops_per_frame(int p_count);
	void set_heavy_ops_per_frame(int p_count);
	void reset_frame_counters();

	// 简单入口：在受控环境中调用脚本函数。
	// 注意：这里只负责限流与错误记录，不拦截脚本内部所有调用。
	Variant call_script_function(const Ref<Script> &p_script,
			Object *p_owner,
			const StringName &p_method,
			const Array &p_args,
			String &r_error);

	// 记录来自外部的错误（例如自定义包装层捕获到的异常）。
	void add_error(const String &p_type,
			const String &p_message,
			const String &p_file = "",
			int p_line = 0,
			int p_column = 0,
			const String &p_stack_trace = "",
			const String &p_severity = "error",
			const String &p_trigger_context = "",
			const String &p_phase = "");

	String get_last_error() const;
	Array get_all_errors() const;
	String get_error_report_markdown() const;

	void set_dependencies(const PackedStringArray &p_dependencies);
	PackedStringArray get_dependencies() const;

	// Get a loaded dependency script by path (returns null if not found or not loaded yet)
	Ref<GDScript> get_dependency_script(const String &p_path) const;

	// Instance method: Resolve and populate dependencies by scanning the load directory
	// recursively for all .hm and .hmc files. Stores results in the dependencies property.
	void resolve_dependencies();

	// Apply sandbox profile to all loaded dependency scripts
	// Iterates through dependencies and sets sandbox_enabled and profile_id for each loaded script
	void configure_script_profiles();

	// Pre-scan script files and register class names BEFORE loading scripts
	// Uses lightweight parsing to extract class_name without full compilation
	// This allows scripts to find base classes during compilation
	void register_classes();

	// Sandbox-local class registry for isolated class resolution
	bool has_local_class(const String &p_class_name) const;
	Ref<GDScript> lookup_local_class(const String &p_class_name) const;
	Dictionary get_local_classes() const;
	SandboxClassRegistry &get_class_registry() { return class_registry; }
	const SandboxClassRegistry &get_class_registry() const { return class_registry; }

	// Check if a script path is registered in this sandbox
	bool has_script_path(const String &p_path) const;

	// Get the script path for a registered class name
	// Returns empty string if class is not registered
	String get_script_path_for_class(const String &p_class_name) const;

	// Unload the sandbox, cleaning up resources and clearing caches
	void unload();

	// Static loader method - loads a sandbox from directory and scene file
	static HMSandbox *load(const String &p_directory, const String &p_tscn_filename);

	// Static method: Collect all .hm and .hmc files recursively from a directory
	// Returns a PackedStringArray of all found script paths
	static PackedStringArray collect_dependencies(const String &p_dir_path);

	// Generate a unique sandbox ID in the format "Sandbox_XXXXXXXX"
	// Returns a string with "Sandbox_" prefix followed by 8 hex characters
	static String generate_sandbox_id();

private:
	String profile_id;
	Ref<PackedScene> packed_scene;

	String load_directory; // Directory from which the sandbox was loaded
	String scene_filename; // Filename of the scene file (e.g., "scene.tscn")

	// Map of script path -> loaded GDScript for all .hm and .hmc dependencies in the sandbox
	HashMap<String, Ref<GDScript>> dependencies;

	// Direct pointer to the GDScriptLanguage profile
	SandboxProfile *profile = nullptr;

	// Sandbox-local class registry for isolated class name resolution
	SandboxClassRegistry class_registry;

	// Static dummy instances for error cases (when profile lookup fails)
	static HMSandboxConfig dummy_config;
	static HMSandboxLimiter dummy_limiter;
	static HMSandboxErrorRegistry dummy_errors;

	// Set the profile pointer directly
	void set_profile(SandboxProfile *p_profile);

	friend class HMSandboxManager;
};

} // namespace hmsandbox

