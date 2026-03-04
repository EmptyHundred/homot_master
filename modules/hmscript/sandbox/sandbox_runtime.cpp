/**************************************************************************/
/*  sandbox_runtime.cpp                                                   */
/**************************************************************************/

#include "sandbox_runtime.h"
#include "sandbox_manager.h"
#include "sandbox_profile.h"

#include "core/crypto/crypto.h"
#include "core/io/dir_access.h"
#include "core/io/resource.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/object/script_language.h"
#include "core/os/os.h"
#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/gdscript_cache.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"

namespace hmsandbox {

// Forward declaration of external manager
extern HMSandboxManager *hm_sandbox_manager;

// Static dummy instances for error cases
HMSandboxConfig HMSandbox::dummy_config;
HMSandboxLimiter HMSandbox::dummy_limiter;
HMSandboxErrorRegistry HMSandbox::dummy_errors;

void HMSandbox::_bind_methods() {
	ClassDB::bind_static_method("HMSandbox", D_METHOD("load", "directory", "tscn_filename"), &HMSandbox::load);

	ClassDB::bind_method(D_METHOD("get_profile_id"), &HMSandbox::get_profile_id);

	ClassDB::bind_method(D_METHOD("get_packed_scene"), &HMSandbox::get_packed_scene);

	ClassDB::bind_method(D_METHOD("get_root_node"), &HMSandbox::get_root_node);

	ClassDB::bind_method(D_METHOD("get_load_directory"), &HMSandbox::get_load_directory);

	ClassDB::bind_method(D_METHOD("get_scene_filename"), &HMSandbox::get_scene_filename);

	ClassDB::bind_method(D_METHOD("set_timeout_ms", "ms"), &HMSandbox::set_timeout_ms);
	ClassDB::bind_method(D_METHOD("set_memory_limit_mb", "mb"), &HMSandbox::set_memory_limit_mb);
	ClassDB::bind_method(D_METHOD("set_write_ops_per_frame", "count"), &HMSandbox::set_write_ops_per_frame);
	ClassDB::bind_method(D_METHOD("set_heavy_ops_per_frame", "count"), &HMSandbox::set_heavy_ops_per_frame);
	ClassDB::bind_method(D_METHOD("reset_frame_counters"), &HMSandbox::reset_frame_counters);

	ClassDB::bind_method(D_METHOD("get_last_error"), &HMSandbox::get_last_error);
	ClassDB::bind_method(D_METHOD("get_all_errors"), &HMSandbox::get_all_errors);
	ClassDB::bind_method(D_METHOD("get_error_report_markdown"), &HMSandbox::get_error_report_markdown);

	ClassDB::bind_method(D_METHOD("get_dependencies"), &HMSandbox::get_dependencies);

	ClassDB::bind_method(D_METHOD("unload"), &HMSandbox::unload);

	ClassDB::bind_static_method("HMSandbox", D_METHOD("collect_dependencies", "directory"), &HMSandbox::collect_dependencies);
}

void HMSandbox::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PREDELETE: {
			// Unregister from manager when being deleted
			if (hm_sandbox_manager) {
				hm_sandbox_manager->unregister_sandbox(this);
			}
		} break;
	}
}

HMSandbox::HMSandbox() {
}

HMSandbox::~HMSandbox() {
	// Node destructor will handle cleanup of children
}

void HMSandbox::set_profile(SandboxProfile *p_profile) {
	profile = p_profile;
}

void HMSandbox::set_profile_id(const String &p_id) {
	profile_id = p_id;
}

String HMSandbox::get_profile_id() const {
	return profile_id;
}

void HMSandbox::set_packed_scene(const Ref<PackedScene> &p_scene) {
	packed_scene = p_scene;
}

Ref<PackedScene> HMSandbox::get_packed_scene() const {
	return packed_scene;
}

void HMSandbox::set_root_node(Node *p_node) {
	// Remove all current children
	while (get_child_count() > 0) {
		Node *child = get_child(0);
		remove_child(child);
		if (child->is_inside_tree()) {
			child->queue_free();
		} else {
			memdelete(child);
		}
	}

	// Add the new node as a child if not null
	if (p_node) {
		add_child(p_node);
	}
}

Node *HMSandbox::get_root_node() const {
	return get_child_count() > 0 ? get_child(0) : nullptr;
}

void HMSandbox::set_load_directory(const String &p_directory) {
	load_directory = p_directory;
}

String HMSandbox::get_load_directory() const {
	return load_directory;
}

void HMSandbox::set_scene_filename(const String &p_filename) {
	scene_filename = p_filename;
}

String HMSandbox::get_scene_filename() const {
	return scene_filename;
}

void HMSandbox::set_dependencies(const PackedStringArray &p_dependencies) {
	dependencies = p_dependencies;
}

PackedStringArray HMSandbox::get_dependencies() const {
	return dependencies;
}

HMSandboxConfig &HMSandbox::get_config() {
	ERR_FAIL_NULL_V(profile, dummy_config);
	return profile->config;
}

