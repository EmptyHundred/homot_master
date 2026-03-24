/**************************************************************************/
/*  verifier.cpp                                                          */
/**************************************************************************/

#ifdef HOMOT

#include "verifier.h"

#include "../stubs/linterdb.h"
#include "../stubs/script_server_stub.h"
#include "../workspace.h"

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_parser.h"
#include "modules/gdscript/gdscript_warning.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"

using linter::ScriptServerStub;

namespace lspa {

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
			workspace::collect_scripts(path, script_paths);
		} else if (workspace::is_script_file(path)) {
			script_paths.push_back(path);
		}
	}

	// Pre-scan for class_name and register global classes.
	HashMap<String, String> class_to_path;
	HashMap<String, String> class_to_extends;
	workspace::scan_and_register_classes(script_paths, class_to_path, class_to_extends);

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
