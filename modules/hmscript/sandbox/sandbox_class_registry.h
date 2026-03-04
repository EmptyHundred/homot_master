/**************************************************************************/
/*  sandbox_class_registry.h                                              */
/**************************************************************************/

#pragma once

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/variant/variant.h"

class GDScript;

namespace hmsandbox {

// Local class registry for a single sandbox instance.
// Each sandbox maintains its own isolated namespace to prevent class name conflicts.
class SandboxClassRegistry {
public:
	struct ClassInfo {
		String class_name; // Local class name (e.g., "Enemy")
		String script_path; // Full path to script file
		String base_type; // Base class name (may be local or global)
		String icon_path; // Optional icon path
		bool is_abstract = false;
		bool is_tool = false;
		Ref<GDScript> cached_script; // Keep script loaded

		ClassInfo() = default;
	};

private:
	HashMap<String, ClassInfo> name_to_class; // class_name -> ClassInfo
	HashMap<String, String> path_to_name; // script_path -> class_name

	// Helper: Check for circular dependencies
	bool _has_circular_dependency(const ClassInfo &p_info) const;

public:
	SandboxClassRegistry() = default;
	~SandboxClassRegistry() = default;

	// Register a class in this sandbox
	// Returns true if successful, false if circular dependency detected
	bool register_class(const ClassInfo &p_info);

	// Unregister a specific class by name
	void unregister_class(const String &p_class_name);

	// Clear all registrations
	void clear();

	// Lookup by class name
	bool has_class(const String &p_class_name) const;
	ClassInfo get_class_info(const String &p_class_name) const;
	Ref<GDScript> get_class_script(const String &p_class_name) const;

	// Lookup by script path
	bool has_script_path(const String &p_path) const;
	String get_class_name_for_path(const String &p_path) const;

	// Get all registered classes
	PackedStringArray get_all_class_names() const;

	// Get number of registered classes
	int get_class_count() const { return name_to_class.size(); }

	// Debug: Print all registered classes
	void print_registry() const;
};

} // namespace hmsandbox
