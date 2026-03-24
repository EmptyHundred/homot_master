/**************************************************************************/
/*  linter_run.cpp                                                        */
/**************************************************************************/
/*  CLI linter — collects scripts/resources/shaders, pre-scans for        */
/*  class_name declarations, registers global classes, then runs the      */
/*  appropriate analyzer on each file.                                    */
/**************************************************************************/

#ifdef HOMOT

#include "linter_run.h"
#include "resource_lint.h"
#include "shader_lint.h"
#include "workspace.h"

#include "stubs/classdb_stub.h"
#include "stubs/linterdb.h"

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_parser.h"
#include "modules/gdscript/gdscript_warning.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/string/print_string.h"

namespace linter {

int run_lint(const Vector<String> &p_paths, const String &p_db_path) {
	// 1. Optionally load linter database.
	LinterDB *linter_db = nullptr;
	if (!p_db_path.is_empty()) {
		linter_db = memnew(LinterDB);
		Error err = linter_db->load_from_json(p_db_path);
		if (err != OK) {
			print_line(vformat("WARNING: Failed to load linter database: %s (using engine ClassDB)", p_db_path));
			memdelete(linter_db);
			linter_db = nullptr;
		} else {
			print_line(vformat("Loaded linter database: %s", p_db_path));
		}
	}

	// 2. Collect all lintable files (expand directories, pass through files).
	Vector<String> script_paths;
	Vector<String> resource_paths;
	Vector<String> shader_paths;

	for (const String &path : p_paths) {
		String normalized = path.replace("\\", "/");
		if (DirAccess::exists(normalized)) {
			workspace::collect_all_files(normalized, script_paths, resource_paths, shader_paths);
		} else if (workspace::is_script_file(normalized)) {
			script_paths.push_back(normalized);
		} else if (workspace::is_resource_file(normalized)) {
			resource_paths.push_back(normalized);
		} else if (workspace::is_shader_file(normalized)) {
			shader_paths.push_back(normalized);
		} else {
			print_line(vformat("WARNING: Skipping unsupported path: %s", normalized));
		}
	}

	int total_files = script_paths.size() + resource_paths.size() + shader_paths.size();
	if (total_files == 0) {
		print_line("No lintable files found.");
		if (linter_db) {
			cleanup_classdb_stubs();
			memdelete(linter_db);
		}
		return 0;
	}
	print_line(vformat("Found %d file(s): %d scripts, %d resources, %d shaders.",
			total_files, script_paths.size(), resource_paths.size(), shader_paths.size()));

	// 3. Pre-scan scripts: extract class_name declarations and register global classes.
	HashMap<String, String> class_to_path;
	HashMap<String, String> class_to_extends;
	if (!script_paths.is_empty()) {
		workspace::scan_and_register_classes(script_paths, class_to_path, class_to_extends);
	}

	// 4. Lint scripts.
	int total_errors = 0;
	int total_warnings = 0;

	for (const String &path : script_paths) {
		String source = FileAccess::get_file_as_string(path);
		if (source.is_empty()) {
			print_line(vformat("  SKIP (empty): %s", path));
			continue;
		}

		GDScriptParser parser;
		GDScriptAnalyzer analyzer(&parser);

		Error parse_err = parser.parse(source, path, false);
		if (parse_err == OK) {
			parse_err = analyzer.analyze();
		}

		const List<GDScriptParser::ParserError> &errors = parser.get_errors();
		int file_errors = errors.size();
		int file_warnings = 0;

		if (file_errors > 0) {
			for (const GDScriptParser::ParserError &e : errors) {
				print_line(vformat("  ERROR: %s:%d:%d: %s", path, e.line, e.column, e.message));
			}
		}

#ifdef DEBUG_ENABLED
		const List<GDScriptWarning> &warnings = parser.get_warnings();
		file_warnings = warnings.size();
		if (file_warnings > 0) {
			for (const GDScriptWarning &w : warnings) {
				print_line(vformat("  WARN:  %s:%d: [%s] %s", path, w.start_line,
						GDScriptWarning::get_name_from_code(w.code), w.get_message()));
			}
		}
#endif

		if (file_errors == 0 && file_warnings == 0) {
			print_line(vformat("  OK: %s", path));
		}

		total_errors += file_errors;
		total_warnings += file_warnings;
	}

	// 5. Lint resource files (.tscn, .tres).
	for (const String &path : resource_paths) {
		resource_lint::LintResult res = resource_lint::lint_resource_file(path);

		for (const resource_lint::Diagnostic &d : res.diagnostics) {
			if (d.severity == "error") {
				print_line(vformat("  ERROR: %s:%d: %s", path, d.line, d.message));
			} else {
				print_line(vformat("  WARN:  %s:%d: %s", path, d.line, d.message));
			}
		}

		if (res.errors == 0 && res.warnings == 0) {
			print_line(vformat("  OK: %s", path));
		}

		total_errors += res.errors;
		total_warnings += res.warnings;
	}

	// 6. Lint shader files (.gdshader).
	for (const String &path : shader_paths) {
		shader_lint::LintResult res = shader_lint::lint_shader_file(path);

		for (const shader_lint::Diagnostic &d : res.diagnostics) {
			if (d.severity == "error") {
				print_line(vformat("  ERROR: %s:%d: %s", path, d.line, d.message));
			} else {
				print_line(vformat("  WARN:  %s:%d: %s", path, d.line, d.message));
			}
		}

		if (res.errors == 0 && res.warnings == 0) {
			print_line(vformat("  OK: %s", path));
		}

		total_errors += res.errors;
		total_warnings += res.warnings;
	}

	// 7. Summary.
	print_line("");
	print_line("=== Lint Summary ===");
	print_line(vformat("Files:    %d (scripts: %d, resources: %d, shaders: %d)",
			total_files, script_paths.size(), resource_paths.size(), shader_paths.size()));
	print_line(vformat("Errors:   %d", total_errors));
	print_line(vformat("Warnings: %d", total_warnings));

	// Cleanup.
	if (linter_db) {
		cleanup_classdb_stubs();
		memdelete(linter_db);
	}

	return total_errors > 0 ? 1 : 0;
}

} // namespace linter

#endif // HOMOT
