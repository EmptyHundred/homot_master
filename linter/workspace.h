/**************************************************************************/
/*  workspace.h                                                           */
/**************************************************************************/
/*  Shared utilities for script collection, class_name scanning, and      */
/*  global class registration. Used by lint, LSP, and LSPA.              */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

namespace workspace {

// Recursively collect script files (.gd, .hm, .hmc) from a directory.
void collect_scripts(const String &p_dir, Vector<String> &r_scripts);

// Recursively collect all lintable files (.gd, .hm, .hmc, .tscn, .tres, .gdshader) from a directory.
void collect_all_files(const String &p_dir, Vector<String> &r_scripts, Vector<String> &r_resources, Vector<String> &r_shaders);

// Check if a file path is a script file.
bool is_script_file(const String &p_path);

// Check if a file path is a resource file (.tscn, .tres).
bool is_resource_file(const String &p_path);

// Check if a file path is a shader file (.gdshader).
bool is_shader_file(const String &p_path);

// Check if a file path is any lintable file.
bool is_lintable_file(const String &p_path);

// Lightweight class_name extraction from source without full parsing.
String extract_class_name(const String &p_source);

// Extract `extends <Type>` from source.
String extract_extends(const String &p_source);

// Resolve a script class's native base by walking the extends chain.
StringName resolve_native_base(const String &p_extends, const HashMap<String, String> &p_class_to_extends);

// Pre-scan scripts and register global classes with ScriptServerStub.
void scan_and_register_classes(const Vector<String> &p_script_paths,
		HashMap<String, String> &r_class_to_path,
		HashMap<String, String> &r_class_to_extends);

// Register global classes from already-scanned data.
void register_classes(const HashMap<String, String> &p_class_to_path,
		const HashMap<String, String> &p_class_to_extends);

// Autoload entry parsed from project.godot.
struct AutoloadEntry {
	String name;
	String path; // Absolute path to the script/scene.
	bool is_singleton = false; // true if prefixed with "*".
};

// Parse project.godot [autoload] section.
Vector<AutoloadEntry> parse_autoloads(const String &p_project_godot_path);

// Load project context: scan all scripts, register class_names and autoloads.
// p_project_root is the directory containing project.godot.
// Returns the number of global classes registered.
int load_project_context(const String &p_project_root);

} // namespace workspace

#endif // HOMOT
