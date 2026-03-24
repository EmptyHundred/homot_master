/**************************************************************************/
/*  shader_lint.cpp                                                       */
/**************************************************************************/
/*  Lightweight linter for .gdshader files.                               */
/*  Phase 1: structural and syntax checks.                                */
/**************************************************************************/

#ifdef HOMOT

#include "shader_lint.h"

#include "core/io/file_access.h"
#include "core/templates/hash_set.h"

namespace shader_lint {

// ---------------------------------------------------------------------------
// Known identifiers
// ---------------------------------------------------------------------------

static const char *VALID_SHADER_TYPES[] = {
	"spatial", "canvas_item", "particles", "sky", "fog", nullptr
};

static const char *VALID_SHADER_DATA_TYPES[] = {
	"void", "bool", "bvec2", "bvec3", "bvec4",
	"int", "ivec2", "ivec3", "ivec4",
	"uint", "uvec2", "uvec3", "uvec4",
	"float", "vec2", "vec3", "vec4",
	"mat2", "mat3", "mat4",
	"sampler2D", "isampler2D", "usampler2D",
	"sampler2DArray", "isampler2DArray", "usampler2DArray",
	"sampler3D", "isampler3D", "usampler3D",
	"samplerCube", "samplerCubeArray",
	"samplerExternalOES",
	nullptr
};

// Standard entry points per shader type.
// spatial: vertex, fragment, light
// canvas_item: vertex, fragment, light
// particles: start, process
// sky: sky
// fog: fog
static const char *ENTRY_POINTS_SPATIAL[] = { "vertex", "fragment", "light", nullptr };
static const char *ENTRY_POINTS_CANVAS_ITEM[] = { "vertex", "fragment", "light", nullptr };
static const char *ENTRY_POINTS_PARTICLES[] = { "start", "process", nullptr };
static const char *ENTRY_POINTS_SKY[] = { "sky", nullptr };
static const char *ENTRY_POINTS_FOG[] = { "fog", nullptr };

static const char **_get_entry_points(const String &p_shader_type) {
	if (p_shader_type == "spatial") return ENTRY_POINTS_SPATIAL;
	if (p_shader_type == "canvas_item") return ENTRY_POINTS_CANVAS_ITEM;
	if (p_shader_type == "particles") return ENTRY_POINTS_PARTICLES;
	if (p_shader_type == "sky") return ENTRY_POINTS_SKY;
	if (p_shader_type == "fog") return ENTRY_POINTS_FOG;
	return nullptr;
}

static bool _is_valid_type(const String &p_type) {
	for (int i = 0; VALID_SHADER_DATA_TYPES[i]; i++) {
		if (p_type == VALID_SHADER_DATA_TYPES[i]) {
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// Simple tokenizer state
// ---------------------------------------------------------------------------

enum CommentState {
	COMMENT_NONE,
	COMMENT_LINE,
	COMMENT_BLOCK,
};

// Strip comments from a line, tracking multi-line comment state.
static String _strip_comments(const String &p_line, CommentState &r_state) {
	String result;
	int i = 0;
	int len = p_line.length();

	while (i < len) {
		if (r_state == COMMENT_BLOCK) {
			// Look for end of block comment.
			if (i + 1 < len && p_line[i] == '*' && p_line[i + 1] == '/') {
				r_state = COMMENT_NONE;
				i += 2;
			} else {
				i++;
			}
			continue;
		}

		// Check for comment start.
		if (i + 1 < len && p_line[i] == '/' && p_line[i + 1] == '/') {
			// Line comment — rest of line is comment.
			break;
		}
		if (i + 1 < len && p_line[i] == '/' && p_line[i + 1] == '*') {
			r_state = COMMENT_BLOCK;
			i += 2;
			continue;
		}

		result += p_line[i];
		i++;
	}

	return result;
}

// ---------------------------------------------------------------------------
// Main lint logic
// ---------------------------------------------------------------------------

static LintResult _lint_content(const String &p_content, const String &p_filename) {
	LintResult result;
	Vector<String> lines = p_content.split("\n");

	if (lines.is_empty()) {
		Diagnostic d;
		d.file = p_filename;
		d.line = 1;
		d.severity = "error";
		d.message = "Empty shader file.";
		result.diagnostics.push_back(d);
		result.errors++;
		return result;
	}

	// --- Pass 1: Strip comments, find shader_type ---
	CommentState comment_state = COMMENT_NONE;
	Vector<String> stripped_lines;
	stripped_lines.resize(lines.size());
	for (int i = 0; i < lines.size(); i++) {
		stripped_lines.write[i] = _strip_comments(lines[i], comment_state);
	}

	if (comment_state == COMMENT_BLOCK) {
		Diagnostic d;
		d.file = p_filename;
		d.line = lines.size();
		d.severity = "error";
		d.message = "Unterminated block comment.";
		result.diagnostics.push_back(d);
		result.errors++;
	}

	// --- Find shader_type declaration ---
	String shader_type;
	int shader_type_line = -1;
	for (int i = 0; i < stripped_lines.size(); i++) {
		String line = stripped_lines[i].strip_edges();
		if (line.is_empty()) {
			continue;
		}
		if (line.begins_with("shader_type")) {
			shader_type_line = i + 1;
			// Parse: shader_type <name>;
			String rest = line.substr(11).strip_edges();
			if (rest.ends_with(";")) {
				rest = rest.substr(0, rest.length() - 1).strip_edges();
			} else {
				Diagnostic d;
				d.file = p_filename;
				d.line = i + 1;
				d.severity = "error";
				d.message = "Missing semicolon after shader_type declaration.";
				result.diagnostics.push_back(d);
				result.errors++;
			}
			shader_type = rest;
			break;
		} else {
			// First non-empty, non-comment line must be shader_type.
			Diagnostic d;
			d.file = p_filename;
			d.line = i + 1;
			d.severity = "error";
			d.message = "First statement must be 'shader_type <type>;'.";
			result.diagnostics.push_back(d);
			result.errors++;
			break;
		}
	}

	if (shader_type.is_empty() && shader_type_line == -1) {
		Diagnostic d;
		d.file = p_filename;
		d.line = 1;
		d.severity = "error";
		d.message = "Missing shader_type declaration.";
		result.diagnostics.push_back(d);
		result.errors++;
		return result;
	}

	// Validate shader type.
	if (!shader_type.is_empty()) {
		bool valid = false;
		for (int i = 0; VALID_SHADER_TYPES[i]; i++) {
			if (shader_type == VALID_SHADER_TYPES[i]) {
				valid = true;
				break;
			}
		}
		if (!valid) {
			Diagnostic d;
			d.file = p_filename;
			d.line = shader_type_line;
			d.severity = "error";
			d.message = vformat("Unknown shader_type \"%s\". Expected: spatial, canvas_item, particles, sky, fog.", shader_type);
			result.diagnostics.push_back(d);
			result.errors++;
		}
	}

	// --- Pass 2: Brace/parenthesis matching and structural checks ---
	int brace_depth = 0;
	int paren_depth = 0;
	HashSet<String> declared_functions;
	HashSet<String> declared_uniforms;

	for (int i = 0; i < stripped_lines.size(); i++) {
		String line = stripped_lines[i].strip_edges();
		if (line.is_empty()) {
			continue;
		}

		// Count braces and parens.
		for (int j = 0; j < line.length(); j++) {
			char32_t c = line[j];
			if (c == '{') brace_depth++;
			else if (c == '}') brace_depth--;
			else if (c == '(') paren_depth++;
			else if (c == ')') paren_depth--;

			if (brace_depth < 0) {
				Diagnostic d;
				d.file = p_filename;
				d.line = i + 1;
				d.severity = "error";
				d.message = "Unexpected closing brace '}'.";
				result.diagnostics.push_back(d);
				result.errors++;
				brace_depth = 0;
			}
			if (paren_depth < 0) {
				Diagnostic d;
				d.file = p_filename;
				d.line = i + 1;
				d.severity = "error";
				d.message = "Unexpected closing parenthesis ')'.";
				result.diagnostics.push_back(d);
				result.errors++;
				paren_depth = 0;
			}
		}

		// Check uniform declarations (at top level: brace_depth == 0).
		if (brace_depth == 0 && line.begins_with("uniform")) {
			// uniform <type> <name> ...;
			Vector<String> parts = line.replace(";", "").split(" ");
			if (parts.size() >= 3) {
				String type = parts[1].strip_edges();
				String name = parts[2].strip_edges();
				// Remove any trailing : or = from name.
				if (name.contains(":")) {
					name = name.substr(0, name.find(":"));
				}
				if (name.contains("=")) {
					name = name.substr(0, name.find("="));
				}
				if (!_is_valid_type(type) && type != "struct") {
					Diagnostic d;
					d.file = p_filename;
					d.line = i + 1;
					d.severity = "error";
					d.message = vformat("Unknown uniform type \"%s\".", type);
					result.diagnostics.push_back(d);
					result.errors++;
				}
				if (!name.is_empty()) {
					if (declared_uniforms.has(name)) {
						Diagnostic d;
						d.file = p_filename;
						d.line = i + 1;
						d.severity = "error";
						d.message = vformat("Duplicate uniform \"%s\".", name);
						result.diagnostics.push_back(d);
						result.errors++;
					} else {
						declared_uniforms.insert(name);
					}
				}
			} else if (parts.size() < 3) {
				Diagnostic d;
				d.file = p_filename;
				d.line = i + 1;
				d.severity = "error";
				d.message = "Incomplete uniform declaration.";
				result.diagnostics.push_back(d);
				result.errors++;
			}
		}

		// Detect function definitions at top level.
		if (brace_depth == 0 && line.contains("(") && line.contains(")")) {
			// Look for pattern: <type> <name>(...) {
			// Simple heuristic: if line has a type keyword followed by identifier and parens.
			Vector<String> parts = line.split("(");
			if (parts.size() >= 2) {
				String before_paren = parts[0].strip_edges();
				Vector<String> words = before_paren.split(" ");
				if (words.size() >= 2) {
					String func_name = words[words.size() - 1].strip_edges();
					String return_type = words[words.size() - 2].strip_edges();
					if (_is_valid_type(return_type) || return_type == "void") {
						declared_functions.insert(func_name);
					}
				}
			}
		}
	}

	// Check unclosed braces/parens.
	if (brace_depth > 0) {
		Diagnostic d;
		d.file = p_filename;
		d.line = lines.size();
		d.severity = "error";
		d.message = vformat("Unclosed brace(s): %d '{' without matching '}'.", brace_depth);
		result.diagnostics.push_back(d);
		result.errors++;
	}
	if (paren_depth > 0) {
		Diagnostic d;
		d.file = p_filename;
		d.line = lines.size();
		d.severity = "error";
		d.message = vformat("Unclosed parenthesis: %d '(' without matching ')'.", paren_depth);
		result.diagnostics.push_back(d);
		result.errors++;
	}

	// --- Pass 3: Validate entry point function names ---
	const char **entry_points = _get_entry_points(shader_type);
	if (entry_points) {
		for (const String &func : declared_functions) {
			bool is_entry = false;
			for (int k = 0; entry_points[k]; k++) {
				if (func == entry_points[k]) {
					is_entry = true;
					break;
				}
			}
			// Custom functions are fine, no warning needed.
		}
	}

	return result;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

LintResult lint_shader_file(const String &p_path) {
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

LintResult lint_shader_string(const String &p_content, const String &p_filename) {
	return _lint_content(p_content, p_filename);
}

} // namespace shader_lint

#endif // HOMOT
