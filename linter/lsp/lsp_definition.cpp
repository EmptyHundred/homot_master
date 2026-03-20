/**************************************************************************/
/*  lsp_definition.cpp                                                    */
/**************************************************************************/

#ifdef HOMOT

#include "lsp_definition.h"
#include "lsp_protocol.h"
#include "lsp_server.h"
#include "lsp_utils.h"

#include "../stubs/linterdb.h"

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_parser.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/variant/variant.h"

using linter::DocClassData;
using linter::DocMethodData;
using linter::LinterDB;

namespace lsp {

// ---------------------------------------------------------------------------
// Definition — internal helpers
// ---------------------------------------------------------------------------

// Given a parser node's line number, determine which file it belongs to.
static String _resolve_node_path(
		const GDScriptParser::Node *p_node,
		const String &p_current_path,
		GDScriptParser &p_parser) {
	const GDScriptParser::ClassNode *current_tree = p_parser.get_tree();
	if (current_tree && p_node->start_line >= current_tree->start_line && p_node->start_line <= current_tree->end_line) {
		// Could be current file, but verify it's not from a dependency.
	}

	for (const KeyValue<String, Ref<GDScriptParserRef>> &dep : p_parser.get_depended_parsers()) {
		if (dep.value.is_null() || dep.value->get_parser() == nullptr) {
			continue;
		}
		const GDScriptParser::ClassNode *dep_tree = dep.value->get_parser()->get_tree();
		if (!dep_tree) {
			continue;
		}
		for (int i = 0; i < dep_tree->members.size(); i++) {
			const GDScriptParser::ClassNode::Member &member = dep_tree->members[i];
			bool match = false;
			switch (member.type) {
				case GDScriptParser::ClassNode::Member::FUNCTION:
					match = (member.function == p_node);
					break;
				case GDScriptParser::ClassNode::Member::VARIABLE:
					match = (member.variable == p_node);
					break;
				case GDScriptParser::ClassNode::Member::CONSTANT:
					match = (member.constant == p_node);
					break;
				case GDScriptParser::ClassNode::Member::SIGNAL:
					match = (member.signal == p_node);
					break;
				case GDScriptParser::ClassNode::Member::CLASS:
					match = (member.m_class == p_node);
					break;
				default:
					break;
			}
			if (match) {
				return dep.key;
			}
		}
		if (dep_tree == p_node) {
			return dep.key;
		}
	}

	return p_current_path;
}

// Resolve an IdentifierNode's definition location.
static bool _resolve_identifier_location(
		const GDScriptParser::IdentifierNode *p_id,
		const String &p_current_path,
		const HashMap<String, String> &p_class_to_path,
		String &r_path, int &r_line, int &r_col,
		GDScriptParser *p_parser = nullptr) {

	switch (p_id->source) {
		case GDScriptParser::IdentifierNode::FUNCTION_PARAMETER: {
			if (p_id->parameter_source) {
				r_path = p_current_path;
				r_line = p_id->parameter_source->start_line;
				r_col = p_id->parameter_source->start_column;
				return true;
			}
		} break;
		case GDScriptParser::IdentifierNode::LOCAL_VARIABLE:
		case GDScriptParser::IdentifierNode::LOCAL_CONSTANT:
		case GDScriptParser::IdentifierNode::LOCAL_ITERATOR:
		case GDScriptParser::IdentifierNode::LOCAL_BIND: {
			const GDScriptParser::Node *src = nullptr;
			switch (p_id->source) {
				case GDScriptParser::IdentifierNode::LOCAL_VARIABLE:
					src = p_id->variable_source;
					break;
				case GDScriptParser::IdentifierNode::LOCAL_CONSTANT:
					src = p_id->constant_source;
					break;
				case GDScriptParser::IdentifierNode::LOCAL_ITERATOR:
				case GDScriptParser::IdentifierNode::LOCAL_BIND:
					src = p_id->bind_source;
					break;
				default:
					break;
			}
			if (src) {
				r_path = p_current_path;
				r_line = src->start_line;
				r_col = src->start_column;
				return true;
			}
		} break;
		case GDScriptParser::IdentifierNode::MEMBER_VARIABLE:
		case GDScriptParser::IdentifierNode::INHERITED_VARIABLE:
		case GDScriptParser::IdentifierNode::STATIC_VARIABLE: {
			if (p_id->variable_source) {
				r_path = p_parser ? _resolve_node_path(p_id->variable_source, p_current_path, *p_parser) : p_current_path;
				r_line = p_id->variable_source->start_line;
				r_col = p_id->variable_source->start_column;
				return true;
			}
		} break;
		case GDScriptParser::IdentifierNode::MEMBER_CONSTANT: {
			if (p_id->constant_source) {
				r_path = p_parser ? _resolve_node_path(p_id->constant_source, p_current_path, *p_parser) : p_current_path;
				r_line = p_id->constant_source->start_line;
				r_col = p_id->constant_source->start_column;
				return true;
			}
		} break;
		case GDScriptParser::IdentifierNode::MEMBER_FUNCTION: {
			if (p_id->function_source) {
				r_path = p_parser ? _resolve_node_path(p_id->function_source, p_current_path, *p_parser) : p_current_path;
				r_line = p_id->function_source->start_line;
				r_col = p_id->function_source->start_column;
				return true;
			}
		} break;
		case GDScriptParser::IdentifierNode::MEMBER_SIGNAL: {
			if (p_id->signal_source) {
				r_path = p_parser ? _resolve_node_path(p_id->signal_source, p_current_path, *p_parser) : p_current_path;
				r_line = p_id->signal_source->start_line;
				r_col = p_id->signal_source->start_column;
				return true;
			}
		} break;
		case GDScriptParser::IdentifierNode::MEMBER_CLASS: {
			if (p_id->datatype.kind == GDScriptParser::DataType::CLASS && p_id->datatype.class_type) {
				r_path = p_parser ? _resolve_node_path(p_id->datatype.class_type, p_current_path, *p_parser) : p_current_path;
				r_line = p_id->datatype.class_type->start_line;
				r_col = p_id->datatype.class_type->start_column;
				return true;
			}
		} break;
		case GDScriptParser::IdentifierNode::NATIVE_CLASS: {
			return false;
		} break;
		default:
			break;
	}

	// Fallback: check if this is a global class name.
	String name_str = p_id->name;
	if (p_class_to_path.has(name_str)) {
		r_path = p_class_to_path[name_str];
		r_line = 1;
		r_col = 1;
		return true;
	}

	return false;
}

// Fallback: search the class AST for a member matching the identifier name.
static bool _find_member_definition_in_class(
		const GDScriptParser::ClassNode *p_class,
		const StringName &p_name,
		const String &p_current_path,
		String &r_path, int &r_line, int &r_col) {
	if (!p_class) return false;

	for (int i = 0; i < p_class->members.size(); i++) {
		const GDScriptParser::ClassNode::Member &member = p_class->members[i];
		switch (member.type) {
			case GDScriptParser::ClassNode::Member::FUNCTION: {
				if (member.function->identifier && member.function->identifier->name == p_name) {
					r_path = p_current_path;
					r_line = member.function->start_line;
					r_col = member.function->start_column;
					return true;
				}
			} break;
			case GDScriptParser::ClassNode::Member::VARIABLE: {
				if (member.variable->identifier && member.variable->identifier->name == p_name) {
					r_path = p_current_path;
					r_line = member.variable->start_line;
					r_col = member.variable->start_column;
					return true;
				}
			} break;
			case GDScriptParser::ClassNode::Member::CONSTANT: {
				if (member.constant->identifier && member.constant->identifier->name == p_name) {
					r_path = p_current_path;
					r_line = member.constant->start_line;
					r_col = member.constant->start_column;
					return true;
				}
			} break;
			case GDScriptParser::ClassNode::Member::SIGNAL: {
				if (member.signal->identifier && member.signal->identifier->name == p_name) {
					r_path = p_current_path;
					r_line = member.signal->start_line;
					r_col = member.signal->start_column;
					return true;
				}
			} break;
			case GDScriptParser::ClassNode::Member::ENUM: {
				if (member.m_enum->identifier && member.m_enum->identifier->name == p_name) {
					r_path = p_current_path;
					r_line = member.m_enum->start_line;
					r_col = member.m_enum->start_column;
					return true;
				}
			} break;
			case GDScriptParser::ClassNode::Member::CLASS: {
				if (member.m_class->identifier && member.m_class->identifier->name == p_name) {
					r_path = p_current_path;
					r_line = member.m_class->start_line;
					r_col = member.m_class->start_column;
					return true;
				}
				if (_find_member_definition_in_class(member.m_class, p_name, p_current_path, r_path, r_line, r_col)) {
					return true;
				}
			} break;
			default:
				break;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// Definition — doc file helpers
// ---------------------------------------------------------------------------

String DefinitionHandler::get_or_create_doc_file(const String &p_symbol) {
	if (server.doc_file_cache.has(p_symbol)) {
		return server.doc_file_cache[p_symbol];
	}

	if (server.doc_cache_dir.is_empty()) {
		server.doc_cache_dir = server.root_path.path_join(".godot").path_join("lsp_docs");
		Ref<DirAccess> da = DirAccess::open(server.root_path);
		if (da.is_valid()) {
			da->make_dir_recursive(server.doc_cache_dir);
		}
	}

	LinterDB *db = LinterDB::get_singleton();
	if (!db) return String();

	String markdown;

	if (db->class_exists(StringName(p_symbol))) {
		const DocClassData *doc = db->get_class_doc(StringName(p_symbol));
		if (doc) {
			String parent = db->get_parent_class(StringName(p_symbol));
			markdown = generate_doc_markdown(p_symbol, *doc, parent);
		}
	}

	if (markdown.is_empty()) {
		const DocClassData *doc = db->get_builtin_type_doc(p_symbol);
		if (doc) {
			markdown = generate_doc_markdown(p_symbol, *doc);
		}
	}

	if (markdown.is_empty()) {
		const DocMethodData *md = db->get_utility_function_doc(StringName(p_symbol));
		if (md) {
			markdown = generate_function_doc_markdown(p_symbol, *md);
		}
	}

	if (markdown.is_empty()) return String();

	String file_path = server.doc_cache_dir.path_join(p_symbol + ".md");
	Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::WRITE);
	if (f.is_valid()) {
		f->store_string(markdown);
		f->flush();
		f.unref();
		server.doc_file_cache[p_symbol] = file_path;
		return file_path;
	}
	return String();
}

int DefinitionHandler::find_doc_line(const String &p_file_path, const String &p_member) {
	if (p_member.is_empty()) return 0;
	Ref<FileAccess> f = FileAccess::open(p_file_path, FileAccess::READ);
	if (f.is_null()) return 0;
	String heading_target = "### " + p_member;
	String list_target = "- **" + p_member + "**";
	int line = 0;
	while (!f->eof_reached()) {
		String l = f->get_line().strip_edges();
		if (l.begins_with(heading_target) || l.begins_with(list_target)) {
			return line;
		}
		line++;
	}
	return 0;
}

// ---------------------------------------------------------------------------
// Definition — main handler
// ---------------------------------------------------------------------------

Dictionary DefinitionHandler::handle(const Variant &p_id, const Dictionary &p_params) {
	Dictionary td = p_params["textDocument"];
	String uri = td["uri"];
	Dictionary pos_dict = p_params["position"];
	int line = pos_dict["line"];       // 0-based
	int character = pos_dict["character"]; // 0-based

	String source;
	if (server.documents.has(uri)) {
		source = server.documents[uri].content;
	} else {
		source = FileAccess::get_file_as_string(Server::uri_to_path(uri));
	}

	if (source.is_empty()) {
		return make_response(p_id, Variant());
	}

	String file_path = Server::uri_to_path(uri);

	GDScriptParser parser;
	GDScriptAnalyzer analyzer(&parser);

	parser.parse(source, file_path, false);
	analyzer.analyze();

	int parser_line = line + 1;
	int parser_col = lsp_to_parser_column(source, line, character);

	const GDScriptParser::IdentifierNode *ident = find_identifier_at_position(parser.get_tree(), parser_line, parser_col);

	String def_path;
	int def_line = 0;
	int def_col = 0;

	if (ident) {
		if (!_resolve_identifier_location(ident, file_path, server.class_to_path, def_path, def_line, def_col, &parser)) {
			if (!_find_member_definition_in_class(parser.get_tree(), ident->name, file_path, def_path, def_line, def_col)) {
				// Search depended parsers (cross-file classes).
				bool found_in_dep = false;
				for (const KeyValue<String, Ref<GDScriptParserRef>> &dep : parser.get_depended_parsers()) {
					if (dep.value.is_null() || dep.value->get_parser() == nullptr) {
						continue;
					}
					if (_find_member_definition_in_class(dep.value->get_parser()->get_tree(), ident->name, dep.key, def_path, def_line, def_col)) {
						found_in_dep = true;
						break;
					}
				}
				if (!found_in_dep) {
					String symbol;
					String member;

					if (ident->source == GDScriptParser::IdentifierNode::NATIVE_CLASS) {
						symbol = ident->name;
					} else if (ident->datatype.is_set() && ident->datatype.kind == GDScriptParser::DataType::NATIVE) {
						symbol = ident->datatype.native_type;
					}

					// Check for native member.
					if (symbol.is_empty()) {
						const GDScriptParser::ClassNode *cls = parser.get_tree();
						if (cls) {
							GDScriptParser::DataType base_type = cls->base_type;
							LinterDB *db = LinterDB::get_singleton();
							if (db && base_type.is_set() && base_type.kind == GDScriptParser::DataType::NATIVE) {
								StringName check = base_type.native_type;
								while (check != StringName()) {
									if (db->has_method(check, ident->name, true) ||
											db->has_property(check, ident->name, true) ||
											db->has_signal(check, ident->name, true)) {
										symbol = check;
										member = ident->name;
										break;
									}
									StringName parent = db->get_parent_class(check);
									if (parent == StringName() || parent == check) break;
									check = parent;
								}
							}
						}
					}

					// Check for attribute access on a typed variable.
					if (symbol.is_empty()) {
						String word = String(ident->name);
						Vector<String> src_lines = source.split("\n");
						if (line < src_lines.size()) {
							String line_text = src_lines[line];
							int word_start = character;
							while (word_start > 0 && ((line_text[word_start - 1] >= 'a' && line_text[word_start - 1] <= 'z') ||
									(line_text[word_start - 1] >= 'A' && line_text[word_start - 1] <= 'Z') ||
									(line_text[word_start - 1] >= '0' && line_text[word_start - 1] <= '9') ||
									line_text[word_start - 1] == '_')) {
								word_start--;
							}
							int dot_pos = word_start - 1;
							while (dot_pos >= 0 && line_text[dot_pos] == ' ') {
								dot_pos--;
							}
							if (dot_pos >= 0 && line_text[dot_pos] == '.') {
								int base_end = dot_pos - 1;
								while (base_end >= 0 && line_text[base_end] == ' ') {
									base_end--;
								}
								int base_start = base_end;
								while (base_start >= 0 && ((line_text[base_start] >= 'a' && line_text[base_start] <= 'z') ||
										(line_text[base_start] >= 'A' && line_text[base_start] <= 'Z') ||
										(line_text[base_start] >= '0' && line_text[base_start] <= '9') ||
										line_text[base_start] == '_')) {
									base_start--;
								}
								base_start++;
								if (base_start <= base_end) {
									String base_name = line_text.substr(base_start, base_end - base_start + 1);

									GDScriptParser::DataType base_type;
									const GDScriptParser::ClassNode *cls = parser.get_tree();

									if (cls && cls->has_member(StringName(base_name))) {
										const GDScriptParser::ClassNode::Member &m = cls->get_member(StringName(base_name));
										if (m.type == GDScriptParser::ClassNode::Member::VARIABLE) {
											base_type = m.variable->get_datatype();
										} else if (m.type == GDScriptParser::ClassNode::Member::SIGNAL) {
											base_type.kind = GDScriptParser::DataType::BUILTIN;
											base_type.builtin_type = Variant::SIGNAL;
											base_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
										}
									}

									if (!base_type.is_set() && cls) {
										for (int i = 0; i < cls->members.size(); i++) {
											if (base_type.is_set()) break;
											const GDScriptParser::ClassNode::Member &m = cls->members[i];
											if (m.type != GDScriptParser::ClassNode::Member::FUNCTION) continue;
											const GDScriptParser::FunctionNode *func = m.function;
											if (parser_line < func->start_line || parser_line > func->end_line) continue;
											for (int j = 0; j < func->parameters.size(); j++) {
												if (func->parameters[j]->identifier->name == StringName(base_name)) {
													base_type = func->parameters[j]->get_datatype();
													break;
												}
											}
											if (!base_type.is_set() && func->body) {
												const GDScriptParser::SuiteNode *suite = func->body;
												while (suite && !base_type.is_set()) {
													for (int j = 0; j < suite->locals.size(); j++) {
														if (suite->locals[j].name == StringName(base_name)) {
															base_type = suite->locals[j].get_datatype();
															break;
														}
													}
													suite = suite->parent_block;
												}
											}
										}
									}

									LinterDB *db = LinterDB::get_singleton();

									if (base_type.is_set() && base_type.kind == GDScriptParser::DataType::NATIVE && db) {
										StringName check = base_type.native_type;
										while (check != StringName()) {
											if (db->has_method(check, StringName(word), true) ||
													db->has_property(check, StringName(word), true) ||
													db->has_signal(check, StringName(word), true)) {
												symbol = check;
												member = word;
												break;
											}
											StringName parent = db->get_parent_class(check);
											if (parent == StringName() || parent == check) break;
											check = parent;
										}
									}

									if (symbol.is_empty() && base_type.is_set() && base_type.kind == GDScriptParser::DataType::BUILTIN) {
										String type_name = Variant::get_type_name(base_type.builtin_type);
										if (Variant::has_builtin_method(base_type.builtin_type, StringName(word)) ||
												Variant::has_constant(base_type.builtin_type, StringName(word))) {
											symbol = type_name;
											member = word;
										}
									}

									if (symbol.is_empty()) {
										Variant::Type vt = GDScriptParser::get_builtin_type(StringName(base_name));
										if (vt != Variant::NIL && vt < Variant::VARIANT_MAX) {
											if (Variant::has_constant(vt, StringName(word)) ||
													Variant::has_builtin_method(vt, StringName(word))) {
												symbol = Variant::get_type_name(vt);
												member = word;
											}
										}
									}

									if (symbol.is_empty() && db && db->class_exists(StringName(base_name))) {
										if (db->has_method(StringName(base_name), StringName(word)) ||
												db->has_integer_constant(StringName(base_name), StringName(word)) ||
												db->has_enum(StringName(base_name), StringName(word)) ||
												db->has_property(StringName(base_name), StringName(word)) ||
												db->has_signal(StringName(base_name), StringName(word))) {
											symbol = base_name;
											member = word;
										}
									}

									// Script enum member.
									if (symbol.is_empty()) {
										Vector<const GDScriptParser::ClassNode *> dep_classes;
										Vector<String> dep_paths;
										for (const KeyValue<String, Ref<GDScriptParserRef>> &dep : parser.get_depended_parsers()) {
											if (dep.value.is_null() || dep.value->get_parser() == nullptr) continue;
											dep_classes.push_back(dep.value->get_parser()->get_tree());
											dep_paths.push_back(dep.key);
										}

										bool found_enum = false;
										for (int si = 0; si < 1 + dep_classes.size() && !found_enum; si++) {
											const GDScriptParser::ClassNode *search_cls = (si == 0) ? cls : dep_classes[si - 1];
											String search_path = (si == 0) ? file_path : dep_paths[si - 1];
											if (!search_cls) continue;
											for (int i = 0; i < search_cls->members.size(); i++) {
												const GDScriptParser::ClassNode::Member &m = search_cls->members[i];
												if (m.type == GDScriptParser::ClassNode::Member::ENUM && m.m_enum->identifier->name == StringName(base_name)) {
													for (int j = 0; j < m.m_enum->values.size(); j++) {
														if (m.m_enum->values[j].identifier->name == StringName(word)) {
															String target_source = (search_path == file_path) ? source : FileAccess::get_file_as_string(search_path);
															Location loc;
															loc.uri = Server::path_to_uri(search_path);
															loc.range.start.line = MAX(0, m.m_enum->values[j].identifier->start_line - 1);
															loc.range.start.character = target_source.is_empty() ? 0 : parser_column_to_lsp(target_source, m.m_enum->values[j].identifier->start_line, m.m_enum->values[j].identifier->start_column);
															loc.range.end = loc.range.start;
															return make_response(p_id, loc.to_dict());
														}
													}
													found_enum = true;
													break;
												}
											}
										}
									}

									// Chained access: Class.Enum.VALUE.
									if (symbol.is_empty()) {
										int prev_dot = base_start - 1;
										while (prev_dot >= 0 && line_text[prev_dot] == ' ') {
											prev_dot--;
										}
										if (prev_dot >= 0 && line_text[prev_dot] == '.') {
											int cls_end = prev_dot - 1;
											while (cls_end >= 0 && line_text[cls_end] == ' ') {
												cls_end--;
											}
											int cls_start = cls_end;
											while (cls_start >= 0 && ((line_text[cls_start] >= 'a' && line_text[cls_start] <= 'z') ||
													(line_text[cls_start] >= 'A' && line_text[cls_start] <= 'Z') ||
													(line_text[cls_start] >= '0' && line_text[cls_start] <= '9') ||
													line_text[cls_start] == '_')) {
												cls_start--;
											}
											cls_start++;
											if (cls_start <= cls_end) {
												String cls_name = line_text.substr(cls_start, cls_end - cls_start + 1);
												if (db && db->class_exists(StringName(cls_name))) {
													if (db->has_integer_constant(StringName(cls_name), StringName(word))) {
														symbol = cls_name;
														member = word;
													}
												}
												if (symbol.is_empty()) {
													if (cls && cls->identifier && cls->identifier->name == StringName(cls_name)) {
														symbol = cls_name;
														member = word;
													}
													if (symbol.is_empty() && server.class_to_path.has(cls_name)) {
														symbol = cls_name;
														member = word;
													}
												}
											}
										}
									}
								}
							}
						}
					}

					if (!symbol.is_empty()) {
						String doc_path = get_or_create_doc_file(symbol);
						if (!doc_path.is_empty()) {
							Location loc;
							loc.uri = Server::path_to_uri(doc_path);
							loc.range.start.line = 0;
							loc.range.start.character = 0;
							loc.range.end = loc.range.start;
							if (!member.is_empty()) {
								int member_line = find_doc_line(doc_path, member);
								if (member_line > 0) {
									loc.range.start.line = member_line;
									loc.range.end.line = member_line;
								}
							}
							return make_response(p_id, loc.to_dict());
						}
					}

					ident = nullptr; // Fall through to text-based lookup.
				}
			}
		}
	}

	if (!ident) {
		String word = get_word_at_position(source, line, character);
		if (!word.is_empty() && server.class_to_path.has(word)) {
			def_path = server.class_to_path[word];
			def_line = 1;
			def_col = 1;
		} else if (!word.is_empty()) {
			String doc_path = get_or_create_doc_file(word);
			if (doc_path.is_empty()) {
				return make_response(p_id, Variant());
			}
			Location loc;
			loc.uri = Server::path_to_uri(doc_path);
			loc.range.start.line = 0;
			loc.range.start.character = 0;
			loc.range.end = loc.range.start;
			return make_response(p_id, loc.to_dict());
		} else {
			return make_response(p_id, Variant());
		}
	}

	// Convert to LSP Location.
	String def_source;
	if (def_path == file_path) {
		def_source = source;
	} else {
		String def_uri = Server::path_to_uri(def_path);
		if (server.documents.has(def_uri)) {
			def_source = server.documents[def_uri].content;
		} else {
			def_source = FileAccess::get_file_as_string(def_path);
		}
	}

	Location loc;
	loc.uri = Server::path_to_uri(def_path);
	loc.range.start.line = MAX(0, def_line - 1);
	loc.range.start.character = def_source.is_empty() ? MAX(0, def_col - 1) : parser_column_to_lsp(def_source, def_line, def_col);
	loc.range.end.line = loc.range.start.line;
	loc.range.end.character = loc.range.start.character;

	return make_response(p_id, loc.to_dict());
}

} // namespace lsp

#endif // HOMOT
