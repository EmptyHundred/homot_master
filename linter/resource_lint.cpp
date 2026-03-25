/**************************************************************************/
/*  resource_lint.cpp                                                     */
/**************************************************************************/
/*  Linter for .tscn and .tres files.                                     */
/*  Parses the Godot text resource format and validates:                  */
/*    - File header (gd_scene / gd_resource)                              */
/*    - Section tags: ext_resource, sub_resource, node, resource          */
/*    - Node/resource type existence in linterdb                          */
/*    - Property names exist on the declared type or attached script      */
/*    - Duplicate node names under the same parent                        */
/*    - ExtResource/SubResource reference validity                        */
/**************************************************************************/

#ifdef HOMOT

#include "resource_lint.h"
#include "stubs/linterdb.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

using linter::LinterDB;

namespace resource_lint {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static String _extract_quoted(const String &p_str, const String &p_key) {
	String prefix = p_key + "=\"";
	int pos = p_str.find(prefix);
	if (pos == -1) {
		return String();
	}
	pos += prefix.length();
	int end = p_str.find("\"", pos);
	if (end == -1) {
		return String();
	}
	return p_str.substr(pos, end - pos);
}

static String _extract_value(const String &p_str, const String &p_key) {
	String quoted = _extract_quoted(p_str, p_key);
	if (!quoted.is_empty()) {
		return quoted;
	}
	String prefix = p_key + "=";
	int pos = p_str.find(prefix);
	if (pos == -1) {
		return String();
	}
	pos += prefix.length();
	int end = pos;
	while (end < p_str.length() && p_str[end] != ' ' && p_str[end] != ']' && p_str[end] != '\n') {
		end++;
	}
	return p_str.substr(pos, end - pos);
}

static bool _is_section(const String &p_line, String &r_tag) {
	String stripped = p_line.strip_edges();
	if (stripped.length() < 2 || stripped[0] != '[' || stripped[stripped.length() - 1] != ']') {
		return false;
	}
	String inner = stripped.substr(1, stripped.length() - 2).strip_edges();
	int space = inner.find(" ");
	r_tag = (space != -1) ? inner.substr(0, space) : inner;
	return true;
}

// ---------------------------------------------------------------------------
// Script @export extraction
// ---------------------------------------------------------------------------

struct ExtResourceInfo {
	String type;
	String path;
};

// Extract @export var names from a GDScript source string.
// Looks for patterns: @export var name, @export_* var name
static void _extract_script_exports(const String &p_source, HashSet<String> &r_exports) {
	int pos = 0;
	while (pos < p_source.length()) {
		int end = p_source.find("\n", pos);
		if (end == -1) {
			end = p_source.length();
		}
		String line = p_source.substr(pos, end - pos).strip_edges();
		pos = end + 1;

		// Match lines starting with @export (covers @export, @export_range, @export_enum, etc.)
		if (!line.begins_with("@export")) {
			continue;
		}

		// Find "var" keyword after the annotation.
		// There may be multiple annotations on separate lines before var, but
		// in typical usage @export and var are on the same line or @export is
		// immediately followed by var on the next line.
		int var_pos = line.find("var ");
		if (var_pos == -1) {
			// Annotation might be on a separate line; peek at the next line.
			// But for simplicity, also scan next few lines.
			for (int lookahead = 0; lookahead < 3 && pos < p_source.length(); lookahead++) {
				int next_end = p_source.find("\n", pos);
				if (next_end == -1) {
					next_end = p_source.length();
				}
				String next_line = p_source.substr(pos, next_end - pos).strip_edges();
				if (next_line.begins_with("var ")) {
					line = next_line;
					var_pos = 0;
					pos = next_end + 1;
					break;
				}
				if (next_line.begins_with("@")) {
					// Another annotation — keep looking.
					pos = next_end + 1;
					continue;
				}
				break;
			}
			if (var_pos == -1) {
				continue;
			}
		}

		// Extract variable name after "var ".
		String after_var = line.substr(var_pos + 4).strip_edges();
		String var_name;
		for (int j = 0; j < after_var.length(); j++) {
			char32_t c = after_var[j];
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
				var_name += c;
			} else {
				break;
			}
		}
		if (!var_name.is_empty()) {
			r_exports.insert(var_name);
		}
	}
}

// Resolve a script path from ext_resource relative to the tscn/tres file.
static String _resolve_script_path(const String &p_script_path, const String &p_resource_dir) {
	String script_path = p_script_path;
	bool had_res_prefix = false;
	// Strip res:// prefix if present.
	if (script_path.begins_with("res://")) {
		script_path = script_path.substr(6);
		had_res_prefix = true;
	}
	// Try as absolute path first.
	if (FileAccess::exists(script_path)) {
		return script_path;
	}
	// Resolve relative to project root (if --project was set and path was res://).
	if (had_res_prefix) {
		String project_root = ProjectSettings::get_singleton()->get_resource_path();
		if (!project_root.is_empty()) {
			String resolved = project_root.path_join(script_path);
			if (FileAccess::exists(resolved)) {
				return resolved;
			}
		}
	}
	// Resolve relative to the resource file's directory.
	if (!p_resource_dir.is_empty()) {
		String resolved = p_resource_dir.path_join(script_path);
		if (FileAccess::exists(resolved)) {
			return resolved;
		}
	}
	return String();
}

