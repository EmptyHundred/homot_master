/**************************************************************************/
/*  workspace.cpp                                                         */
/**************************************************************************/
/*  Shared utilities for script collection, class_name scanning, and      */
/*  global class registration.                                            */
/**************************************************************************/

#ifdef HOMOT

#include "workspace.h"

#include "stubs/linterdb.h"
#include "stubs/script_server_stub.h"

using linter::LinterDB;

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"

using linter::LinterDB;
using linter::ScriptServerStub;

namespace workspace {

void collect_scripts(const String &p_dir, Vector<String> &r_scripts) {
	Ref<DirAccess> da = DirAccess::open(p_dir);
	if (da.is_null()) {
		return;
	}

	da->list_dir_begin();
	String file = da->get_next();
	while (!file.is_empty()) {
		if (da->current_is_dir()) {
			if (file != "." && file != "..") {
				collect_scripts(p_dir.path_join(file), r_scripts);
			}
		} else {
			if (is_script_file(file)) {
				r_scripts.push_back(p_dir.path_join(file));
			}
		}
		file = da->get_next();
	}
	da->list_dir_end();
}

bool is_script_file(const String &p_path) {
	String ext = p_path.get_extension().to_lower();
	return ext == "gd" || ext == "hm" || ext == "hmc";
}

bool is_resource_file(const String &p_path) {
	String ext = p_path.get_extension().to_lower();
	return ext == "tscn" || ext == "tres";
}

bool is_shader_file(const String &p_path) {
	String ext = p_path.get_extension().to_lower();
	return ext == "gdshader";
}

bool is_lintable_file(const String &p_path) {
	return is_script_file(p_path) || is_resource_file(p_path) || is_shader_file(p_path);
}

void collect_all_files(const String &p_dir, Vector<String> &r_scripts, Vector<String> &r_resources, Vector<String> &r_shaders) {
	Ref<DirAccess> da = DirAccess::open(p_dir);
	if (da.is_null()) {
		return;
	}

	da->list_dir_begin();
	String file = da->get_next();
	while (!file.is_empty()) {
		if (da->current_is_dir()) {
			if (file != "." && file != "..") {
				collect_all_files(p_dir.path_join(file), r_scripts, r_resources, r_shaders);
			}
		} else {
			String path = p_dir.path_join(file);
			if (is_script_file(file)) {
				r_scripts.push_back(path);
			} else if (is_resource_file(file)) {
				r_resources.push_back(path);
			} else if (is_shader_file(file)) {
				r_shaders.push_back(path);
			}
		}
		file = da->get_next();
	}
	da->list_dir_end();
}

String extract_class_name(const String &p_source) {
	int pos = 0;
	for (int line = 0; line < 50 && pos < p_source.length(); line++) {
		int end = p_source.find("\n", pos);
		if (end == -1) {
			end = p_source.length();
		}
		String line_str = p_source.substr(pos, end - pos).strip_edges();
		pos = end + 1;

		if (line_str.begins_with("class_name")) {
			String rest = line_str.substr(10).strip_edges();
			String name;
			for (int i = 0; i < rest.length(); i++) {
				char32_t c = rest[i];
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
					name += c;
				} else {
					break;
				}
			}
			if (!name.is_empty()) {
				return name;
			}
		}
	}
	return String();
}

String extract_extends(const String &p_source) {
	int pos = 0;
	for (int line = 0; line < 50 && pos < p_source.length(); line++) {
		int end = p_source.find("\n", pos);
		if (end == -1) {
			end = p_source.length();
		}
		String line_str = p_source.substr(pos, end - pos).strip_edges();
		pos = end + 1;

		if (line_str.begins_with("extends")) {
			String rest = line_str.substr(7).strip_edges();
			String name;
			for (int i = 0; i < rest.length(); i++) {
				char32_t c = rest[i];
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
					name += c;
				} else {
					break;
				}
			}
			if (!name.is_empty()) {
				return name;
			}
		}
	}
	return "RefCounted";
}

