/**************************************************************************/
/*  lsp_hover.cpp                                                         */
/**************************************************************************/

#ifdef HOMOT

#include "lsp_hover.h"
#include "lsp_protocol.h"
#include "lsp_server.h"
#include "lsp_utils.h"

#include "../stubs/linterdb.h"

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_parser.h"
#include "modules/gdscript/gdscript_utility_functions.h"

#include "core/io/file_access.h"
#include "core/variant/variant.h"

using linter::DocClassData;
using linter::DocConstantData;
using linter::DocMethodData;
using linter::DocPropertyData;
using linter::LinterDB;

namespace lsp {

// ---------------------------------------------------------------------------
// Hover — internal helpers
// ---------------------------------------------------------------------------

static String _build_hover_text(const GDScriptParser::IdentifierNode *p_id) {
	String type_str = format_datatype(p_id->datatype);
	String name = p_id->name;

	switch (p_id->source) {
		case GDScriptParser::IdentifierNode::FUNCTION_PARAMETER:
			return vformat("(parameter) %s: %s", name, type_str);
		case GDScriptParser::IdentifierNode::LOCAL_VARIABLE:
			return vformat("(local variable) %s: %s", name, type_str);
		case GDScriptParser::IdentifierNode::LOCAL_CONSTANT:
			return vformat("(local constant) %s: %s", name, type_str);
		case GDScriptParser::IdentifierNode::LOCAL_ITERATOR:
			return vformat("(iterator) %s: %s", name, type_str);
		case GDScriptParser::IdentifierNode::LOCAL_BIND:
			return vformat("(bind) %s: %s", name, type_str);
		case GDScriptParser::IdentifierNode::MEMBER_VARIABLE:
		case GDScriptParser::IdentifierNode::INHERITED_VARIABLE:
		case GDScriptParser::IdentifierNode::STATIC_VARIABLE:
			return vformat("(property) %s: %s", name, type_str);
		case GDScriptParser::IdentifierNode::MEMBER_CONSTANT:
			return vformat("(constant) %s: %s", name, type_str);
		case GDScriptParser::IdentifierNode::MEMBER_FUNCTION: {
			if (p_id->function_source) {
				String sig = "func " + name + "(";
				for (int i = 0; i < p_id->function_source->parameters.size(); i++) {
					if (i > 0) sig += ", ";
					const GDScriptParser::ParameterNode *param = p_id->function_source->parameters[i];
					sig += param->identifier->name;
					if (param->datatype_specifier || param->get_datatype().is_set()) {
						sig += ": " + format_datatype(param->get_datatype());
					}
				}
				sig += ")";
				GDScriptParser::DataType ret = p_id->function_source->get_datatype();
				if (ret.is_set() && ret.builtin_type != Variant::NIL) {
					sig += " -> " + format_datatype(ret);
				}
				return sig;
			}
			return vformat("(method) %s: %s", name, type_str);
		}
		case GDScriptParser::IdentifierNode::MEMBER_SIGNAL:
			return vformat("(signal) %s", name);
		case GDScriptParser::IdentifierNode::MEMBER_CLASS:
			return vformat("(class) %s", name);
		case GDScriptParser::IdentifierNode::NATIVE_CLASS:
			return vformat("(native class) %s", name);
		default:
			if (type_str != "Variant") {
				return vformat("%s: %s", name, type_str);
			}
			return name;
	}
}

// ---------------------------------------------------------------------------
// Hover — main handler
// ---------------------------------------------------------------------------

Dictionary HoverHandler::handle(const Variant &p_id, const Dictionary &p_params) {
	Dictionary td = p_params["textDocument"];
	String uri = td["uri"];
	Dictionary pos_dict = p_params["position"];
	int line = pos_dict["line"];
	int character = pos_dict["character"];

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

	String hover_text;
	if (ident) {
		hover_text = _build_hover_text(ident);
	}

	// Fallback for native method calls (e.g. n.add_child).
	if (ident && (hover_text.is_empty() || hover_text == String(ident->name))) {
		String word = get_word_at_position(source, line, character);
		if (!word.is_empty()) {
			Vector<String> lines = source.split("\n");
			if (line < lines.size()) {
				String line_text = lines[line];
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
							const GDScriptParser::ClassNode::Member &member = cls->get_member(StringName(base_name));
							if (member.type == GDScriptParser::ClassNode::Member::VARIABLE) {
								base_type = member.variable->get_datatype();
							} else if (member.type == GDScriptParser::ClassNode::Member::SIGNAL) {
								base_type.kind = GDScriptParser::DataType::BUILTIN;
								base_type.builtin_type = Variant::SIGNAL;
								base_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
							}
						}

						if (!base_type.is_set() && cls) {
							for (int i = 0; i < cls->members.size(); i++) {
								if (base_type.is_set()) break;
								const GDScriptParser::ClassNode::Member &member = cls->members[i];
								if (member.type != GDScriptParser::ClassNode::Member::FUNCTION) continue;
								const GDScriptParser::FunctionNode *func = member.function;
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
						if (!base_type.is_set() && db && db->class_exists(StringName(base_name))) {
							MethodInfo mi;
							if (db->get_method_info(StringName(base_name), StringName(word), &mi)) {
								hover_text = "func " + base_name + "." + word + method_signature(mi);
							}
						}

						if (hover_text.is_empty() || hover_text == String(ident->name)) {
							if (base_type.is_set() && base_type.kind == GDScriptParser::DataType::NATIVE && db) {
								MethodInfo mi;
								if (db->get_method_info(base_type.native_type, StringName(word), &mi)) {
									hover_text = "func " + String(base_type.native_type) + "." + word + method_signature(mi);
								}
							} else if (base_type.is_set() && base_type.kind == GDScriptParser::DataType::BUILTIN) {
								if (Variant::has_builtin_method(base_type.builtin_type, StringName(word))) {
									MethodInfo mi = Variant::get_builtin_method_info(base_type.builtin_type, StringName(word));
									hover_text = "func " + Variant::get_type_name(base_type.builtin_type) + "." + word + method_signature(mi);
								}
							} else if (base_type.is_set() && base_type.kind == GDScriptParser::DataType::CLASS && base_type.class_type) {
								const GDScriptParser::ClassNode *target_cls = base_type.class_type;
								if (target_cls->has_member(StringName(word))) {
									const GDScriptParser::ClassNode::Member &m = target_cls->get_member(StringName(word));
									if (m.type == GDScriptParser::ClassNode::Member::FUNCTION) {
										const GDScriptParser::FunctionNode *fn = m.function;
										String sig = "func " + word + "(";
										for (int k = 0; k < fn->parameters.size(); k++) {
											if (k > 0) sig += ", ";
											sig += fn->parameters[k]->identifier->name;
											GDScriptParser::DataType pt = fn->parameters[k]->get_datatype();
											if (pt.is_set() && !pt.is_variant()) {
												sig += ": " + pt.to_string();
											}
										}
										sig += ")";
										GDScriptParser::DataType rt = fn->get_datatype();
										if (rt.is_set() && !rt.is_variant()) {
											sig += " -> " + rt.to_string();
										}
										hover_text = sig;
									} else if (m.type == GDScriptParser::ClassNode::Member::VARIABLE) {
										GDScriptParser::DataType vt = m.variable->get_datatype();
										hover_text = "(property) " + word + ": " + (vt.is_set() ? vt.to_string() : "Variant");
									}
								}
							}
						}
					}
				}
			}
		}
	}