const HMSandboxConfig &HMSandbox::get_config() const {
	ERR_FAIL_NULL_V(profile, dummy_config);
	return profile->config;
}

HMSandboxLimiter &HMSandbox::get_limiter() {
	ERR_FAIL_NULL_V(profile, dummy_limiter);
	return profile->limiter;
}

const HMSandboxLimiter &HMSandbox::get_limiter() const {
	ERR_FAIL_NULL_V(profile, dummy_limiter);
	return profile->limiter;
}

HMSandboxErrorRegistry &HMSandbox::get_error_registry() {
	ERR_FAIL_NULL_V(profile, dummy_errors);
	return profile->errors;
}

const HMSandboxErrorRegistry &HMSandbox::get_error_registry() const {
	ERR_FAIL_NULL_V(profile, dummy_errors);
	return profile->errors;
}

void HMSandbox::set_timeout_ms(int p_ms) {
	get_limiter().set_timeout_ms(p_ms);
}

void HMSandbox::set_memory_limit_mb(int p_mb) {
	get_limiter().set_memory_limit_mb(p_mb);
}

void HMSandbox::set_write_ops_per_frame(int p_count) {
	get_limiter().set_write_ops_per_frame(p_count);
}

void HMSandbox::set_heavy_ops_per_frame(int p_count) {
	get_limiter().set_heavy_ops_per_frame(p_count);
}

void HMSandbox::reset_frame_counters() {
	get_limiter().reset_frame_counters();
}

Variant HMSandbox::call_script_function(const Ref<Script> &p_script,
		Object *p_owner,
		const StringName &p_method,
		const Array &p_args,
		String &r_error) {
	r_error = String();

	if (p_script.is_null() || p_owner == nullptr) {
		r_error = "Invalid script or owner.";
		add_error("sandbox", r_error);
		return Variant();
	}

	if (get_limiter().is_timeout_exceeded()) {
		r_error = "Sandbox timeout exceeded before call.";
		add_error("timeout", r_error);
		return Variant();
	}

	if (get_limiter().is_memory_limit_exceeded()) {
		r_error = "Sandbox memory limit exceeded before call.";
		add_error("memory", r_error);
		return Variant();
	}

	get_limiter().begin_execution();

	const int argc = p_args.size();
	Vector<const Variant *> arg_ptrs;
	arg_ptrs.resize(argc);
	for (int i = 0; i < argc; i++) {
		arg_ptrs.write[i] = &p_args[i];
	}

	Callable::CallError ce;
	const Variant **arg_array = (argc > 0) ? const_cast<const Variant **>(arg_ptrs.ptr()) : nullptr;
	Variant ret = p_owner->callp(p_method, arg_array, argc, ce);

	get_limiter().end_execution();

	if (get_limiter().is_timeout_exceeded()) {
		r_error = "Sandbox timeout exceeded during call.";
		add_error("timeout", r_error);
		return Variant();
	}

	if (get_limiter().is_memory_limit_exceeded()) {
		r_error = "Sandbox memory limit exceeded during call.";
		add_error("memory", r_error);
		return Variant();
	}

	if (ce.error != Callable::CallError::CALL_OK) {
		const Variant **err_array = (argc > 0) ? const_cast<const Variant **>(arg_ptrs.ptr()) : nullptr;
		r_error = Variant::get_call_error_text(p_owner, p_method, err_array, argc, ce);
		add_error("gdscript", r_error);
		return Variant();
	}

	return ret;
}

void HMSandbox::add_error(const String &p_type,
		const String &p_message,
		const String &p_file,
		int p_line,
		int p_column,
		const String &p_stack_trace,
		const String &p_severity,
		const String &p_trigger_context,
		const String &p_phase) {
	get_error_registry().add_error(p_type, p_message, p_file, p_line, p_column, p_stack_trace, p_severity, p_trigger_context, p_phase);
}

String HMSandbox::get_last_error() const {
	return get_error_registry().get_last_error();
}

Array HMSandbox::get_all_errors() const {
	return get_error_registry().get_all_errors();
}

String HMSandbox::get_error_report_markdown() const {
	return get_error_registry().get_error_report_markdown();
}

void HMSandbox::unload() {
	// Remove all children (which cleans up the root node)
	set_root_node(nullptr);

	// Clear GDScript cache for PackedScene dependencies
	if (packed_scene.is_valid()) {
		// Get all resolved dependencies
		PackedStringArray deps = get_dependencies();

		// Clear GDScript cache for each HMScript dependency
		for (int i = 0; i < deps.size(); i++) {
			String actual_path = deps[i];

			if (actual_path.ends_with(".hm") || actual_path.ends_with(".hmc")) {
				// Remove script from GDScript cache
				GDScriptCache::remove_script(actual_path);
			}
		}

		// Clear the PackedScene reference
		set_packed_scene(Ref<PackedScene>());
	}
}

