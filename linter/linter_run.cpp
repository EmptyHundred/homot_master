/**************************************************************************/
/*  linter_run.cpp                                                        */
/**************************************************************************/
/*  GDScript directory linter implementation.                             */
/**************************************************************************/

#ifdef HOMOT

#include "linter_run.h"

#include "stubs/classdb_stub.h"
#include "stubs/linterdb.h"

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_parser.h"
#include "modules/gdscript/gdscript_warning.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/string/print_string.h"

namespace linter {

// Recursively collect script files from a directory.
static void collect_scripts(const String &p_dir, Vector<String> &r_scripts) {
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
			String ext = file.get_extension().to_lower();
			if (ext == "gd" || ext == "hm" || ext == "hmc") {
				r_scripts.push_back(p_dir.path_join(file));
			}
		}
		file = da->get_next();
	}
	da->list_dir_end();
}

// Lightweight class_name extraction from source without full parsing.
static String extract_class_name(const String &p_source) {
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

// Extract `extends <Type>`.
static String extract_extends(const String &p_source) {
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

// Resolve native base by walking extends chain.
static StringName resolve_native_base(const String &p_extends, const HashMap<String, String> &p_class_to_extends) {
	String current = p_extends;
	HashSet<String> visited;
	while (!current.is_empty() && !visited.has(current)) {
		visited.insert(current);
		if (ClassDB::class_exists(StringName(current))) {
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

int run_lint_dir(const String &p_dir, const String &p_db_path) {
	// Optionally load linter database.
	LinterDB *linter_db = nullptr;
	if (!p_db_path.is_empty()) {
		linter_db = memnew(LinterDB);
		Error err = linter_db->load_from_json(p_db_path);
		if (err != OK) {
			print_line(vformat("WARNING: Failed to load linter database: %s", p_db_path));
			memdelete(linter_db);
			linter_db = nullptr;
		} else {
			print_line(vformat("Loaded linter database: %s", p_db_path));
		}
	}

	// Collect script files.
	Vector<String> script_paths;
	collect_scripts(p_dir, script_paths);

	if (script_paths.is_empty()) {
		print_line(vformat("No script files found in: %s", p_dir));
		if (linter_db) {
			memdelete(linter_db);
		}
		return 0;
	}
	print_line(vformat("Found %d script(s) in: %s", script_paths.size(), p_dir));

	// Pre-scan: extract class_name declarations.
	HashMap<String, String> class_to_path;
	HashMap<String, String> class_to_extends;
	for (const String &path : script_paths) {
		String source = FileAccess::get_file_as_string(path);
		String class_name = extract_class_name(source);
		if (!class_name.is_empty()) {
			class_to_path[class_name] = path;
			class_to_extends[class_name] = extract_extends(source);
		}
	}

	// Lint each script.
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

		// Collect errors.
		const List<GDScriptParser::ParserError> &errors = parser.get_errors();
		int file_errors = errors.size();
		int file_warnings = 0;

		if (file_errors > 0) {
			for (const GDScriptParser::ParserError &e : errors) {
				print_line(vformat("  ERROR: %s:%d:%d: %s", path, e.line, e.column, e.message));
			}
		}

#ifdef DEBUG_ENABLED
		// Collect warnings.
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

	// Summary.
	print_line("");
	print_line("=== Lint Summary ===");
	print_line(vformat("Scripts:  %d", script_paths.size()));
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