	// Fallback for bare function calls.
	if (ident && (hover_text.is_empty() || hover_text == String(ident->name))) {
		String word = String(ident->name);
		const GDScriptParser::ClassNode *cls = parser.get_tree();

		if (cls && cls->has_member(StringName(word))) {
			const GDScriptParser::ClassNode::Member &m = cls->get_member(StringName(word));
			if (m.type == GDScriptParser::ClassNode::Member::FUNCTION) {
				const GDScriptParser::FunctionNode *fn = m.function;
				String sig = "func " + word + "(";
				for (int i = 0; i < fn->parameters.size(); i++) {
					if (i > 0) sig += ", ";
					sig += fn->parameters[i]->identifier->name;
					GDScriptParser::DataType pt = fn->parameters[i]->get_datatype();
					if (pt.is_set() && !pt.is_variant()) {
						sig += ": " + pt.to_string();
					}
				}
				sig += ")";
				GDScriptParser::DataType rt = fn->get_datatype();
				if (rt.is_set() && !rt.is_variant()) {
					sig += " -> " + rt.to_string();
				}
				hover_text = sig;
			} else if (m.type == GDScriptParser::ClassNode::Member::VARIABLE) {
				GDScriptParser::DataType vt = m.variable->get_datatype();
				hover_text = "(property) " + word + ": " + (vt.is_set() ? vt.to_string() : "Variant");
			} else if (m.type == GDScriptParser::ClassNode::Member::SIGNAL) {
				hover_text = "(signal) " + word;
			}
		}

		if ((hover_text.is_empty() || hover_text == word) && cls) {
			LinterDB *db = LinterDB::get_singleton();
			if (db) {
				GDScriptParser::DataType base_type = cls->base_type;
				while (base_type.is_set() && base_type.kind == GDScriptParser::DataType::NATIVE) {
					MethodInfo mi;
					if (db->get_method_info(base_type.native_type, StringName(word), &mi)) {
						hover_text = "func " + String(base_type.native_type) + "." + word + method_signature(mi);
						break;
					}
					StringName parent = db->get_parent_class(base_type.native_type);
					if (parent == StringName() || parent == base_type.native_type) break;
					base_type.native_type = parent;
				}
			}
		}

		if (hover_text.is_empty() || hover_text == word) {
			if (Variant::has_utility_function(StringName(word))) {
				MethodInfo mi = Variant::get_utility_function_info(StringName(word));
				hover_text = "func " + word + method_signature(mi);
			} else if (GDScriptUtilityFunctions::function_exists(StringName(word))) {
				MethodInfo mi = GDScriptUtilityFunctions::get_function_info(StringName(word));
				hover_text = "func " + word + method_signature(mi);
			}
		}
	}