StringName resolve_native_base(const String &p_extends, const HashMap<String, String> &p_class_to_extends) {
	String current = p_extends;
	HashSet<String> visited;
	while (!current.is_empty() && !visited.has(current)) {
		visited.insert(current);
		if (ClassDB::class_exists(StringName(current))) {
			return StringName(current);
		}
		LinterDB *db = LinterDB::get_singleton();
		if (db && db->class_exists(StringName(current))) {
			return StringName(current);
		}
		auto it = p_class_to_extends.find(current);
		if (it) {
			current = it->value;
		} else {
			break;
		}
	}
	return StringName("RefCounted");
}

void scan_and_register_classes(const Vector<String> &p_script_paths,
		HashMap<String, String> &r_class_to_path,
		HashMap<String, String> &r_class_to_extends) {
	r_class_to_path.clear();
	r_class_to_extends.clear();

	for (const String &path : p_script_paths) {
		String source = FileAccess::get_file_as_string(path);
		String cname = extract_class_name(source);
		if (!cname.is_empty()) {
			r_class_to_path[cname] = path;
			r_class_to_extends[cname] = extract_extends(source);
		}
	}

	// Note: register_classes is additive — it does not clear existing registrations.
	// Call ScriptServerStub::clear() explicitly before the first scan if needed.
	register_classes(r_class_to_path, r_class_to_extends);
}

void register_classes(const HashMap<String, String> &p_class_to_path,
		const HashMap<String, String> &p_class_to_extends) {
	for (const KeyValue<String, String> &kv : p_class_to_path) {
		StringName native_base = resolve_native_base(
				p_class_to_extends.has(kv.key) ? p_class_to_extends[kv.key] : "RefCounted",
				p_class_to_extends);
		ScriptServerStub::register_global_class(StringName(kv.key), kv.value, native_base);
	}
}

// ---------------------------------------------------------------------------
// Project context loading
// ---------------------------------------------------------------------------

Vector<AutoloadEntry> parse_autoloads(const String &p_project_godot_path) {
	Vector<AutoloadEntry> result;

	String content = FileAccess::get_file_as_string(p_project_godot_path);
	if (content.is_empty()) {
		return result;
	}

	bool in_autoload_section = false;
	Vector<String> lines = content.split("\n");

	for (const String &raw_line : lines) {
		String line = raw_line.strip_edges();

		// Section header.
		if (line.begins_with("[")) {
			in_autoload_section = (line == "[autoload]");
			continue;
		}

		if (!in_autoload_section || line.is_empty() || line.begins_with(";")) {
			continue;
		}

		// Format: Name="*res://path/to/script.gd"  or  Name="*uid://..."
		int eq = line.find("=");
		if (eq == -1) {
			continue;
		}

		String name = line.substr(0, eq).strip_edges();
		String value = line.substr(eq + 1).strip_edges();

		// Strip quotes.
		if (value.begins_with("\"") && value.ends_with("\"")) {
			value = value.substr(1, value.length() - 2);
		}

		AutoloadEntry entry;
		entry.name = name;

		// Leading "*" means it's a singleton.
		if (value.begins_with("*")) {
			entry.is_singleton = true;
			value = value.substr(1);
		}

		// Skip uid:// references — we can't resolve them without the .godot/uid_cache.
		if (value.begins_with("uid://")) {
			// Still register the singleton name even if we can't resolve the path.
			entry.path = "";
			result.push_back(entry);
			continue;
		}

		entry.path = value;
		result.push_back(entry);
	}

	return result;
}

int load_project_context(const String &p_project_root) {
	// 1. Collect all scripts in the project.
	Vector<String> scripts;
	collect_scripts(p_project_root, scripts);

	// 2. Scan and register class_name declarations.
	HashMap<String, String> class_to_path;
	HashMap<String, String> class_to_extends;
	if (!scripts.is_empty()) {
		scan_and_register_classes(scripts, class_to_path, class_to_extends);
	}

	// 3. Parse autoloads and register as singletons.
	String project_godot = p_project_root.path_join("project.godot");
	Vector<AutoloadEntry> autoloads = parse_autoloads(project_godot);

	LinterDB *db = LinterDB::get_singleton();
	for (const AutoloadEntry &entry : autoloads) {
		if (entry.is_singleton && db) {
			db->add_singleton(StringName(entry.name));
		}
	}

	return class_to_path.size();
}

} // namespace workspace

#endif // HOMOT
