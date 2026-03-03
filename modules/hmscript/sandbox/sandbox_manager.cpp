/**************************************************************************/
/*  sandbox_manager.cpp                                                   */
/**************************************************************************/

#include "sandbox_manager.h"

#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/os/os.h"
#include "modules/gdscript/gdscript_cache.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"

namespace hmsandbox {

void HMSandboxManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load_sandbox", "directory", "tscn_filename"), &HMSandboxManager::load_sandbox);
	ClassDB::bind_method(D_METHOD("register_sandbox", "sandbox"), &HMSandboxManager::register_sandbox);
	ClassDB::bind_method(D_METHOD("unregister_sandbox", "sandbox"), &HMSandboxManager::unregister_sandbox);
	ClassDB::bind_method(D_METHOD("remove_script_cache", "script_path"), &HMSandboxManager::remove_script_cache);
}

HMSandboxManager::HMSandboxManager() {
}

HMSandboxManager::~HMSandboxManager() {
	active_sandboxes.clear();
}

HMSandbox *HMSandboxManager::load_sandbox(const String &p_directory, const String &p_tscn_filename) {
	// Wrapper that calls the static load method
	return HMSandbox::load(p_directory, p_tscn_filename);
}

void HMSandboxManager::frame_callback() {
	for (int i = 0; i < active_sandboxes.size(); i++) {
		if (active_sandboxes[i]) {
			active_sandboxes[i]->reset_frame_counters();
		}
	}
}

void HMSandboxManager::register_sandbox(HMSandbox *p_sandbox) {
	if (p_sandbox && !active_sandboxes.has(p_sandbox)) {
		active_sandboxes.push_back(p_sandbox);
	}
}

void HMSandboxManager::unregister_sandbox(HMSandbox *p_sandbox) {
	if (!p_sandbox) {
		return;
	}

	// Remove from active sandboxes list
	active_sandboxes.erase(p_sandbox);

	// Unload the sandbox (cleanup resources and clear caches)
	p_sandbox->unload();
}

void HMSandboxManager::remove_script_cache(const String &p_script_path) {
	if (p_script_path.is_empty()) {
		return;
	}
	GDScriptCache::remove_script(p_script_path);
}

} // namespace hmsandbox
