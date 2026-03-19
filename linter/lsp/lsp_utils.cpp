/**************************************************************************/
/*  lsp_utils.cpp                                                         */
/**************************************************************************/

#ifdef HOMOT

#include "lsp_utils.h"

#include "../stubs/linterdb.h"

#include "modules/gdscript/gdscript_parser.h"

#include "core/variant/variant.h"

using linter::DocClassData;
using linter::DocConstantData;
using linter::DocEnumData;
using linter::DocMethodData;
using linter::DocPropertyData;
using linter::DocTutorialData;

namespace lsp {

// ---------------------------------------------------------------------------
// Position / word helpers
// ---------------------------------------------------------------------------

String get_word_at_position(const String &p_source, int p_lsp_line, int p_lsp_character) {
	// Find start of the target line.
	int pos = 0;
	for (int i = 0; i < p_lsp_line && pos < p_source.length(); i++) {
		while (pos < p_source.length() && p_source[pos] != '\n') {
			pos++;
		}
		if (pos < p_source.length()) {
			pos++; // skip '\n'
		}
	}

	int line_start = pos;

	// Find end of line.
	int line_end = line_start;
	while (line_end < p_source.length() && p_source[line_end] != '\n') {
		line_end++;
	}

	int cursor = line_start + p_lsp_character;
	if (cursor < line_start || cursor >= line_end) {
		return String();
	}

	// Check if cursor is on an identifier character.
	char32_t c = p_source[cursor];
	if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (c >= '0' && c <= '9'))) {
		return String();
	}

	// Expand left.
	int start = cursor;
	while (start > line_start) {
		char32_t ch = p_source[start - 1];
		if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' || (ch >= '0' && ch <= '9')) {
			start--;
		} else {
			break;
		}
	}

	// Expand right.
	int end = cursor;
	while (end < line_end) {
		char32_t ch = p_source[end];
		if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' || (ch >= '0' && ch <= '9')) {
			end++;
		} else {
			break;
		}
	}

	return p_source.substr(start, end - start);
}

int lsp_to_parser_column(const String &p_source, int p_lsp_line, int p_lsp_character, int p_tab_size) {
	// Find the start of the target line.
	int pos = 0;
	for (int i = 0; i < p_lsp_line && pos < p_source.length(); i++) {
		while (pos < p_source.length() && p_source[pos] != '\n') {
			pos++;
		}
		if (pos < p_source.length()) {
			pos++; // skip '\n'
		}
	}

	// Walk the line, expanding tabs the same way the tokenizer does.
	int col = 1; // Parser columns are 1-based.
	for (int i = 0; i < p_lsp_character && pos < p_source.length() && p_source[pos] != '\n'; i++, pos++) {
		if (p_source[pos] == '\t') {
			col += p_tab_size;
		} else {
			col++;
		}
	}
	return col;
}

int parser_column_to_lsp(const String &p_source, int p_parser_line, int p_parser_col, int p_tab_size) {
	// Find the start of the target line (p_parser_line is 1-based).
	int pos = 0;
	for (int i = 1; i < p_parser_line && pos < p_source.length(); i++) {
		while (pos < p_source.length() && p_source[pos] != '\n') {
			pos++;
		}
		if (pos < p_source.length()) {
			pos++; // skip '\n'
		}
	}

	// Walk the line, counting tabs the same way as the tokenizer, until we
	// reach the target parser column.
	int col = 1;
	int chars = 0;
	while (col < p_parser_col && pos < p_source.length() && p_source[pos] != '\n') {
		if (p_source[pos] == '\t') {
			col += p_tab_size;
		} else {
			col++;
		}
		pos++;
		chars++;
	}
	return chars;
}

// ---------------------------------------------------------------------------
// AST helpers
// ---------------------------------------------------------------------------

