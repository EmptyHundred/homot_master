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

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/class_db.h"

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_parser.h"

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

// ---------------------------------------------------------------------------
// JSON serialization helpers (same format as linterdb.json)
// ---------------------------------------------------------------------------

static Dictionary _property_info_to_dict(const PropertyInfo &p_info) {
	Dictionary d;
	d["type"] = (int)p_info.type;
	if (!p_info.name.is_empty()) {
		d["name"] = p_info.name;
	}
	if (!p_info.class_name.is_empty()) {
		d["class_name"] = String(p_info.class_name);
	}
	if (p_info.hint != PROPERTY_HINT_NONE) {
		d["hint"] = (int)p_info.hint;
	}
	if (!p_info.hint_string.is_empty()) {
		d["hint_string"] = p_info.hint_string;
	}
	if (p_info.usage != PROPERTY_USAGE_DEFAULT) {
		d["usage"] = (int)p_info.usage;
	}
	return d;
}

static Dictionary _method_info_to_dict(const MethodInfo &p_info) {
	Dictionary d;
	d["name"] = p_info.name;
	d["return_val"] = _property_info_to_dict(p_info.return_val);
	d["flags"] = (int)p_info.flags;

	Array args;
	for (int i = 0; i < p_info.arguments.size(); i++) {
		args.push_back(_property_info_to_dict(p_info.arguments[i]));
	}
	d["args"] = args;
	d["default_arg_count"] = p_info.default_arguments.size();
	return d;
}

// ---------------------------------------------------------------------------
// Project classdb export (dump-project)
// ---------------------------------------------------------------------------

