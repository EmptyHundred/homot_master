/**************************************************************************/
/*  sandbox_manager.h                                                     */
/**************************************************************************/

#pragma once

#include "sandbox_runtime.h"

#include "core/object/object.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

class PackedScene;

namespace hmsandbox {

// 全局沙盒管理器，提供创建和管理多个沙盒实例的功能。
class HMSandboxManager : public Object {
	GDCLASS(HMSandboxManager, Object);

protected:
	static void _bind_methods();

public:
	HMSandboxManager();
	~HMSandboxManager();

	// 全局加载沙盒：加载场景并创建一个新的沙盒实例。
	// 返回配置好的 HMSandbox 实例，包含唯一的 profile_id 和加载的场景。
	HMSandbox *load_sandbox(const String &p_directory, const String &p_tscn_filename);

	// 每帧回调，重置所有活跃沙盒的帧计数器。
	void frame_callback();

	// 注册和注销沙盒实例。
	void register_sandbox(HMSandbox *p_sandbox);
	void unregister_sandbox(HMSandbox *p_sandbox);

	// 通过 profile_id 查找沙盒实例。
	HMSandbox *find_sandbox_by_profile_id(const String &p_profile_id);

	// 获取所有活跃的沙盒实例。
	Vector<HMSandbox *> get_all_sandboxes() const;

	// 清除指定脚本的缓存。
	void remove_script_cache(const String &p_script_path);

private:
	// Map from profile_id to sandbox instance
	HashMap<String, HMSandbox *> profile_to_sandbox;

	friend class HMSandbox;
};

// Global sandbox manager instance (initialized in register_types.cpp)
extern HMSandboxManager *hm_sandbox_manager;

// Helper function to get the global sandbox manager
inline HMSandboxManager *get_global_sandbox_manager() {
	return hm_sandbox_manager;
}

} // namespace hmsandbox
