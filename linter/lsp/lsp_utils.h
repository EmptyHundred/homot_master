/**************************************************************************/
/*  lsp_utils.h                                                           */
/**************************************************************************/
/*  Shared utility functions for LSP handlers — AST/position helpers,     */
/*  BBCode-to-Markdown conversion, method signatures, doc generation.     */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "modules/gdscript/gdscript_parser.h"

#include "core/string/ustring.h"

namespace linter {
struct DocClassData;
struct DocMethodData;
} // namespace linter

namespace lsp {

// --- Position / word helpers ---

String get_word_at_position(const String &p_source, int p_lsp_line, int p_lsp_character);
int lsp_to_parser_column(const String &p_source, int p_lsp_line, int p_lsp_character, int p_tab_size = 4);
int parser_column_to_lsp(const String &p_source, int p_parser_line, int p_parser_col, int p_tab_size = 4);

// --- AST helpers ---

bool node_contains_position(const GDScriptParser::Node *p_node, int p_line, int p_col);

const GDScriptParser::IdentifierNode *find_identifier_in_expression(
		const GDScriptParser::ExpressionNode *p_expr, int p_line, int p_col);
const GDScriptParser::IdentifierNode *find_identifier_in_suite(
		const GDScriptParser::SuiteNode *p_suite, int p_line, int p_col);
const GDScriptParser::IdentifierNode *find_identifier_at_position(
		const GDScriptParser::ClassNode *p_class, int p_line, int p_col);

// --- Signature / type formatting ---

String method_signature(const MethodInfo &p_mi);
String format_datatype(const GDScriptParser::DataType &p_type);

// --- BBCode / Markdown / Doc generation ---

String bbcode_to_markdown(const String &p_bbcode);
String doc_method_sig(const linter::DocMethodData &p_method);
String generate_doc_markdown(const String &p_name, const linter::DocClassData &p_doc, const String &p_parent = String());
String generate_function_doc_markdown(const String &p_name, const linter::DocMethodData &p_method);

} // namespace lsp

#endif // HOMOT
