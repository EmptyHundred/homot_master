/**************************************************************************/
/*  resource_lint.h                                                       */
/**************************************************************************/
/*  Linter for .tscn and .tres files.                                     */
/*  Validates node/resource types, property names, script references,     */
/*  and structural correctness against linterdb.                          */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"

namespace resource_lint {

struct Diagnostic {
	String file;
	int line = 0;
	String severity; // "error" or "warning"
	String message;

	Dictionary to_dict() const {
		Dictionary d;
		if (!file.is_empty()) {
			d[String("file")] = file;
		}
		d[String("line")] = line;
		d[String("severity")] = severity;
		d[String("message")] = message;
		return d;
	}
};

struct LintResult {
	int errors = 0;
	int warnings = 0;
	Vector<Diagnostic> diagnostics;
};

// Lint a .tscn or .tres file. Returns diagnostics.
LintResult lint_resource_file(const String &p_path);

// Lint resource content from a string (for verify/check).
LintResult lint_resource_string(const String &p_content, const String &p_filename);

} // namespace resource_lint

#endif // HOMOT
