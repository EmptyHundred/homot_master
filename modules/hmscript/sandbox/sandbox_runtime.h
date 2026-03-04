/**************************************************************************/
/*  sandbox_runtime.h                                                     */
/**************************************************************************/

#pragma once

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

	// Unload the sandbox, cleaning up resources and clearing caches
	void unload();

	// Static loader method - loads a sandbox from directory and scene file
	static HMSandbox *load(const String &p_directory, const String &p_tscn_filename);

	// Collect all .hm and .hmc files recursively from a directory
	static PackedStringArray collect_dependencies(const String &p_directory);

	// Generate a unique UUID for sandbox identification
	static String generate_uuid();

private:
	String profile_id;
	Ref<PackedScene> packed_scene;

	String load_directory; // Directory from which the sandbox was loaded
	String scene_filename; // Filename of the scene file (e.g., "scene.tscn")

	PackedStringArray dependencies; // All .hm and .hmc file paths found in the sandbox directory

	// Direct pointer to the GDScriptLanguage profile
	SandboxProfile *profile = nullptr;

	// Static dummy instances for error cases (when profile lookup fails)
	static HMSandboxConfig dummy_config;
	static HMSandboxLimiter dummy_limiter;
	static HMSandboxErrorRegistry dummy_errors;

	// Set the profile pointer directly
	void set_profile(SandboxProfile *p_profile);

	friend class HMSandboxManager;
};

} // namespace hmsandbox

