/**************************************************************************/
/*  sandbox_class_registry.cpp                                            */
/**************************************************************************/

#include "sandbox_class_registry.h"

#include "modules/gdscript/gdscript.h"

namespace hmsandbox {

bool SandboxClassRegistry::_has_circular_dependency(const ClassInfo &p_info) const {
	// Check if adding this class would create a circular dependency
	String current_base = p_info.base_type;
	HashSet<String> visited;
	visited.insert(p_info.class_name);

	// Traverse the inheritance chain
	while (!current_base.is_empty()) {
		// If we've seen this class before, we have a cycle
		if (visited.has(current_base)) {
			return true;
		}
		visited.insert(current_base);

		// If the base class is not in this sandbox registry, it's external (safe)
		if (!has_class(current_base)) {
			break;
		}

		// Move to the next base class in the chain
		current_base = get_class_info(current_base).base_type;
	}

	return false;
}

bool SandboxClassRegistry::register_class(const ClassInfo &p_info) {
	if (p_info.class_name.is_empty()) {
		ERR_PRINT("Cannot register class with empty name.");
		return false;
	}

	if (p_info.script_path.is_empty()) {
		ERR_PRINT(vformat("Cannot register class '%s' with empty script path.", p_info.class_name));
		return false;
	}

	// Check for circular dependencies
	if (_has_circular_dependency(p_info)) {
		ERR_PRINT(vformat(
				"Circular dependency detected: Cannot register class '%s' (base: '%s'). "
				"This would create an inheritance cycle.",
				p_info.class_name,
				p_info.base_type));
		return false;
	}

	// If this script path was already registered under a different name, remove the old registration
	if (path_to_name.has(p_info.script_path)) {
		String old_name = path_to_name[p_info.script_path];
		if (old_name != p_info.class_name) {
			print_verbose(vformat(
					"Script '%s' was previously registered as '%s', now registering as '%s'.",
					p_info.script_path,
					old_name,
					p_info.class_name));
			name_to_class.erase(old_name);
		}
	}

	// Register the class
	name_to_class[p_info.class_name] = p_info;
	path_to_name[p_info.script_path] = p_info.class_name;

	print_verbose(vformat(
			"Registered sandbox class: '%s' from '%s' (base: '%s')",
			p_info.class_name,
			p_info.script_path,
			p_info.base_type.is_empty() ? "none" : p_info.base_type));

	return true;
}

void SandboxClassRegistry::unregister_class(const String &p_class_name) {
	if (!name_to_class.has(p_class_name)) {
		return;
	}

	ClassInfo info = name_to_class[p_class_name];

	// Remove from both maps
	name_to_class.erase(p_class_name);
	path_to_name.erase(info.script_path);

	print_verbose(vformat("Unregistered sandbox class: '%s'", p_class_name));
}

void SandboxClassRegistry::clear() {
	print_verbose(vformat("Clearing sandbox class registry (%d classes)", name_to_class.size()));

	name_to_class.clear();
	path_to_name.clear();
}

bool SandboxClassRegistry::has_class(const String &p_class_name) const {
	return name_to_class.has(p_class_name);
}

SandboxClassRegistry::ClassInfo SandboxClassRegistry::get_class_info(const String &p_class_name) const {
	if (!name_to_class.has(p_class_name)) {
		return ClassInfo();
	}
	return name_to_class[p_class_name];
}

Ref<GDScript> SandboxClassRegistry::get_class_script(const String &p_class_name) const {
	if (!name_to_class.has(p_class_name)) {
		return Ref<GDScript>();
	}
	return name_to_class[p_class_name].cached_script;
}

bool SandboxClassRegistry::has_script_path(const String &p_path) const {
	return path_to_name.has(p_path);
}

String SandboxClassRegistry::get_class_name_for_path(const String &p_path) const {
	if (!path_to_name.has(p_path)) {
		return String();
	}
	return path_to_name[p_path];
}

PackedStringArray SandboxClassRegistry::get_all_class_names() const {
	PackedStringArray result;
	result.resize(name_to_class.size());

	int idx = 0;
	for (const KeyValue<String, ClassInfo> &E : name_to_class) {
		result.write[idx++] = E.key;
	}

	return result;
}

void SandboxClassRegistry::print_registry() const {
	print_line(vformat("=== Sandbox Class Registry (%d classes) ===", name_to_class.size()));

	if (name_to_class.is_empty()) {
		print_line("  (empty)");
		return;
	}

	for (const KeyValue<String, ClassInfo> &E : name_to_class) {
		const ClassInfo &info = E.value;
		print_line(vformat(
				"  - %s: %s (base: %s)%s%s",
				info.class_name,
				info.script_path,
				info.base_type.is_empty() ? "none" : info.base_type,
				info.is_abstract ? " [abstract]" : "",
				info.is_tool ? " [tool]" : ""));
	}
}

} // namespace hmsandbox