PackedStringArray HMSandbox::collect_dependencies(const String &p_directory) {
	PackedStringArray result;

	// Open the directory
	Ref<DirAccess> dir = DirAccess::open(p_directory);
	if (dir.is_null()) {
		ERR_PRINT("Failed to open directory: " + p_directory);
		return result;
	}

	// List all files and directories
	dir->list_dir_begin();
	String file_name = dir->get_next();

	while (!file_name.is_empty()) {
		// Skip hidden files and special directories
		if (file_name.begins_with(".")) {
			file_name = dir->get_next();
			continue;
		}

		String full_path = p_directory;
		if (!full_path.ends_with("/")) {
			full_path += "/";
		}
		full_path += file_name;

		if (dir->current_is_dir()) {
			// Recursively collect dependencies from subdirectory
			PackedStringArray sub_deps = collect_dependencies(full_path);
			for (int i = 0; i < sub_deps.size(); i++) {
				result.push_back(sub_deps[i]);
			}
		} else {
			// Check if file has .hm or .hmc extension
			if (file_name.ends_with(".hm") || file_name.ends_with(".hmc")) {
				result.push_back(full_path);
			}
		}

		file_name = dir->get_next();
	}

	dir->list_dir_end();
	return result;
}

String HMSandbox::generate_uuid() {
	// Generate a short UUID (8 hex characters)
	Ref<Crypto> crypto = Crypto::create();
	if (crypto.is_null()) {
		// Fallback if crypto is unavailable
		uint64_t ticks = OS::get_singleton()->get_ticks_usec();
		return String::num_int64(ticks & 0xFFFFFFFF, 16).pad_zeros(8);
	}

	PackedByteArray bytes = crypto->generate_random_bytes(4);
	if (bytes.size() != 4) {
		// Fallback if random generation fails
		uint64_t ticks = OS::get_singleton()->get_ticks_usec();
		return String::num_int64(ticks & 0xFFFFFFFF, 16).pad_zeros(8);
	}

	// Format as 8-character hex string
	String result;
	for (int i = 0; i < 4; i++) {
		result += String::num_int64(bytes[i], 16).pad_zeros(2);
	}
	return result;
}

HMSandbox *HMSandbox::load(const String &p_directory, const String &p_tscn_filename) {
	// Construct the full path
	String full_path = p_directory;
	if (!full_path.is_empty() && !full_path.ends_with("/")) {
		full_path += "/";
	}
	full_path += p_tscn_filename;

	// Load the resource without cache
	Error err = OK;
	Ref<Resource> resource = ResourceLoader::load(full_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);

	if (err != OK || resource.is_null()) {
		ERR_FAIL_V_MSG(nullptr, "Failed to load Resource: " + full_path);
	}
	// Cast to PackedScene
	Ref<PackedScene> scene = resource;
	if (scene.is_null()) {
		ERR_FAIL_V_MSG(nullptr, "Resource is not a PackedScene: " + full_path);
	}

	// Constructing Sandbox
	String profile_id = "Sandbox_" + generate_uuid();
	// Collect all .hm and .hmc dependencies from the directory
	PackedStringArray deps = collect_dependencies(p_directory);

	// Update all loaded .hm/.hmc scripts to use this sandbox's unique profile_id
	// This must happen after PackedScene is loaded (which loads all dependencies)
	// but before scene instantiation (which creates GDScriptInstances)
	for (int i = 0; i < deps.size(); i++) {
		const String &script_path = deps[i];

		// Try to get the already-loaded script from ResourceCache
		Ref<Resource> res = ResourceCache::get_ref(script_path);
		if (res.is_null()) {
			// Script not in cache yet, it will be loaded later
			continue;
		}

		// Cast to GDScript
		Ref<GDScript> gds = res;
		if (gds.is_null()) {
			// Not a GDScript resource
			continue;
		}

		// Update the script's sandbox profile_id from "hm_default" to this sandbox's unique ID
		gds->set_sandbox_enabled(true, profile_id);
	}

	// Ensure the GDScriptLanguage has a sandbox profile created for this ID
	// This will be used when the script instances execute
	GDScriptLanguage *lang = GDScriptLanguage::get_singleton();
	SandboxProfile *profile_ptr = nullptr;
	if (lang) {
		profile_ptr = lang->ensure_sandbox_profile(profile_id);
	}

	// Instantiate the scene to a node
	Node *instance = scene->instantiate();
	if (!instance) {
		ERR_FAIL_V_MSG(nullptr, "Failed to instantiate PackedScene: " + full_path);
	}

	// Create a new sandbox runtime with unique profile_id
	HMSandbox *sandbox = memnew(HMSandbox);

	sandbox->set_profile_id(profile_id);
	sandbox->set_name(profile_id);
	sandbox->set_profile(profile_ptr);

	sandbox->set_packed_scene(scene);
	sandbox->set_root_node(instance);
	sandbox->set_load_directory(p_directory);
	sandbox->set_scene_filename(p_tscn_filename);

	sandbox->set_dependencies(deps);

	// Register for frame callbacks if manager is available
	if (hm_sandbox_manager) {
		hm_sandbox_manager->register_sandbox(sandbox);
	}

	return sandbox;
}

} // namespace hmsandbox