	// Text-based fallback for global/native/built-in class names.
	if (hover_text.is_empty()) {
		String word = get_word_at_position(source, line, character);
		if (!word.is_empty()) {
			if (server.class_to_path.has(word)) {
				hover_text = vformat("(global class) %s\n%s", word, server.class_to_path[word]);
			} else {
				LinterDB *db = LinterDB::get_singleton();
				if (db && db->class_exists(StringName(word))) {
					StringName parent = db->get_parent_class(StringName(word));
					if (parent != StringName()) {
						hover_text = vformat("(native class) %s extends %s", word, String(parent));
					} else {
						hover_text = vformat("(native class) %s", word);
					}
				} else {
					for (int i = 0; i < Variant::VARIANT_MAX; i++) {
						if (Variant::get_type_name((Variant::Type)i) == word) {
							hover_text = vformat("(built-in type) %s", word);
							break;
						}
					}
				}
			}
		}
	}

	if (hover_text.is_empty()) {
		return make_response(p_id, Variant());
	}

	// Look up documentation.
	String hover_doc;
	{
		LinterDB *db = LinterDB::get_singleton();
		if (db) {
			String word = ident ? String(ident->name) : get_word_at_position(source, line, character);
			if (!word.is_empty()) {
				if (db->class_exists(StringName(word))) {
					const DocClassData *class_doc = db->get_class_doc(StringName(word));
					if (class_doc && !class_doc->brief_description.is_empty()) {
						hover_doc = bbcode_to_markdown(class_doc->brief_description);
						if (!class_doc->description.is_empty() && class_doc->description != class_doc->brief_description) {
							hover_doc += "\n\n" + bbcode_to_markdown(class_doc->description);
						}
					}
				}

				if (hover_doc.is_empty()) {
					const DocClassData *bt_doc = db->get_builtin_type_doc(word);
					if (bt_doc && !bt_doc->brief_description.is_empty()) {
						hover_doc = bbcode_to_markdown(bt_doc->brief_description);
						if (!bt_doc->description.is_empty() && bt_doc->description != bt_doc->brief_description) {
							hover_doc += "\n\n" + bbcode_to_markdown(bt_doc->description);
						}
					}
				}

				if (hover_doc.is_empty()) {
					const DocMethodData *umd = db->get_utility_function_doc(StringName(word));
					if (umd && !umd->description.is_empty()) {
						hover_doc = bbcode_to_markdown(umd->description);
					}
				}

				if (hover_doc.is_empty() && ident) {
					StringName native_class;

					if (ident->source == GDScriptParser::IdentifierNode::MEMBER_FUNCTION ||
							ident->source == GDScriptParser::IdentifierNode::MEMBER_VARIABLE ||
							ident->source == GDScriptParser::IdentifierNode::INHERITED_VARIABLE ||
							ident->source == GDScriptParser::IdentifierNode::STATIC_VARIABLE ||
							ident->source == GDScriptParser::IdentifierNode::MEMBER_SIGNAL ||
							ident->source == GDScriptParser::IdentifierNode::MEMBER_CONSTANT) {
						const GDScriptParser::ClassNode *cls = parser.get_tree();
						if (cls) {
							GDScriptParser::DataType base_type = cls->base_type;
							if (base_type.is_set() && base_type.kind == GDScriptParser::DataType::NATIVE) {
								native_class = base_type.native_type;
							}
						}
					}

					if (native_class == StringName() && ident->datatype.is_set() &&
							ident->datatype.kind == GDScriptParser::DataType::NATIVE) {
						native_class = ident->datatype.native_type;
					}

					if (native_class != StringName()) {
						const DocMethodData *md = db->get_method_doc(native_class, StringName(word));
						if (md && !md->description.is_empty()) {
							hover_doc = bbcode_to_markdown(md->description);
						}
						if (hover_doc.is_empty()) {
							const DocPropertyData *pd = db->get_property_doc(native_class, StringName(word));
							if (pd && !pd->description.is_empty()) {
								hover_doc = bbcode_to_markdown(pd->description);
							}
						}
						if (hover_doc.is_empty()) {
							const DocMethodData *sd = db->get_signal_doc(native_class, StringName(word));
							if (sd && !sd->description.is_empty()) {
								hover_doc = bbcode_to_markdown(sd->description);
							}
						}
						if (hover_doc.is_empty()) {
							const DocConstantData *cd = db->get_constant_doc(native_class, StringName(word));
							if (cd && !cd->description.is_empty()) {
								hover_doc = bbcode_to_markdown(cd->description);
							}
						}
					}
				}

				// Fallback: extract class name from hover_text pattern.
				if (hover_doc.is_empty() && hover_text.contains(".")) {
					int dot_pos_ht = hover_text.find(".");
					if (dot_pos_ht != -1) {
						int cls_start = dot_pos_ht - 1;
						while (cls_start >= 0 && ((hover_text[cls_start] >= 'a' && hover_text[cls_start] <= 'z') ||
								(hover_text[cls_start] >= 'A' && hover_text[cls_start] <= 'Z') ||
								(hover_text[cls_start] >= '0' && hover_text[cls_start] <= '9') ||
								hover_text[cls_start] == '_')) {
							cls_start--;
						}
						cls_start++;
						String cls_name = hover_text.substr(cls_start, dot_pos_ht - cls_start);
						if (!cls_name.is_empty() && db->class_exists(StringName(cls_name))) {
							const DocMethodData *md = db->get_method_doc(StringName(cls_name), StringName(word));
							if (md && !md->description.is_empty()) {
								hover_doc = bbcode_to_markdown(md->description);
							}
							if (hover_doc.is_empty()) {
								const DocPropertyData *pd = db->get_property_doc(StringName(cls_name), StringName(word));
								if (pd && !pd->description.is_empty()) {
									hover_doc = bbcode_to_markdown(pd->description);
								}
							}
							if (hover_doc.is_empty()) {
								const DocMethodData *sd = db->get_signal_doc(StringName(cls_name), StringName(word));
								if (sd && !sd->description.is_empty()) {
									hover_doc = bbcode_to_markdown(sd->description);
								}
							}
						}
						if (hover_doc.is_empty() && !cls_name.is_empty()) {
							const DocClassData *bt_doc = db->get_builtin_type_doc(cls_name);
							if (bt_doc) {
								const DocMethodData *md = bt_doc->find_method(word);
								if (md && !md->description.is_empty()) {
									hover_doc = bbcode_to_markdown(md->description);
								}
								if (hover_doc.is_empty()) {
									const DocPropertyData *pd = bt_doc->find_property(word);
									if (pd && !pd->description.is_empty()) {
										hover_doc = bbcode_to_markdown(pd->description);
									}
								}
								if (hover_doc.is_empty()) {
									const DocConstantData *cd = bt_doc->find_constant(word);
									if (cd && !cd->description.is_empty()) {
										hover_doc = bbcode_to_markdown(cd->description);
									}
								}
							}
						}
					}
				}
			}
		}
	}

	// Build Hover response.
	String markdown = "```gdscript\n" + hover_text + "\n```";
	if (!hover_doc.is_empty()) {
		markdown += "\n\n---\n\n" + hover_doc;
	}

	Dictionary contents;
	contents["kind"] = "markdown";
	contents["value"] = markdown;

	Dictionary hover;
	hover["contents"] = contents;

	if (ident) {
		Range range;
		range.start.line = MAX(0, ident->start_line - 1);
		range.start.character = parser_column_to_lsp(source, ident->start_line, ident->start_column);
		range.end.line = MAX(0, ident->end_line - 1);
		range.end.character = parser_column_to_lsp(source, ident->end_line, ident->end_column);
		hover["range"] = range.to_dict();
	}

	return make_response(p_id, hover);
}

} // namespace lsp

#endif // HOMOT