bool node_contains_position(const GDScriptParser::Node *p_node, int p_line, int p_col) {
	int line1 = p_line;
	int col1 = p_col;

	if (p_node->start_line > line1 || p_node->end_line < line1) {
		return false;
	}
	if (p_node->start_line == line1 && p_node->start_column > col1) {
		return false;
	}
	if (p_node->end_line == line1 && p_node->end_column < col1) {
		return false;
	}
	return true;
}

const GDScriptParser::IdentifierNode *find_identifier_in_expression(
		const GDScriptParser::ExpressionNode *p_expr, int p_line, int p_col) {
	if (!p_expr) return nullptr;

	switch (p_expr->type) {
		case GDScriptParser::Node::IDENTIFIER: {
			if (node_contains_position(p_expr, p_line, p_col)) {
				return static_cast<const GDScriptParser::IdentifierNode *>(p_expr);
			}
		} break;
		case GDScriptParser::Node::SUBSCRIPT: {
			auto *sub = static_cast<const GDScriptParser::SubscriptNode *>(p_expr);
			if (sub->is_attribute && sub->attribute) {
				// Check the attribute identifier first (more specific).
				if (node_contains_position(sub->attribute, p_line, p_col)) {
					return sub->attribute;
				}
			}
			// Check base expression.
			auto *found = find_identifier_in_expression(sub->base, p_line, p_col);
			if (found) return found;
			if (!sub->is_attribute && sub->index) {
				found = find_identifier_in_expression(sub->index, p_line, p_col);
				if (found) return found;
			}
		} break;
		case GDScriptParser::Node::CALL: {
			auto *call = static_cast<const GDScriptParser::CallNode *>(p_expr);
			// Check callee.
			auto *found = find_identifier_in_expression(call->callee, p_line, p_col);
			if (found) return found;
			// Check arguments.
			for (int i = 0; i < call->arguments.size(); i++) {
				found = find_identifier_in_expression(call->arguments[i], p_line, p_col);
				if (found) return found;
			}
		} break;
		case GDScriptParser::Node::BINARY_OPERATOR: {
			auto *binop = static_cast<const GDScriptParser::BinaryOpNode *>(p_expr);
			auto *found = find_identifier_in_expression(binop->left_operand, p_line, p_col);
			if (found) return found;
			found = find_identifier_in_expression(binop->right_operand, p_line, p_col);
			if (found) return found;
		} break;
		case GDScriptParser::Node::UNARY_OPERATOR: {
			auto *unop = static_cast<const GDScriptParser::UnaryOpNode *>(p_expr);
			auto *found = find_identifier_in_expression(unop->operand, p_line, p_col);
			if (found) return found;
		} break;
		case GDScriptParser::Node::TERNARY_OPERATOR: {
			auto *ternop = static_cast<const GDScriptParser::TernaryOpNode *>(p_expr);
			auto *found = find_identifier_in_expression(ternop->true_expr, p_line, p_col);
			if (found) return found;
			found = find_identifier_in_expression(ternop->false_expr, p_line, p_col);
			if (found) return found;
			found = find_identifier_in_expression(ternop->condition, p_line, p_col);
			if (found) return found;
		} break;
		case GDScriptParser::Node::ASSIGNMENT: {
			auto *assign = static_cast<const GDScriptParser::AssignmentNode *>(p_expr);
			auto *found = find_identifier_in_expression(assign->assignee, p_line, p_col);
			if (found) return found;
			found = find_identifier_in_expression(assign->assigned_value, p_line, p_col);
			if (found) return found;
		} break;
		case GDScriptParser::Node::CAST: {
			auto *cast = static_cast<const GDScriptParser::CastNode *>(p_expr);
			auto *found = find_identifier_in_expression(cast->operand, p_line, p_col);
			if (found) return found;
		} break;
		case GDScriptParser::Node::AWAIT: {
			auto *aw = static_cast<const GDScriptParser::AwaitNode *>(p_expr);
			auto *found = find_identifier_in_expression(aw->to_await, p_line, p_col);
			if (found) return found;
		} break;
		default:
			break;
	}
	return nullptr;
}

