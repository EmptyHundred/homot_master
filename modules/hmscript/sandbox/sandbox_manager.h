/**************************************************************************/
/*  sandbox_manager.h                                                     */
/**************************************************************************/

#pragma once

#include "sandbox_runtime.h"

#include "core/object/object.h"
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

	// 清除指定脚本的缓存。
	void remove_script_cache(const String &p_script_path);

private:
	Vector<HMSandbox *> active_sandboxes;

	friend class HMSandbox;
};

} // namespace hmsandbox
