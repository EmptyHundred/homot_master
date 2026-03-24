/**************************************************************************/
/*  shader_lint.h                                                         */
/**************************************************************************/
/*  Lightweight linter for .gdshader files.                               */
/*  Phase 1: structural and syntax checks without RenderingServer.        */
/*    - shader_type declaration                                           */
/*    - Brace/parenthesis matching                                        */
/*    - Known shader types and render modes                               */
/*    - Uniform declaration syntax                                        */
/*    - Built-in function entry points                                    */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"

namespace shader_lint {

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

// Lint a .gdshader file.
LintResult lint_shader_file(const String &p_path);

// Lint shader content from a string.
LintResult lint_shader_string(const String &p_content, const String &p_filename);

} // namespace shader_lint

#endif // HOMOT