const GDScriptParser::IdentifierNode *find_identifier_in_suite(
		const GDScriptParser::SuiteNode *p_suite, int p_line, int p_col) {
	if (!p_suite) return nullptr;

	for (int i = 0; i < p_suite->statements.size(); i++) {
		const GDScriptParser::Node *stmt = p_suite->statements[i];
		if (!node_contains_position(stmt, p_line, p_col)) {
			continue;
		}

		switch (stmt->type) {
			case GDScriptParser::Node::VARIABLE: {
				auto *var = static_cast<const GDScriptParser::VariableNode *>(stmt);
				if (var->datatype_specifier && node_contains_position(var->datatype_specifier, p_line, p_col)) {
					for (int ti = 0; ti < var->datatype_specifier->type_chain.size(); ti++) {
						if (node_contains_position(var->datatype_specifier->type_chain[ti], p_line, p_col)) {
							return var->datatype_specifier->type_chain[ti];
						}
					}
				}
				if (var->initializer) {
					auto *found = find_identifier_in_expression(var->initializer, p_line, p_col);
					if (found) return found;
				}
			} break;
			case GDScriptParser::Node::ASSIGNMENT: {
				auto *assign = static_cast<const GDScriptParser::AssignmentNode *>(stmt);
				auto *found = find_identifier_in_expression(assign->assignee, p_line, p_col);
				if (found) return found;
				found = find_identifier_in_expression(assign->assigned_value, p_line, p_col);
				if (found) return found;
			} break;
			case GDScriptParser::Node::IF: {
				auto *if_node = static_cast<const GDScriptParser::IfNode *>(stmt);
				auto *found = find_identifier_in_expression(if_node->condition, p_line, p_col);
				if (found) return found;
				found = find_identifier_in_suite(if_node->true_block, p_line, p_col);
				if (found) return found;
				found = find_identifier_in_suite(if_node->false_block, p_line, p_col);
				if (found) return found;
			} break;
			case GDScriptParser::Node::FOR: {
				auto *for_node = static_cast<const GDScriptParser::ForNode *>(stmt);
				auto *found = find_identifier_in_expression(for_node->list, p_line, p_col);
				if (found) return found;
				found = find_identifier_in_suite(for_node->loop, p_line, p_col);
				if (found) return found;
			} break;
			case GDScriptParser::Node::WHILE: {
				auto *while_node = static_cast<const GDScriptParser::WhileNode *>(stmt);
				auto *found = find_identifier_in_expression(while_node->condition, p_line, p_col);
				if (found) return found;
				found = find_identifier_in_suite(while_node->loop, p_line, p_col);
				if (found) return found;
			} break;
			case GDScriptParser::Node::RETURN: {
				auto *ret = static_cast<const GDScriptParser::ReturnNode *>(stmt);
				if (ret->return_value) {
					auto *found = find_identifier_in_expression(ret->return_value, p_line, p_col);
					if (found) return found;
				}
			} break;
			case GDScriptParser::Node::MATCH: {
				auto *match_node = static_cast<const GDScriptParser::MatchNode *>(stmt);
				auto *found = find_identifier_in_expression(match_node->test, p_line, p_col);
				if (found) return found;
				for (int j = 0; j < match_node->branches.size(); j++) {
					found = find_identifier_in_suite(match_node->branches[j]->block, p_line, p_col);
					if (found) return found;
				}
			} break;
			default: {
				// For expression statements (bare function calls, etc.)
				if (stmt->is_expression()) {
					auto *found = find_identifier_in_expression(
							static_cast<const GDScriptParser::ExpressionNode *>(stmt), p_line, p_col);
					if (found) return found;
				}
			} break;
		}
	}
	return nullptr;
}