Error dump_project_classdb(const String &p_project_root, const String &p_output_path) {
	// 1. Collect all scripts.
	Vector<String> scripts;
	collect_scripts(p_project_root, scripts);

	// 2. Pre-scan: extract class_name + extends for all scripts.
	HashMap<String, String> class_to_path;
	HashMap<String, String> class_to_extends;
	if (!scripts.is_empty()) {
		scan_and_register_classes(scripts, class_to_path, class_to_extends);
	}

	// 3. Fully parse each class_name script and extract members.
	Dictionary classes_dict;

	for (const KeyValue<String, String> &kv : class_to_path) {
		const String &cname = kv.key;
		const String &path = kv.value;

		String source = FileAccess::get_file_as_string(path);
		if (source.is_empty()) {
			continue;
		}

		// Parse and analyze.
		GDScriptParser parser;
		GDScriptAnalyzer analyzer(&parser);

		Error parse_err = parser.parse(source, path, false);
		if (parse_err != OK) {
			// Still include the class with basic info even if parsing fails.
			print_line(vformat("  WARNING: Parse errors in %s, exporting basic info only.", path));
		} else {
			analyzer.analyze();
		}

		const GDScriptParser::ClassNode *cls_node = parser.get_tree();
		if (!cls_node) {
			continue;
		}

		Dictionary cls;

		// Resolve parent to native base.
		String extends_name = class_to_extends.has(cname) ? class_to_extends[cname] : "RefCounted";
		StringName native_base = resolve_native_base(extends_name, class_to_extends);
		cls["parent"] = extends_name;
		cls["is_abstract"] = false;

		// Methods.
		{
			Array methods_arr;
			for (int i = 0; i < cls_node->members.size(); i++) {
				if (cls_node->members[i].type != GDScriptParser::ClassNode::Member::FUNCTION) {
					continue;
				}
				const GDScriptParser::FunctionNode *fn = cls_node->members[i].function;
				Dictionary md = _method_info_to_dict(fn->info);
				md["is_vararg"] = fn->is_vararg();
				md["is_static"] = fn->is_static;
				md["instance_class"] = cname;
				methods_arr.push_back(md);
			}
			cls["methods"] = methods_arr;
		}

		// Properties (class variables).
		{
			Array props_arr;
			for (int i = 0; i < cls_node->members.size(); i++) {
				if (cls_node->members[i].type != GDScriptParser::ClassNode::Member::VARIABLE) {
					continue;
				}
				const GDScriptParser::VariableNode *var = cls_node->members[i].variable;

				PropertyInfo pi;
				pi.name = var->identifier->name;

				// Get type from datatype if resolved.
				GDScriptParser::DataType dt = var->get_datatype();
				if (dt.is_set()) {
					pi.type = dt.builtin_type;
					if (dt.kind == GDScriptParser::DataType::CLASS || dt.kind == GDScriptParser::DataType::NATIVE) {
						pi.type = Variant::OBJECT;
						pi.class_name = dt.native_type;
					}
				}

				// Use export info if available.
				if (var->exported) {
					pi = var->export_info;
					if (pi.name.is_empty()) {
						pi.name = var->identifier->name;
					}
				}

				Dictionary pd = _property_info_to_dict(pi);

				// Getter/setter names.
				if (var->property == GDScriptParser::VariableNode::PROP_SETGET) {
					if (var->setter_pointer) {
						pd["setter"] = String(var->setter_pointer->name);
					}
					if (var->getter_pointer) {
						pd["getter"] = String(var->getter_pointer->name);
					}
				}
				props_arr.push_back(pd);
			}
			cls["properties"] = props_arr;
		}

		// Signals.
		{
			Array signals_arr;
			for (int i = 0; i < cls_node->members.size(); i++) {
				if (cls_node->members[i].type != GDScriptParser::ClassNode::Member::SIGNAL) {
					continue;
				}
				const GDScriptParser::SignalNode *sig = cls_node->members[i].signal;
				signals_arr.push_back(_method_info_to_dict(sig->method_info));
			}
			cls["signals"] = signals_arr;
		}

		// Enums.
		{
			Dictionary enums_dict;
			for (int i = 0; i < cls_node->members.size(); i++) {
				if (cls_node->members[i].type != GDScriptParser::ClassNode::Member::ENUM) {
					continue;
				}
				const GDScriptParser::EnumNode *en = cls_node->members[i].m_enum;
				Dictionary enum_values;
				for (int j = 0; j < en->values.size(); j++) {
					enum_values[String(en->values[j].identifier->name)] = en->values[j].value;
				}
				enums_dict[String(en->identifier->name)] = enum_values;
			}
			if (!enums_dict.is_empty()) {
				cls["enums"] = enums_dict;
			}
		}

		// Constants.
		{
			Dictionary consts;
			for (int i = 0; i < cls_node->members.size(); i++) {
				if (cls_node->members[i].type == GDScriptParser::ClassNode::Member::ENUM_VALUE) {
					// Unnamed enum values as constants.
					const GDScriptParser::EnumNode::Value &ev = cls_node->members[i].enum_value;
					consts[String(ev.identifier->name)] = ev.value;
				} else if (cls_node->members[i].type == GDScriptParser::ClassNode::Member::CONSTANT) {
					const GDScriptParser::ConstantNode *cn = cls_node->members[i].constant;
					// Try to get the resolved value.
					if (cn->initializer && cn->initializer->is_constant) {
						Variant val = cn->initializer->reduced_value;
						if (val.get_type() == Variant::INT) {
							consts[String(cn->identifier->name)] = (int64_t)val;
						}
					}
				}
			}
			if (!consts.is_empty()) {
				cls["constants"] = consts;
			}
		}

		classes_dict[cname] = cls;
	}

	// 4. Parse autoloads.
	String project_godot = p_project_root.path_join("project.godot");
	Vector<AutoloadEntry> autoloads = parse_autoloads(project_godot);

	Array singletons_arr;
	for (const AutoloadEntry &entry : autoloads) {
		if (entry.is_singleton) {
			singletons_arr.push_back(entry.name);
		}
	}

	// 5. Build root dict (linterdb-compatible format).
	Dictionary root;
	root["format_version"] = 1;
	root["classes"] = classes_dict;
	if (!singletons_arr.is_empty()) {
		root["singletons"] = singletons_arr;
	}

	// 6. Write JSON.
	String json_text = JSON::stringify(root, "\t");
	Ref<FileAccess> f = FileAccess::open(p_output_path, FileAccess::WRITE);
	if (f.is_null()) {
		return ERR_CANT_CREATE;
	}
	f->store_string(json_text);
	return OK;
}

// ---------------------------------------------------------------------------
// Project classdb import (--project-db)
// ---------------------------------------------------------------------------

int load_project_db(const String &p_json_path) {
	String json_text = FileAccess::get_file_as_string(p_json_path);
	if (json_text.is_empty()) {
		return -1;
	}

	JSON json;
	Error err = json.parse(json_text);
	if (err != OK) {
		return -1;
	}

	Dictionary root = json.get_data();
	if (!root.has("classes")) {
		return -1;
	}

	// Merge into LinterDB (classes + singletons).
	LinterDB *db = LinterDB::get_singleton();
	if (!db) {
		return -1;
	}

	err = db->load_additional_classes(root);
	if (err != OK) {
		return -1;
	}

	// Count loaded classes.
	Dictionary classes_dict = root["classes"];
	return classes_dict.size();
}

} // namespace workspace

#endif // HOMOT
