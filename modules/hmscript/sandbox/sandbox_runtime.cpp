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

	ClassDB::bind_method(D_METHOD("get_local_classes"), &HMSandbox::get_local_classes);

	// ClassDB::bind_method(D_METHOD("get_dependency_script", "path"), &HMSandbox::get_dependency_script);

	// ClassDB::bind_method(D_METHOD("resolve_dependencies"), &HMSandbox::resolve_dependencies);

	// ClassDB::bind_method(D_METHOD("register_classes"), &HMSandbox::register_classes);

	// ClassDB::bind_method(D_METHOD("configure_script_profiles"), &HMSandbox::configure_script_profiles);

	ClassDB::bind_static_method("HMSandbox", D_METHOD("collect_dependencies", "dir_path"), &HMSandbox::collect_dependencies);

	ClassDB::bind_method(D_METHOD("unload"), &HMSandbox::unload);
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
	// Clear existing dependencies
	dependencies.clear();

	// Populate the map with paths (scripts will be loaded later)
	for (int i = 0; i < p_dependencies.size(); i++) {
		dependencies.insert(p_dependencies[i], Ref<GDScript>());
	}
}

PackedStringArray HMSandbox::get_dependencies() const {
	PackedStringArray result;
	result.resize(dependencies.size());

	int idx = 0;
	for (const KeyValue<String, Ref<GDScript>> &E : dependencies) {
		result.write[idx++] = E.key;
	}

	return result;
}

Ref<GDScript> HMSandbox::get_dependency_script(const String &p_path) const {
	HashMap<String, Ref<GDScript>>::ConstIterator it = dependencies.find(p_path);
	if (it) {
		return it->value;
	}
	return Ref<GDScript>();
}

bool HMSandbox::has_local_class(const String &p_class_name) const {
	return class_registry.has_class(p_class_name);
}

Ref<GDScript> HMSandbox::lookup_local_class(const String &p_class_name) const {
	return class_registry.get_class_script(p_class_name);
}

Dictionary HMSandbox::get_local_classes() const {
	return class_registry.get_all_classes();
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
	class_registry.clear();

	// Remove all children (which cleans up the root node)
	set_root_node(nullptr);

	// Clear GDScript cache for all dependencies
	for (const KeyValue<String, Ref<GDScript>> &E : dependencies) {
		const String &script_path = E.key;

		if (script_path.ends_with(".hm") || script_path.ends_with(".hmc")) {
			// Remove script from GDScript cache
			GDScriptCache::remove_script(script_path);
		}
	}

	// Clear the dependencies map
	dependencies.clear();

	// Clear the PackedScene reference
	if (packed_scene.is_valid()) {
		set_packed_scene(Ref<PackedScene>());
	}
}

// Helper function for recursive directory scanning
static void _collect_dependencies_recursive(const String &p_directory, PackedStringArray &r_result) {
	Ref<DirAccess> dir = DirAccess::open(p_directory);
	if (dir.is_null()) {
		WARN_PRINT("Failed to open directory: " + p_directory);
		return;
	}

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
			_collect_dependencies_recursive(full_path, r_result);
		} else {
			// Check if file has .hm or .hmc extension
			if (file_name.ends_with(".hm") || file_name.ends_with(".hmc")) {
				r_result.push_back(full_path);
			}
		}

		file_name = dir->get_next();
	}

	dir->list_dir_end();
}

PackedStringArray HMSandbox::collect_dependencies(const String &p_dir_path) {
	PackedStringArray result;

	if (p_dir_path.is_empty()) {
		WARN_PRINT("Cannot collect dependencies: directory path is empty.");
		return result;
	}

	// Recursively collect all .hm and .hmc files from the directory
	_collect_dependencies_recursive(p_dir_path, result);

	return result;
}

void HMSandbox::resolve_dependencies() {
	// Clear existing dependencies
	dependencies.clear();

	if (load_directory.is_empty()) {
		WARN_PRINT("Cannot resolve dependencies: load_directory is empty.");
		return;
	}

	// Collect all .hm/.hmc paths and pre-load all scripts
	PackedStringArray paths = collect_dependencies(load_directory);
	int loaded_count = 0;

	for (int i = 0; i < paths.size(); i++) {
		const String &script_path = paths[i];

		// Pre-load the script
		Ref<GDScript> script = ResourceLoader::load(
				script_path,
				"",
				ResourceFormatLoader::CACHE_MODE_REUSE);

		if (script.is_valid()) {
			dependencies.insert(script_path, script);
			loaded_count++;
		} else {
			WARN_PRINT(vformat("Failed to load dependency script: %s", script_path));
			// Still insert with null to track that we attempted to load it
			dependencies.insert(script_path, Ref<GDScript>());
		}
	}

	print_verbose(vformat(
			"Sandbox dependency resolution: Loaded %d/%d scripts from '%s'",
			loaded_count,
			paths.size(),
			load_directory));
}