const GDScriptParser::IdentifierNode *find_identifier_at_position(
		const GDScriptParser::ClassNode *p_class, int p_line, int p_col) {
	if (!p_class) return nullptr;

	for (int i = 0; i < p_class->members.size(); i++) {
		const GDScriptParser::ClassNode::Member &member = p_class->members[i];

		switch (member.type) {
			case GDScriptParser::ClassNode::Member::FUNCTION: {
				const GDScriptParser::FunctionNode *func = member.function;
				if (!node_contains_position(func, p_line, p_col)) continue;
				// Check parameter type annotations.
				for (int pi = 0; pi < func->parameters.size(); pi++) {
					const GDScriptParser::ParameterNode *param = func->parameters[pi];
					if (param->datatype_specifier && node_contains_position(param->datatype_specifier, p_line, p_col)) {
						for (int ti = 0; ti < param->datatype_specifier->type_chain.size(); ti++) {
							if (node_contains_position(param->datatype_specifier->type_chain[ti], p_line, p_col)) {
								return param->datatype_specifier->type_chain[ti];
							}
						}
					}
				}
				// Check return type annotation.
				if (func->return_type) {
					if (node_contains_position(func->return_type, p_line, p_col)) {
						for (int ti = 0; ti < func->return_type->type_chain.size(); ti++) {
							if (node_contains_position(func->return_type->type_chain[ti], p_line, p_col)) {
								return func->return_type->type_chain[ti];
							}
						}
					}
				}
				// Check function body.
				if (func->body) {
					auto *found = find_identifier_in_suite(func->body, p_line, p_col);
					if (found) return found;
				}
			} break;
			case GDScriptParser::ClassNode::Member::VARIABLE: {
				const GDScriptParser::VariableNode *var = member.variable;
				if (node_contains_position(var, p_line, p_col)) {
					// Check type annotation.
					if (var->datatype_specifier && node_contains_position(var->datatype_specifier, p_line, p_col)) {
						for (int ti = 0; ti < var->datatype_specifier->type_chain.size(); ti++) {
							if (node_contains_position(var->datatype_specifier->type_chain[ti], p_line, p_col)) {
								return var->datatype_specifier->type_chain[ti];
							}
						}
					}
					if (var->initializer) {
						auto *found = find_identifier_in_expression(var->initializer, p_line, p_col);
						if (found) return found;
					}
				}
			} break;
			case GDScriptParser::ClassNode::Member::CLASS: {
				// Recurse into inner classes.
				auto *found = find_identifier_at_position(member.m_class, p_line, p_col);
				if (found) return found;
			} break;
			default:
				break;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Signature / type formatting
// ---------------------------------------------------------------------------

String method_signature(const MethodInfo &p_mi) {
	String sig = "(";
	int i = 0;
	for (const PropertyInfo &arg : p_mi.arguments) {
		if (i > 0) {
			sig += ", ";
		}
		sig += arg.name;
		if (arg.type != Variant::NIL) {
			sig += ": " + Variant::get_type_name(arg.type);
		} else if (arg.class_name != StringName()) {
			sig += ": " + String(arg.class_name);
		}
		i++;
	}
	sig += ")";
	if (p_mi.return_val.type != Variant::NIL) {
		sig += " -> " + Variant::get_type_name(p_mi.return_val.type);
	} else if (p_mi.return_val.class_name != StringName()) {
		sig += " -> " + String(p_mi.return_val.class_name);
	}
	return sig;
}

String format_datatype(const GDScriptParser::DataType &p_type) {
	if (!p_type.is_set()) {
		return "Variant";
	}
	return p_type.to_string();
}

// ---------------------------------------------------------------------------
// BBCode / Markdown / Doc generation
// ---------------------------------------------------------------------------

String bbcode_to_markdown(const String &p_bbcode) {
	// Dedent the entire text: strip the common leading whitespace from all lines.
	String md;
	{
		Vector<String> lines = p_bbcode.split("\n");
		int min_indent = INT_MAX;
		for (int i = 0; i < lines.size(); i++) {
			if (lines[i].strip_edges().is_empty()) continue;
			int indent = 0;
			while (indent < lines[i].length() && (lines[i][indent] == '\t' || lines[i][indent] == ' ')) {
				indent++;
			}
			if (indent < min_indent) {
				min_indent = indent;
			}
		}
		if (min_indent == INT_MAX) {
			min_indent = 0;
		}
		for (int i = 0; i < lines.size(); i++) {
			if (i > 0) md += "\n";
			if (lines[i].length() > min_indent) {
				md += lines[i].substr(min_indent);
			}
		}
	}

	// Strip [csharp]...[/csharp] blocks entirely (keep only GDScript).
	int pos = 0;
	while ((pos = md.find("[csharp]", pos)) != -1) {
		int close = md.find("[/csharp]", pos);
		if (close == -1) break;
		md = md.substr(0, pos) + md.substr(close + 9);
	}

	// Strip [codeblocks]/[/codeblocks] wrappers.
	md = md.replace("[codeblocks]", "").replace("[/codeblocks]", "");

	// Convert [codeblock]...[/codeblock] and [gdscript]...[/gdscript] to
	// markdown fences.
	const char *code_tags[] = { "[codeblock]", "[gdscript]" };
	for (const char *open_tag : code_tags) {
		String close_tag = String(open_tag).replace("[", "[/");
		int open_len = String(open_tag).length();
		int close_len = close_tag.length();
		pos = 0;
		while ((pos = md.find(open_tag, pos)) != -1) {
			int content_start = pos + open_len;
			int close_pos = md.find(close_tag, content_start);
			if (close_pos == -1) break;
			String content = md.substr(content_start, close_pos - content_start);

			// Dedent: find the minimum leading whitespace across non-empty lines.
			Vector<String> lines = content.split("\n");
			int min_indent = INT_MAX;
			for (int i = 0; i < lines.size(); i++) {
				if (lines[i].strip_edges().is_empty()) continue;
				int indent = 0;
				while (indent < lines[i].length() && (lines[i][indent] == '\t' || lines[i][indent] == ' ')) {
					indent++;
				}
				if (indent < min_indent) {
					min_indent = indent;
				}
			}
			if (min_indent == INT_MAX) {
				min_indent = 0;
			}
			String dedented;
			for (int i = 0; i < lines.size(); i++) {
				if (i > 0) dedented += "\n";
				if (lines[i].length() > min_indent) {
					dedented += lines[i].substr(min_indent);
				}
			}
			dedented = dedented.strip_edges();

			md = md.substr(0, pos) + "\n```gdscript\n" + dedented + "\n```\n" + md.substr(close_pos + close_len);
		}
	}

	// Replace [lb]/[rb] with literal brackets before inline tag processing.
	md = md.replace("[lb]", "\x01LB\x01").replace("[rb]", "\x01RB\x01");

	md = md.replace("[b]", "**").replace("[/b]", "**");
	md = md.replace("[i]", "*").replace("[/i]", "*");
	md = md.replace("[u]", "").replace("[/u]", "");
	md = md.replace("[code]", "`").replace("[/code]", "`");
	md = md.replace("[br]", "\n");

	// [url=link]text[/url] -> [text](link)
	pos = 0;
	while ((pos = md.find("[url=", pos)) != -1) {
		int url_end = md.find("]", pos);
		if (url_end == -1) break;
		String url = md.substr(pos + 5, url_end - pos - 5);
		int close = md.find("[/url]", url_end);
		if (close == -1) break;
		String text = md.substr(url_end + 1, close - url_end - 1);
		md = md.substr(0, pos) + "[" + text + "](" + url + ")" + md.substr(close + 6);
	}

	// [url]link[/url] -> link
	pos = 0;
	while ((pos = md.find("[url]", pos)) != -1) {
		int close = md.find("[/url]", pos);
		if (close == -1) break;
		String url = md.substr(pos + 5, close - pos - 5);
		md = md.substr(0, pos) + url + md.substr(close + 6);
	}

	// [param name] -> `name`
	pos = 0;
	while ((pos = md.find("[param ", pos)) != -1) {
		int end = md.find("]", pos);
		if (end == -1) break;
		String param_name = md.substr(pos + 7, end - pos - 7);
		md = md.substr(0, pos) + "`" + param_name + "`" + md.substr(end + 1);
	}

	// [ClassName] -> `ClassName` (simple reference)
	pos = 0;
	while ((pos = md.find("[", pos)) != -1) {
		// Check if we're inside a code fence (``` block).
		int fence_start = md.rfind("```", pos);
		if (fence_start != -1) {
			int fence_count = 0;
			int search = 0;
			while ((search = md.find("```", search)) != -1 && search < pos) {
				fence_count++;
				search += 3;
			}
			if (fence_count % 2 == 1) {
				int fence_end = md.find("```", pos);
				if (fence_end != -1) {
					pos = fence_end + 3;
				} else {
					pos++;
				}
				continue;
			}
		}

		if (pos > 0 && md[pos - 1] == '`') {
			pos++;
			continue;
		}
		int end = md.find("]", pos);
		if (end == -1) break;

		// Skip markdown links [text](url).
		if (end + 1 < md.length() && md[end + 1] == '(') {
			pos = end + 1;
			continue;
		}

		String inner = md.substr(pos + 1, end - pos - 1);

		if (inner.begins_with("color") || inner.begins_with("/color") ||
				inner.begins_with("img") || inner.begins_with("/img")) {
			pos = end + 1;
			continue;
		}

		if (inner.begins_with("method ") || inner.begins_with("member ") ||
				inner.begins_with("signal ") || inner.begins_with("constant ") ||
				inner.begins_with("enum ") || inner.begins_with("annotation ") ||
				inner.begins_with("theme_item ")) {
			int space = inner.find(" ");
			String ref_name = inner.substr(space + 1);
			md = md.substr(0, pos) + "`" + ref_name + "`" + md.substr(end + 1);
			continue;
		}

		if (!inner.is_empty() && inner[0] >= 'A' && inner[0] <= 'Z') {
			md = md.substr(0, pos) + "`" + inner + "`" + md.substr(end + 1);
			continue;
		}

		pos = end + 1;
	}

	// Restore literal brackets from placeholders.
	md = md.replace("\x01LB\x01", "[").replace("\x01RB\x01", "]");

	// In markdown, a single newline doesn't produce a visible line break.
	// Double all single newlines (outside code fences) so they render properly.
	{
		String result;
		bool in_fence = false;
		int i = 0;
		while (i < md.length()) {
			if (i + 2 < md.length() && md[i] == '`' && md[i + 1] == '`' && md[i + 2] == '`') {
				in_fence = !in_fence;
				result += "```";
				i += 3;
				continue;
			}
			if (md[i] == '\n' && !in_fence) {
				// Check if the next char is already a newline (existing double).
				if (i + 1 < md.length() && md[i + 1] == '\n') {
					// Already a double newline — pass both through.
					result += "\n\n";
					i += 2;
					// Skip any extra consecutive newlines.
					while (i < md.length() && md[i] == '\n') {
						i++;
					}
				} else {
					result += "\n\n";
					i++;
				}
			} else {
				result += md[i];
				i++;
			}
		}
		md = result;
	}

	return md.strip_edges();
}

String doc_method_sig(const DocMethodData &p_method) {
	String sig = p_method.name + "(";
	for (int i = 0; i < p_method.arguments.size(); i++) {
		if (i > 0) sig += ", ";
		sig += p_method.arguments[i].name;
		if (!p_method.arguments[i].type.is_empty()) {
			sig += ": " + p_method.arguments[i].type;
		}
		if (!p_method.arguments[i].default_value.is_empty()) {
			sig += " = " + p_method.arguments[i].default_value;
		}
	}
	sig += ")";
	if (!p_method.return_type.is_empty() && p_method.return_type != "void") {
		sig += " -> " + p_method.return_type;
	}
	if (!p_method.qualifiers.is_empty()) {
		sig += " " + p_method.qualifiers;
	}
	return sig;
}

String generate_doc_markdown(const String &p_name, const DocClassData &p_doc, const String &p_parent) {
	String md;
	md += "# " + p_name + "\n\n";
	if (!p_parent.is_empty()) {
		md += "**Inherits:** `" + p_parent + "`\n\n";
	}
	if (!p_doc.brief_description.is_empty()) {
		md += bbcode_to_markdown(p_doc.brief_description) + "\n\n";
	}
	if (!p_doc.description.is_empty() && p_doc.description != p_doc.brief_description) {
		md += "## Description\n\n";
		md += bbcode_to_markdown(p_doc.description) + "\n\n";
	}

	// Tutorials.
	if (!p_doc.tutorials.is_empty()) {
		md += "## Tutorials\n\n";
		for (const DocTutorialData &tut : p_doc.tutorials) {
			if (!tut.link.is_empty()) {
				String title = tut.title.is_empty() ? tut.link : tut.title;
				md += "- [" + title + "](" + tut.link + ")\n";
			}
		}
		md += "\n";
	}

	// Properties overview table.
	if (!p_doc.properties.is_empty()) {
		md += "## Properties\n\n";
		md += "| Type | Name | Default |\n";
		md += "|------|------|---------|\n";
		for (const DocPropertyData &prop : p_doc.properties) {
			String def = prop.default_value.is_empty() ? "" : "`" + prop.default_value + "`";
			String type = prop.enumeration.is_empty() ? prop.type : prop.enumeration;
			md += "| `" + type + "` | **" + prop.name + "** | " + def + " |\n";
		}
		md += "\n";
	}

	// Methods overview table.
	if (!p_doc.methods.is_empty()) {
		md += "## Methods\n\n";
		md += "| Return | Signature |\n";
		md += "|--------|-----------|\n";
		for (const DocMethodData &m : p_doc.methods) {
			String ret = m.return_type.is_empty() ? "void" : m.return_type;
			md += "| `" + ret + "` | **" + doc_method_sig(m) + "** |\n";
		}
		md += "\n";
	}

	// Enumerations.
	{
		Vector<String> enum_order;
		HashMap<String, Vector<const DocConstantData *>> enum_members;
		for (const DocConstantData &c : p_doc.constants) {
			if (!c.enumeration.is_empty()) {
				if (!enum_members.has(c.enumeration)) {
					enum_order.push_back(c.enumeration);
				}
				enum_members[c.enumeration].push_back(&c);
			}
		}
		for (const KeyValue<String, DocEnumData> &kv : p_doc.enums) {
			if (!enum_members.has(kv.key)) {
				enum_order.push_back(kv.key);
			}
		}
		if (!enum_order.is_empty()) {
			md += "## Enumerations\n\n";
			for (const String &enum_name : enum_order) {
				md += "### " + enum_name + "\n\n";
				if (p_doc.enums.has(enum_name) && !p_doc.enums[enum_name].description.is_empty()) {
					md += bbcode_to_markdown(p_doc.enums[enum_name].description) + "\n\n";
				}
				if (enum_members.has(enum_name)) {
					for (const DocConstantData *c : enum_members[enum_name]) {
						md += "- **" + c->name + "**";
						if (!c->value.is_empty()) {
							md += " = `" + c->value + "`";
						}
						if (!c->description.is_empty()) {
							md += " - " + bbcode_to_markdown(c->description);
						}
						md += "\n";
					}
				}
				md += "\n";
			}
		}
	}

	// Constants (non-enum).
	{
		bool has_standalone = false;
		for (const DocConstantData &c : p_doc.constants) {
			if (c.enumeration.is_empty()) {
				has_standalone = true;
				break;
			}
		}
		if (has_standalone) {
			md += "## Constants\n\n";
			for (const DocConstantData &c : p_doc.constants) {
				if (!c.enumeration.is_empty()) continue;
				md += "### " + c.name + "\n\n";
				md += "```gdscript\n" + c.name;
				if (!c.value.is_empty()) {
					md += " = " + c.value;
				}
				md += "\n```\n\n";
				if (!c.description.is_empty()) {
					md += bbcode_to_markdown(c.description) + "\n\n";
				}
			}
		}
	}

	// Signals.
	if (!p_doc.signals.is_empty()) {
		md += "## Signals\n\n";
		for (const DocMethodData &s : p_doc.signals) {
			md += "### " + s.name + "\n\n";
			md += "```gdscript\nsignal " + doc_method_sig(s) + "\n```\n\n";
			if (!s.description.is_empty()) {
				md += bbcode_to_markdown(s.description) + "\n\n";
			}
		}
	}

	// Constructors.
	if (!p_doc.constructors.is_empty()) {
		md += "## Constructors\n\n";
		for (const DocMethodData &m : p_doc.constructors) {
			md += "### " + m.name + "\n\n";
			md += "```gdscript\n" + doc_method_sig(m) + "\n```\n\n";
			if (!m.description.is_empty()) {
				md += bbcode_to_markdown(m.description) + "\n\n";
			}
		}
	}

	// Property descriptions.
	if (!p_doc.properties.is_empty()) {
		md += "## Property Descriptions\n\n";
		for (const DocPropertyData &prop : p_doc.properties) {
			md += "### " + prop.name + "\n\n";
			String prop_type = prop.enumeration.is_empty() ? prop.type : prop.enumeration;
			md += "```gdscript\n" + prop_type + " " + prop.name;
			if (!prop.default_value.is_empty()) {
				md += " = " + prop.default_value;
			}
			md += "\n```\n\n";
			if (!prop.description.is_empty()) {
				md += bbcode_to_markdown(prop.description) + "\n\n";
			}
		}
	}

	// Method descriptions.
	if (!p_doc.methods.is_empty()) {
		md += "## Method Descriptions\n\n";
		for (const DocMethodData &m : p_doc.methods) {
			md += "### " + m.name + "\n\n";
			md += "```gdscript\nfunc " + doc_method_sig(m) + "\n```\n\n";
			if (m.is_deprecated) {
				md += "*Deprecated";
				if (!m.deprecated_message.is_empty()) {
					md += ": " + bbcode_to_markdown(m.deprecated_message);
				}
				md += "*\n\n";
			}
			if (!m.description.is_empty()) {
				md += bbcode_to_markdown(m.description) + "\n\n";
			}
		}
	}

	// Operators.
	if (!p_doc.operators.is_empty()) {
		md += "## Operators\n\n";
		for (const DocMethodData &m : p_doc.operators) {
			md += "### " + m.name + "\n\n";
			md += "```gdscript\n" + doc_method_sig(m) + "\n```\n\n";
			if (!m.description.is_empty()) {
				md += bbcode_to_markdown(m.description) + "\n\n";
			}
		}
	}

	return md;
}

String generate_function_doc_markdown(const String &p_name, const DocMethodData &p_method) {
	String md;
	md += "# " + p_name + "\n\n";
	md += "```gdscript\nfunc " + doc_method_sig(p_method) + "\n```\n\n";
	if (!p_method.description.is_empty()) {
		md += bbcode_to_markdown(p_method.description) + "\n\n";
	}
	return md;
}

} // namespace lsp

#endif // HOMOT