// ---------------------------------------------------------------------------
// Main lint logic
// ---------------------------------------------------------------------------

static LintResult _lint_content(const String &p_content, const String &p_filename) {
	LintResult result;
	LinterDB *db = LinterDB::get_singleton();

	// Compute directory of the resource file for relative path resolution.
	String resource_dir;
	if (!p_filename.is_empty()) {
		int last_slash = p_filename.rfind("/");
		if (last_slash == -1) {
			last_slash = p_filename.rfind("\\");
		}
		if (last_slash != -1) {
			resource_dir = p_filename.substr(0, last_slash);
		}
	}

	Vector<String> lines = p_content.split("\n");
	if (lines.is_empty()) {
		Diagnostic d;
		d.file = p_filename;
		d.line = 1;
		d.severity = "error";
		d.message = "Empty file.";
		result.diagnostics.push_back(d);
		result.errors++;
		return result;
	}

	// --- Parse header ---
	String first_line = lines[0].strip_edges();
	bool is_scene = first_line.begins_with("[gd_scene");
	bool is_resource = first_line.begins_with("[gd_resource");
	if (!is_scene && !is_resource) {
		Diagnostic d;
		d.file = p_filename;
		d.line = 1;
		d.severity = "error";
		d.message = "Invalid header: expected [gd_scene ...] or [gd_resource ...].";
		result.diagnostics.push_back(d);
		result.errors++;
		return result;
	}

	// Track ext_resource and sub_resource IDs.
	HashMap<String, ExtResourceInfo> ext_resource_ids; // id -> {type, path}
	HashMap<String, String> sub_resource_ids; // id -> type

	// Track node names per parent for duplicate detection.
	HashMap<String, HashSet<String>> parent_children;

	// Current section state.
	String current_section_tag;
	String current_section_type;
	int current_section_line = 0;
	// Script exports for the current node (populated when script = ExtResource is seen).
	HashSet<String> current_script_exports;
	bool current_has_script = false;

	for (int i = 0; i < lines.size(); i++) {
		int line_num = i + 1;
		String line = lines[i].strip_edges();

		if (line.is_empty()) {
			continue;
		}

		// Check for section header.
		String tag;
		if (_is_section(line, tag)) {
			current_section_tag = tag;
			current_section_line = line_num;
			current_section_type = "";
			current_script_exports.clear();
			current_has_script = false;

			if (tag == "gd_scene" || tag == "gd_resource") {
				continue;
			}

			if (tag == "ext_resource") {
				String type = _extract_quoted(line, "type");
				String id = _extract_quoted(line, "id");
				String path = _extract_quoted(line, "path");
				if (!id.is_empty()) {
					ExtResourceInfo info;
					info.type = type;
					info.path = path;
					ext_resource_ids[id] = info;
				}
				if (type.is_empty()) {
					Diagnostic d;
					d.file = p_filename;
					d.line = line_num;
					d.severity = "warning";
					d.message = "ext_resource missing 'type' attribute.";
					result.diagnostics.push_back(d);
					result.warnings++;
				}
				continue;
			}

			if (tag == "sub_resource") {
				String type = _extract_quoted(line, "type");
				String id = _extract_quoted(line, "id");
				if (!id.is_empty()) {
					sub_resource_ids[id] = type;
				}
				if (type.is_empty()) {
					Diagnostic d;
					d.file = p_filename;
					d.line = line_num;
					d.severity = "error";
					d.message = "sub_resource missing 'type' attribute.";
					result.diagnostics.push_back(d);
					result.errors++;
				} else if (db && !db->class_exists(StringName(type))) {
					Diagnostic d;
					d.file = p_filename;
					d.line = line_num;
					d.severity = "error";
					d.message = vformat("Unknown resource type: \"%s\".", type);
					result.diagnostics.push_back(d);
					result.errors++;
				}
				current_section_type = type;
				continue;
			}

			if (tag == "node") {
				String type = _extract_quoted(line, "type");
				String name = _extract_quoted(line, "name");
				String parent = _extract_quoted(line, "parent");

				if (!type.is_empty() && db && !db->class_exists(StringName(type))) {
					Diagnostic d;
					d.file = p_filename;
					d.line = line_num;
					d.severity = "error";
					d.message = vformat("Unknown node type: \"%s\".", type);
					result.diagnostics.push_back(d);
					result.errors++;
				}

				if (!name.is_empty()) {
					String parent_key = parent.is_empty() ? "__root__" : parent;
					if (parent_children[parent_key].has(name)) {
						Diagnostic d;
						d.file = p_filename;
						d.line = line_num;
						d.severity = "error";
						d.message = vformat("Duplicate node name \"%s\" under parent \"%s\".", name, parent.is_empty() ? "(root)" : parent);
						result.diagnostics.push_back(d);
						result.errors++;
					} else {
						parent_children[parent_key].insert(name);
					}
				}

				current_section_type = type;
				continue;
			}

			if (tag == "resource") {
				String resource_type = _extract_quoted(first_line, "type");
				current_section_type = resource_type;
				if (!resource_type.is_empty() && db && !db->class_exists(StringName(resource_type))) {
					Diagnostic d;
					d.file = p_filename;
					d.line = 1;
					d.severity = "error";
					d.message = vformat("Unknown resource type: \"%s\".", resource_type);
					result.diagnostics.push_back(d);
					result.errors++;
				}
				continue;
			}

			if (tag == "connection") {
				continue;
			}

			Diagnostic d;
			d.file = p_filename;
			d.line = line_num;
			d.severity = "warning";
			d.message = vformat("Unknown section tag: [%s].", tag);
			result.diagnostics.push_back(d);
			result.warnings++;
			continue;
		}

		// --- Property line: key = value ---

		if (line.contains("=")) {
			int eq_pos = line.find("=");
			if (eq_pos > 0) {
				String prop_name = line.substr(0, eq_pos).strip_edges();

				// Detect script attachment: script = ExtResource("id")
				if (prop_name == "script" && line.contains("ExtResource(\"")) {
					int start = line.find("ExtResource(\"") + 13;
					int end = line.find("\"", start);
					if (end != -1) {
						String ref_id = line.substr(start, end - start);
						if (ext_resource_ids.has(ref_id)) {
							const ExtResourceInfo &info = ext_resource_ids[ref_id];
							if (info.type == "Script" || info.type == "GDScript" ||
									info.path.ends_with(".gd") || info.path.ends_with(".hm") || info.path.ends_with(".hmc")) {
								current_has_script = true;
								// Try to resolve and parse the script.
								String resolved = _resolve_script_path(info.path, resource_dir);
								if (!resolved.is_empty()) {
									String source = FileAccess::get_file_as_string(resolved);
									if (!source.is_empty()) {
										_extract_script_exports(source, current_script_exports);
									}
								}
							}
						}
					}
				}

				// Property validation.
				if (!current_section_type.is_empty() && db) {
					if (!prop_name.is_empty() && prop_name[0] != '_' && prop_name != "metadata") {
						if (!db->has_property(StringName(current_section_type), StringName(prop_name))) {
							// Check builtin special properties.
							static const char *builtin_props[] = {
								"script", "unique_name_in_owner", "resource_name",
								"resource_local_to_scene", "resource_path", nullptr
							};
							bool is_known = false;
							for (int k = 0; builtin_props[k]; k++) {
								if (prop_name == builtin_props[k]) {
									is_known = true;
									break;
								}
							}
							// Check script @export properties.
							if (!is_known && current_script_exports.has(prop_name)) {
								is_known = true;
							}
							// If node has a script but we couldn't parse it,
							// suppress warnings (we can't know what it exports).
							if (!is_known && current_has_script && current_script_exports.is_empty()) {
								is_known = true;
							}
							if (!is_known) {
								Diagnostic d;
								d.file = p_filename;
								d.line = line_num;
								d.severity = "warning";
								d.message = vformat("Property \"%s\" not found on type \"%s\".", prop_name, current_section_type);
								result.diagnostics.push_back(d);
								result.warnings++;
							}
						}
					}
				}
			}
		}

		// Check ExtResource/SubResource references in values.
		if (line.contains("ExtResource(")) {
			int start = line.find("ExtResource(\"");
			if (start != -1) {
				start += 13;
				int end = line.find("\"", start);
				if (end != -1) {
					String ref_id = line.substr(start, end - start);
					if (!ext_resource_ids.has(ref_id)) {
						Diagnostic d;
						d.file = p_filename;
						d.line = line_num;
						d.severity = "error";
						d.message = vformat("ExtResource reference \"%s\" not declared.", ref_id);
						result.diagnostics.push_back(d);
						result.errors++;
					}
				}
			}
		}
		if (line.contains("SubResource(")) {
			int start = line.find("SubResource(\"");
			if (start != -1) {
				start += 13;
				int end = line.find("\"", start);
				if (end != -1) {
					String ref_id = line.substr(start, end - start);
					if (!sub_resource_ids.has(ref_id)) {
						Diagnostic d;
						d.file = p_filename;
						d.line = line_num;
						d.severity = "error";
						d.message = vformat("SubResource reference \"%s\" not declared.", ref_id);
						result.diagnostics.push_back(d);
						result.errors++;
					}
				}
			}
		}
	}

	return result;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

LintResult lint_resource_file(const String &p_path) {
	String content = FileAccess::get_file_as_string(p_path);
	if (content.is_empty()) {
		LintResult result;
		Diagnostic d;
		d.file = p_path;
		d.line = 1;
		d.severity = "error";
		d.message = "Cannot read file or file is empty.";
		result.diagnostics.push_back(d);
		result.errors++;
		return result;
	}
	return _lint_content(content, p_path);
}

LintResult lint_resource_string(const String &p_content, const String &p_filename) {
	return _lint_content(p_content, p_filename);
}

} // namespace resource_lint

#endif // HOMOT