void HMSandbox::configure_script_profiles() {
	if (profile_id.is_empty()) {
		WARN_PRINT("Cannot configure script profiles: profile_id is empty.");
		return;
	}

	int configured_count = 0;

	for (KeyValue<String, Ref<GDScript>> &E : dependencies) {
		// Skip scripts that failed to load
		if (E.value.is_null()) {
			continue;
		}

		// Update the script's sandbox profile_id from "hm_default" to this sandbox's unique ID
		E.value->set_sandbox_enabled(true, profile_id);
		configured_count++;
	}

	print_verbose(vformat(
			"Sandbox '%s': Configured sandbox profile for %d/%d dependency scripts",
			profile_id,
			configured_count,
			dependencies.size()));
}

String HMSandbox::generate_sandbox_id() {
	// Generate a unique sandbox ID with "Sandbox_" prefix + 8 hex characters
	String uuid;

	Ref<Crypto> crypto = Crypto::create();
	if (crypto.is_null()) {
		// Fallback if crypto is unavailable
		uint64_t ticks = OS::get_singleton()->get_ticks_usec();
		uuid = String::num_int64(ticks & 0xFFFFFFFF, 16).pad_zeros(8);
		return "Sandbox_" + uuid;
	}

	PackedByteArray bytes = crypto->generate_random_bytes(4);
	if (bytes.size() != 4) {
		// Fallback if random generation fails
		uint64_t ticks = OS::get_singleton()->get_ticks_usec();
		uuid = String::num_int64(ticks & 0xFFFFFFFF, 16).pad_zeros(8);
		return "Sandbox_" + uuid;
	}

	// Format as 8-character hex string
	for (int i = 0; i < 4; i++) {
		uuid += String::num_int64(bytes[i], 16).pad_zeros(2);
	}
	return "Sandbox_" + uuid;
}

void HMSandbox::register_classes() {
	class_registry.clear();

	GDScriptLanguage *lang = GDScriptLanguage::get_singleton();
	if (!lang) {
		ERR_PRINT("GDScriptLanguage not available for class registration.");
		return;
	}

	int registered_count = 0;

	for (const KeyValue<String, Ref<GDScript>> &E : dependencies) {
		const String &script_path = E.key;

		// Skip scripts that failed to load
		if (E.value.is_null()) {
			continue;
		}

		// Extract class info using GDScriptLanguage::get_global_class_name()
		String base_type, icon_path;
		bool is_abstract = false, is_tool = false;

		String class_name = lang->get_global_class_name(
				script_path,
				&base_type,
				&icon_path,
				&is_abstract,
				&is_tool);

		// Only register scripts with class_name declarations
		if (!class_name.is_empty()) {
			// Register in sandbox-local registry
			SandboxClassRegistry::ClassInfo info;
			info.class_name = class_name;
			info.script_path = script_path;
			info.base_type = base_type;
			info.icon_path = icon_path;
			info.is_abstract = is_abstract;
			info.is_tool = is_tool;
			info.cached_script = E.value;

			bool registered = class_registry.register_class(info);

			if (registered) {
				registered_count++;
				print_verbose(vformat(
						"Sandbox '%s': Registered local class '%s' from '%s' (base: '%s')",
						profile_id,
						class_name,
						script_path,
						base_type.is_empty() ? "none" : base_type));
			}
		}
	}

	print_verbose(vformat(
			"Sandbox '%s': Registered %d local classes from %d dependencies",
			profile_id,
			registered_count,
			dependencies.size()));
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
	String profile_id = generate_sandbox_id();

	// Create sandbox instance early so we can populate its registry
	HMSandbox *sandbox = memnew(HMSandbox);
	
	sandbox->set_profile_id(profile_id);
	sandbox->set_name(profile_id);
	sandbox->set_load_directory(p_directory);
	sandbox->set_scene_filename(p_tscn_filename);

	// Resolve all .hm and .hmc dependencies from the load directory
	// Preload all dependencies to dependencies map
	sandbox->resolve_dependencies();

	// Configure all loaded scripts to use this sandbox's profile
	// This must happen after PackedScene is loaded (which loads all dependencies)
	// but before scene instantiation (which creates GDScriptInstances)
	sandbox->configure_script_profiles();

	// Register all classes from dependencies
	// This MUST happen before scene instantiation so that scripts can find their base classes
	sandbox->register_classes();

	// Instantiate the scene to a node
	Node *instance = scene->instantiate();
	if (!instance) {
		memdelete(sandbox); // Clean up sandbox if instantiation fails
		ERR_FAIL_V_MSG(nullptr, "Failed to instantiate PackedScene: " + full_path);
	}

	// Ensure the GDScriptLanguage has a sandbox profile created for this ID
	// This will be used when the script instances execute
	GDScriptLanguage *lang = GDScriptLanguage::get_singleton();
	SandboxProfile *profile_ptr = nullptr;
	if (lang) {
		profile_ptr = lang->ensure_sandbox_profile(profile_id);
	}

	// Configure the sandbox with the profile
	sandbox->set_profile(profile_ptr);

	sandbox->set_packed_scene(scene);
	sandbox->set_root_node(instance);

	// Register for frame callbacks if manager is available
	if (hm_sandbox_manager) {
		hm_sandbox_manager->register_sandbox(sandbox);
	}

	return sandbox;
}

} // namespace hmsandbox

