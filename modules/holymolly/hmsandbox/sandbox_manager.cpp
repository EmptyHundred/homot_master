/**************************************************************************/
/*  sandbox_manager.cpp                                                   */
/**************************************************************************/

#include "sandbox_manager.h"
#include "sandbox_profile.h"

#include "core/config/project_settings.h"
#include "core/io/resource.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/os/os.h"
#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/gdscript_cache.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"

namespace hmsandbox {

void HMSandboxManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load_sandbox", "directory", "tscn_filename"), &HMSandboxManager::load_sandbox);
	ClassDB::bind_method(D_METHOD("register_sandbox", "sandbox"), &HMSandboxManager::register_sandbox);
	ClassDB::bind_method(D_METHOD("unregister_sandbox", "sandbox"), &HMSandboxManager::unregister_sandbox);
	ClassDB::bind_method(D_METHOD("find_sandbox_by_profile_id", "profile_id"), &HMSandboxManager::find_sandbox_by_profile_id);
	ClassDB::bind_method(D_METHOD("find_sandbox_by_script_path", "script_path"), &HMSandboxManager::find_sandbox_by_script_path);
	ClassDB::bind_method(D_METHOD("remove_script_cache", "script_path"), &HMSandboxManager::remove_script_cache);

	ClassDB::bind_method(D_METHOD("set_default_profiler_enabled", "enabled"), &HMSandboxManager::set_default_profiler_enabled);
	ClassDB::bind_method(D_METHOD("is_default_profiler_enabled"), &HMSandboxManager::is_default_profiler_enabled);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "default_profiler_enabled"), "set_default_profiler_enabled", "is_default_profiler_enabled");

	ClassDB::bind_method(D_METHOD("get_cached_script_paths"), &HMSandboxManager::get_cached_script_paths);
}

HMSandboxManager::HMSandboxManager() {
}

HMSandboxManager::~HMSandboxManager() {
	profile_to_sandbox.clear();
}

HMSandbox *HMSandboxManager::load_sandbox(const String &p_directory, const String &p_tscn_filename) {
	// Wrapper that calls the static load method
	return HMSandbox::load(p_directory, p_tscn_filename);
}

void HMSandboxManager::frame_callback() {
	for (HashMap<String, HMSandbox *>::Iterator it = profile_to_sandbox.begin(); it != profile_to_sandbox.end(); ++it) {
		if (it->value) {
			it->value->reset_frame_counters();
		}
	}
}

void HMSandboxManager::register_sandbox(HMSandbox *p_sandbox) {
	if (!p_sandbox) {
		return;
	}

	String profile_id = p_sandbox->get_profile_id();
	if (profile_id.is_empty()) {
		ERR_PRINT("Cannot register sandbox without a profile_id");
		return;
	}

	if (profile_to_sandbox.has(profile_id)) {
		WARN_PRINT(vformat("Sandbox with profile_id '%s' is already registered. Replacing.", profile_id));
	}

	profile_to_sandbox[profile_id] = p_sandbox;
}

void HMSandboxManager::unregister_sandbox(HMSandbox *p_sandbox) {
	if (!p_sandbox) {
		return;
	}

	// Remove from profile_to_sandbox map
	String profile_id = p_sandbox->get_profile_id();
	if (!profile_id.is_empty()) {
		profile_to_sandbox.erase(profile_id);
	}

	// Unload the sandbox (cleanup resources and clear caches)
	p_sandbox->unload();
}

HMSandbox *HMSandboxManager::find_sandbox_by_profile_id(const String &p_profile_id) {
	if (p_profile_id.is_empty()) {
		return nullptr;
	}

	if (profile_to_sandbox.has(p_profile_id)) {
		return profile_to_sandbox[p_profile_id];
	}

	return nullptr;
}

HMSandbox *HMSandboxManager::find_sandbox_by_script_path(const String &p_script_path) {
	if (p_script_path.is_empty()) {
		return nullptr;
	}

	// Get all registered sandboxes
	Vector<HMSandbox *> sandboxes = get_all_sandboxes();

	// Iterate through all registered sandboxes
	for (int i = 0; i < sandboxes.size(); i++) {
		HMSandbox *sandbox = sandboxes[i];
		if (!sandbox) {
			continue;
		}

		// Check if the script path is registered in the sandbox's class registry
		if (sandbox->has_script_path(p_script_path)) {
			return sandbox;
		}
	}

	return nullptr;
}

Vector<HMSandbox *> HMSandboxManager::get_all_sandboxes() const {
	Vector<HMSandbox *> result;
	result.resize(profile_to_sandbox.size());

	int index = 0;
	for (HashMap<String, HMSandbox *>::ConstIterator it = profile_to_sandbox.begin(); it != profile_to_sandbox.end(); ++it) {
		result.write[index++] = it->value;
	}

	return result;
}

void HMSandboxManager::remove_script_cache(const String &p_script_path) {
	if (p_script_path.is_empty()) {
		return;
	}
	GDScriptCache::remove_script(p_script_path);
}

void HMSandboxManager::set_default_profiler_enabled(bool p_enabled) {
	GDScriptLanguage *lang = GDScriptLanguage::get_singleton();
	if (!lang) {
		ERR_PRINT("GDScriptLanguage not available.");
		return;
	}

	SandboxProfile *profile = lang->ensure_sandbox_profile("hm_default");
	if (profile) {
		profile->enabled = p_enabled;
	}
}

bool HMSandboxManager::is_default_profiler_enabled() const {
	GDScriptLanguage *lang = GDScriptLanguage::get_singleton();
	if (!lang) {
		return true; // Default to enabled if language not available
	}

	SandboxProfile *profile = lang->ensure_sandbox_profile("hm_default");
	if (profile) {
		return profile->enabled;
	}

	return true; // Default to enabled if profile not found
}

PackedStringArray HMSandboxManager::get_cached_script_paths() const {
	PackedStringArray result;

	// Get all cached resources from ResourceCache
	List<Ref<Resource>> cached_resources;
	ResourceCache::get_cached_resources(&cached_resources);

	// Filter for GDScript resources and extract their paths
	for (List<Ref<Resource>>::Element *E = cached_resources.front(); E; E = E->next()) {
		Ref<Resource> res = E->get();
		if (res.is_null()) {
			continue;
		}

		// Check if it's a GDScript resource
		Ref<GDScript> script = res;
		if (script.is_valid()) {
			String path = script->get_path();
			if (!path.is_empty()) {
				result.push_back(path);
			}
		}
	}

	return result;
}

} // namespace hmsandbox
