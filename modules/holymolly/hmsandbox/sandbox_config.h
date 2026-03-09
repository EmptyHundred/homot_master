/**************************************************************************/
/*  sandbox_config.h                                                      */
/**************************************************************************/
/*  HMScript sandbox utilities.                                           */
/**************************************************************************/

#pragma once

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/string/ustring.h"
#include "core/templates/hash_set.h"

namespace hmsandbox {

// 配置强沙盒允许 / 禁止访问的类、方法、属性与路径前缀。
class HMSandboxConfig {
public:
	HMSandboxConfig();

	// 从 JSON 文件加载自定义策略（可选）。
	// 结构示例：
	// {
	//   "blocked_classes": ["OS", "FileAccess"],
	//   "blocked_methods": ["Object.call", "ClassDB.instantiate"],
	//   "blocked_properties": ["Node.script"],
	//   "allowed_paths": ["res://", "user://"]
	// }
	Error load(const String &p_path);

	// 类屏蔽（含父类）。
	void block_class(const StringName &p_class_name);
	bool is_class_or_parent_blocked(const StringName &p_class_name) const;

	// 方法屏蔽（"Class.method"，含父类）。
	void block_method(const StringName &p_class_name, const StringName &p_method_name);
	bool is_method_blocked_with_inheritance(const StringName &p_class_name, const StringName &p_method_name) const;

	// 属性屏蔽（"Class.property"，含父类）。
	void block_property(const StringName &p_class_name, const StringName &p_property_name);
	bool is_property_blocked_with_inheritance(const StringName &p_class_name, const StringName &p_property_name) const;

	// 路径白名单与安全检查。
	void add_allowed_path_prefix(const String &p_prefix);
	bool is_path_allowed(const String &p_path) const;

	// 重置为默认策略。
	void reset();

private:
	HashSet<StringName> blocked_classes;
	HashSet<String> blocked_methods; // "Class.method"
	HashSet<String> blocked_properties; // "Class.property"
	HashSet<String> allowed_path_prefixes;
	bool default_blocklist_initialized = false;

	void setup_default_blocklist();
	void ensure_default_blocklist();
};

} // namespace hmsandbox

