/**************************************************************************/
/*  verifier.cpp                                                          */
/**************************************************************************/

#ifdef HOMOT

#include "verifier.h"

#include "../stubs/linterdb.h"
#include "../stubs/script_server_stub.h"

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_parser.h"
#include "modules/gdscript/gdscript_warning.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"

using linter::LinterDB;
using linter::ScriptServerStub;

namespace lspa {

// Recursively collect script files from a directory.
static void _collect_scripts(const String &p_dir, Vector<String> &r_scripts) {
	Ref<DirAccess> da = DirAccess::open(p_dir);
	if (da.is_null()) {
		return;
	}
	da->list_dir_begin();
	String file = da->get_next();
	while (!file.is_empty()) {
		if (da->current_is_dir()) {
			if (file != "." && file != "..") {
				_collect_scripts(p_dir.path_join(file), r_scripts);
			}
		} else {
			String ext = file.get_extension().to_lower();
			if (ext == "gd" || ext == "hm" || ext == "hmc") {
				r_scripts.push_back(p_dir.path_join(file));
			}
		}
		file = da->get_next();
	}
	da->list_dir_end();
}

// Extract class_name from source (lightweight, no full parse).
static String _extract_class_name(const String &p_source) {
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

static String _extract_extends(const String &p_source) {
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

static StringName _resolve_native_base(const String &p_extends, const HashMap<String, String> &p_class_to_extends) {
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

// Collect diagnostics from a parsed script into arrays.
static void _collect_diagnostics(const GDScriptParser &p_parser, const String &p_file, const String &p_severity_filter, Array &r_diagnostics) {
	bool want_errors = (p_severity_filter != "warning");
	bool want_warnings = (p_severity_filter != "error");

	if (want_errors) {
		const List<GDScriptParser::ParserError> &errors = p_parser.get_errors();
		for (const GDScriptParser::ParserError &e : errors) {
			Dictionary d;
			if (!p_file.is_empty()) {
				d["file"] = p_file;
			}
			d["line"] = e.line;
			d["col"] = e.column;
			d["severity"] = "error";
			d["message"] = e.message;
			r_diagnostics.push_back(d);
		}
	}

#ifdef DEBUG_ENABLED
	if (want_warnings) {
		const List<GDScriptWarning> &warnings = p_parser.get_warnings();
		for (const GDScriptWarning &w : warnings) {
			Dictionary d;
			if (!p_file.is_empty()) {
				d["file"] = p_file;
			}
			d["line"] = w.start_line;
			d["col"] = 1;
			d["severity"] = "warning";
			d["code"] = GDScriptWarning::get_name_from_code(w.code);
			d["message"] = w.get_message();
			r_diagnostics.push_back(d);
		}
	}
#endif
}

// ---------------------------------------------------------------------------
// verify/lint
// ---------------------------------------------------------------------------

Dictionary Verifier::handle_lint(const Dictionary &p_params) {
	// Clear stale global class registrations from previous lint calls.
	ScriptServerStub::clear();

	Array paths_arr = p_params.get("paths", Array());
	String severity = p_params.get("severity", "all");

	// Collect script files.
	Vector<String> script_paths;
	for (int i = 0; i < paths_arr.size(); i++) {
		String path = String(paths_arr[i]).replace("\\", "/");
		if (DirAccess::exists(path)) {
			_collect_scripts(path, script_paths);
		} else {
			String ext = path.get_extension().to_lower();
			if (ext == "gd" || ext == "hm" || ext == "hmc") {
				script_paths.push_back(path);
			}
		}
	}

	// Pre-scan for class_name and register global classes.
	HashMap<String, String> class_to_path;
	HashMap<String, String> class_to_extends;
	for (const String &path : script_paths) {
		String source = FileAccess::get_file_as_string(path);
		String cname = _extract_class_name(source);
		if (!cname.is_empty()) {
			class_to_path[cname] = path;
			class_to_extends[cname] = _extract_extends(source);
		}
	}
	for (const KeyValue<String, String> &kv : class_to_path) {
		StringName native_base = _resolve_native_base(
				class_to_extends.has(kv.key) ? class_to_extends[kv.key] : "RefCounted",
				class_to_extends);
		ScriptServerStub::register_global_class(StringName(kv.key), kv.value, native_base);
	}

	// Lint each file.
	int total_errors = 0;
	int total_warnings = 0;
	Array diagnostics;

	for (const String &path : script_paths) {
		String source = FileAccess::get_file_as_string(path);
		if (source.is_empty()) {
			continue;
		}

		GDScriptParser parser;
		GDScriptAnalyzer analyzer(&parser);

		Error parse_err = parser.parse(source, path, false);
		if (parse_err == OK) {
			analyzer.analyze();
		}

		int before = diagnostics.size();
		_collect_diagnostics(parser, path, severity, diagnostics);

		// Count errors and warnings from what was added.
		for (int i = before; i < diagnostics.size(); i++) {
			Dictionary d = diagnostics[i];
			if (String(d.get("severity", "")) == "error") {
				total_errors++;
			} else {
				total_warnings++;
			}
		}
	}

	Dictionary summary;
	summary["files"] = script_paths.size();
	summary["errors"] = total_errors;
	summary["warnings"] = total_warnings;

	Dictionary result;
	result["summary"] = summary;
	result["diagnostics"] = diagnostics;
	return result;
}

// ---------------------------------------------------------------------------
// verify/check
// ---------------------------------------------------------------------------

Dictionary Verifier::handle_check(const Dictionary &p_params) {
	String content = p_params.get("content", "");
	String filename = p_params.get("filename", "inline.gd");
	String severity = p_params.get("severity", "all");

	if (content.is_empty()) {
		Dictionary result;
		result["errors"] = Array();
		result["warnings"] = Array();
		return result;
	}

	// Use the filename as a virtual path for the parser.
	String virtual_path = filename;
	if (!virtual_path.contains("/") && !virtual_path.contains("\\")) {
		virtual_path = "/tmp/" + virtual_path;
	}

	GDScriptParser parser;
	GDScriptAnalyzer analyzer(&parser);

	Error parse_err = parser.parse(content, virtual_path, false);
	if (parse_err == OK) {
		analyzer.analyze();
	}

	Array all_diagnostics;
	_collect_diagnostics(parser, "", severity, all_diagnostics);

	// Split into errors and warnings for the response.
	Array errors;
	Array warnings;
	for (int i = 0; i < all_diagnostics.size(); i++) {
		Dictionary d = all_diagnostics[i];
		if (String(d.get("severity", "")) == "error") {
			errors.push_back(d);
		} else {
			warnings.push_back(d);
		}
	}

	Dictionary result;
	result["errors"] = errors;
	result["warnings"] = warnings;
	return result;
}

} // namespace lspa

#endif // HOMOT
