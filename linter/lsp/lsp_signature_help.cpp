/**************************************************************************/
/*  lsp_signature_help.cpp                                                */
/**************************************************************************/

#ifdef HOMOT

#include "lsp_signature_help.h"
#include "lsp_protocol.h"
#include "lsp_server.h"
#include "lsp_utils.h"

#include "../stubs/linterdb.h"

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_parser.h"
#include "modules/gdscript/gdscript_utility_functions.h"

#include "core/io/file_access.h"
#include "core/variant/variant.h"

using linter::LinterDB;

namespace lsp {

// ---------------------------------------------------------------------------
// Signature Help — internal helpers
// ---------------------------------------------------------------------------

struct _CallContext {
	String func_name;
	String base_text; // e.g. "my_node" for "my_node.add_child("
	int active_param = 0;
	bool found = false;
};

static _CallContext _find_call_context(const String &p_source, int p_lsp_line, int p_lsp_character) {
	_CallContext ctx;

	Vector<String> lines = p_source.split("\n");
	if (p_lsp_line >= lines.size()) {
		return ctx;
	}

	String text_before;
	for (int i = 0; i < p_lsp_line; i++) {
		text_before += lines[i] + "\n";
	}
	text_before += lines[p_lsp_line].substr(0, p_lsp_character);

	int paren_depth = 0;
	int comma_count = 0;
	int scan_pos = text_before.length() - 1;

	while (scan_pos >= 0) {
		char32_t c = text_before[scan_pos];
		if (c == ')') {
			paren_depth++;
		} else if (c == '(') {
			if (paren_depth > 0) {
				paren_depth--;
			} else {
				break;
			}
		} else if (c == ',' && paren_depth == 0) {
			comma_count++;
		}
		scan_pos--;
	}

	if (scan_pos < 0) {
		return ctx;
	}

	ctx.active_param = comma_count;

	int end = scan_pos;
	int name_end = end - 1;
	while (name_end >= 0 && text_before[name_end] == ' ') {
		name_end--;
	}
	if (name_end < 0) {
		return ctx;
	}

	int name_start = name_end;
	while (name_start >= 0) {
		char32_t c = text_before[name_start];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
			name_start--;
		} else {
			break;
		}
	}
	name_start++;

	if (name_start > name_end) {
		return ctx;
	}

	ctx.func_name = text_before.substr(name_start, name_end - name_start + 1);

	int dot_pos = name_start - 1;
	while (dot_pos >= 0 && text_before[dot_pos] == ' ') {
		dot_pos--;
	}
	if (dot_pos >= 0 && text_before[dot_pos] == '.') {
		int base_end = dot_pos - 1;
		while (base_end >= 0 && text_before[base_end] == ' ') {
			base_end--;
		}
		if (base_end >= 0) {
			int base_start = base_end;
			while (base_start >= 0) {
				char32_t c = text_before[base_start];
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
					base_start--;
				} else {
					break;
				}
			}
			base_start++;
			if (base_start <= base_end) {
				ctx.base_text = text_before.substr(base_start, base_end - base_start + 1);
			}
		}
	}

	ctx.found = true;
	return ctx;
}

static SignatureInformation _sig_from_method_info(const String &p_name, const MethodInfo &p_mi) {
	SignatureInformation sig;
	String label = p_name + "(";
	int i = 0;
	for (const PropertyInfo &arg : p_mi.arguments) {
		if (i > 0) label += ", ";
		String param_text = arg.name;
		if (arg.type != Variant::NIL) {
			param_text += ": " + Variant::get_type_name(arg.type);
		} else if (arg.class_name != StringName()) {
			param_text += ": " + String(arg.class_name);
		}
		ParameterInformation pi;
		pi.label = param_text;
		sig.parameters.push_back(pi);
		label += param_text;
		i++;
	}
	label += ")";
	if (p_mi.return_val.type != Variant::NIL) {
		label += " -> " + Variant::get_type_name(p_mi.return_val.type);
	} else if (p_mi.return_val.class_name != StringName()) {
		label += " -> " + String(p_mi.return_val.class_name);
	}
	sig.label = label;
	return sig;
}

static SignatureInformation _sig_from_function_node(const GDScriptParser::FunctionNode *p_func) {
	SignatureInformation sig;
	String label = String(p_func->identifier->name) + "(";
	for (int i = 0; i < p_func->parameters.size(); i++) {
		if (i > 0) label += ", ";
		const GDScriptParser::ParameterNode *param = p_func->parameters[i];
		String param_text = param->identifier->name;
		GDScriptParser::DataType pt = param->get_datatype();
		if (pt.is_set() && !pt.is_variant()) {
			param_text += ": " + pt.to_string();
		}
		ParameterInformation pi;
		pi.label = param_text;
		sig.parameters.push_back(pi);
		label += param_text;
	}
	label += ")";
	GDScriptParser::DataType rt = p_func->get_datatype();
	if (rt.is_set() && !rt.is_variant()) {
		label += " -> " + rt.to_string();
	}
	sig.label = label;
	return sig;
}

// ---------------------------------------------------------------------------
// Signature Help — main handler
// ---------------------------------------------------------------------------

Dictionary SignatureHandler::handle(const Variant &p_id, const Dictionary &p_params) {
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

	_CallContext call_ctx = _find_call_context(source, line, character);
	if (!call_ctx.found || call_ctx.func_name.is_empty()) {
		return make_response(p_id, Variant());
	}

	LinterDB *db = LinterDB::get_singleton();
	SignatureHelp help;
	bool found_sig = false;

	if (!call_ctx.base_text.is_empty()) {
		// Method call: base.func(
		String file_path = Server::uri_to_path(uri);
		GDScriptParser parser;
		GDScriptAnalyzer analyzer(&parser);
		parser.parse(source, file_path, false);
		analyzer.analyze();

		const GDScriptParser::ClassNode *cls = parser.get_tree();
		GDScriptParser::DataType base_type;

		if (cls && cls->has_member(StringName(call_ctx.base_text))) {
			const GDScriptParser::ClassNode::Member &member = cls->get_member(StringName(call_ctx.base_text));
			if (member.type == GDScriptParser::ClassNode::Member::VARIABLE) {
				base_type = member.variable->get_datatype();
			}
		}

		if (!base_type.is_set() && db && db->class_exists(StringName(call_ctx.base_text))) {
			if (call_ctx.func_name == "new") {
				SignatureInformation sig;
				sig.label = call_ctx.base_text + ".new()";
				help.signatures.push_back(sig);
				found_sig = true;
			} else {
				MethodInfo mi;
				if (db->get_method_info(StringName(call_ctx.base_text), StringName(call_ctx.func_name), &mi)) {
					help.signatures.push_back(_sig_from_method_info(call_ctx.func_name, mi));
					found_sig = true;
				}
			}
		}

		if (!found_sig && base_type.is_set()) {
			if (base_type.kind == GDScriptParser::DataType::NATIVE && db) {
				MethodInfo mi;
				if (db->get_method_info(base_type.native_type, StringName(call_ctx.func_name), &mi)) {
					help.signatures.push_back(_sig_from_method_info(call_ctx.func_name, mi));
					found_sig = true;
				}
			} else if (base_type.kind == GDScriptParser::DataType::BUILTIN) {
				if (Variant::has_builtin_method(base_type.builtin_type, StringName(call_ctx.func_name))) {
					MethodInfo mi = Variant::get_builtin_method_info(base_type.builtin_type, StringName(call_ctx.func_name));
					help.signatures.push_back(_sig_from_method_info(call_ctx.func_name, mi));
					found_sig = true;
				}
			} else if (base_type.kind == GDScriptParser::DataType::CLASS && base_type.class_type) {
				const GDScriptParser::ClassNode *target_cls = base_type.class_type;
				if (target_cls->has_member(StringName(call_ctx.func_name))) {
					const GDScriptParser::ClassNode::Member &member = target_cls->get_member(StringName(call_ctx.func_name));
					if (member.type == GDScriptParser::ClassNode::Member::FUNCTION) {
						help.signatures.push_back(_sig_from_function_node(member.function));
						found_sig = true;
					}
				}
			}
		}

		// Fallback: try to find the variable as a local/parameter by scanning suites.
		if (!found_sig && db) {
			if (cls) {
				for (int i = 0; i < cls->members.size(); i++) {
					if (found_sig) break;
					const GDScriptParser::ClassNode::Member &member = cls->members[i];
					if (member.type != GDScriptParser::ClassNode::Member::FUNCTION) continue;

					const GDScriptParser::FunctionNode *func = member.function;
					if (line + 1 < func->start_line || line + 1 > func->end_line) continue;

					for (int j = 0; j < func->parameters.size(); j++) {
						if (func->parameters[j]->identifier->name == StringName(call_ctx.base_text)) {
							base_type = func->parameters[j]->get_datatype();
							break;
						}
					}

					if (!base_type.is_set() && func->body) {
						const GDScriptParser::SuiteNode *suite = func->body;
						while (suite) {
							for (int j = 0; j < suite->locals.size(); j++) {
								if (suite->locals[j].name == StringName(call_ctx.base_text)) {
									base_type = suite->locals[j].get_datatype();
									break;
								}
							}
							if (base_type.is_set()) break;
							suite = suite->parent_block;
						}
					}

					if (base_type.is_set() && base_type.kind == GDScriptParser::DataType::NATIVE) {
						MethodInfo mi;
						if (db->get_method_info(base_type.native_type, StringName(call_ctx.func_name), &mi)) {
							help.signatures.push_back(_sig_from_method_info(call_ctx.func_name, mi));
							found_sig = true;
						}
					} else if (base_type.is_set() && base_type.kind == GDScriptParser::DataType::BUILTIN) {
						if (Variant::has_builtin_method(base_type.builtin_type, StringName(call_ctx.func_name))) {
							MethodInfo mi = Variant::get_builtin_method_info(base_type.builtin_type, StringName(call_ctx.func_name));
							help.signatures.push_back(_sig_from_method_info(call_ctx.func_name, mi));
							found_sig = true;
						}
					} else if (base_type.is_set() && base_type.kind == GDScriptParser::DataType::CLASS && base_type.class_type) {
						const GDScriptParser::ClassNode *target_cls = base_type.class_type;
						if (target_cls->has_member(StringName(call_ctx.func_name))) {
							const GDScriptParser::ClassNode::Member &m = target_cls->get_member(StringName(call_ctx.func_name));
							if (m.type == GDScriptParser::ClassNode::Member::FUNCTION) {
								help.signatures.push_back(_sig_from_function_node(m.function));
								found_sig = true;
							}
						}
					}
				}
			}
		}
	} else {
		// Bare function call: func(
		String file_path = Server::uri_to_path(uri);
		GDScriptParser parser;
		GDScriptAnalyzer analyzer(&parser);
		parser.parse(source, file_path, false);
		analyzer.analyze();

		const GDScriptParser::ClassNode *cls = parser.get_tree();

		if (cls) {
			if (cls->has_member(StringName(call_ctx.func_name))) {
				const GDScriptParser::ClassNode::Member &member = cls->get_member(StringName(call_ctx.func_name));
				if (member.type == GDScriptParser::ClassNode::Member::FUNCTION) {
					help.signatures.push_back(_sig_from_function_node(member.function));
					found_sig = true;
				}
			}

			if (!found_sig && db) {
				GDScriptParser::DataType base_type = cls->base_type;
				while (base_type.is_set() && base_type.kind == GDScriptParser::DataType::NATIVE) {
					MethodInfo mi;
					if (db->get_method_info(base_type.native_type, StringName(call_ctx.func_name), &mi)) {
						help.signatures.push_back(_sig_from_method_info(call_ctx.func_name, mi));
						found_sig = true;
						break;
					}
					StringName parent = db->get_parent_class(base_type.native_type);
					if (parent == StringName() || parent == base_type.native_type) break;
					base_type.native_type = parent;
				}
			}
		}

		if (!found_sig && Variant::has_utility_function(StringName(call_ctx.func_name))) {
			MethodInfo mi = Variant::get_utility_function_info(StringName(call_ctx.func_name));
			help.signatures.push_back(_sig_from_method_info(call_ctx.func_name, mi));
			found_sig = true;
		}

		if (!found_sig && GDScriptUtilityFunctions::function_exists(StringName(call_ctx.func_name))) {
			MethodInfo mi = GDScriptUtilityFunctions::get_function_info(StringName(call_ctx.func_name));
			help.signatures.push_back(_sig_from_method_info(call_ctx.func_name, mi));
			found_sig = true;
		}

		if (!found_sig) {
			Variant::Type builtin_type = GDScriptParser::get_builtin_type(StringName(call_ctx.func_name));
			if (builtin_type < Variant::VARIANT_MAX) {
				List<MethodInfo> constructors;
				Variant::get_constructor_list(builtin_type, &constructors);
				for (const MethodInfo &ci : constructors) {
					if (ci.arguments.size() == 0) continue;
					help.signatures.push_back(_sig_from_method_info(call_ctx.func_name, ci));
				}
				if (help.signatures.size() > 0) {
					found_sig = true;
				}
			}
		}
	}

	if (!found_sig) {
		return make_response(p_id, Variant());
	}

	help.active_parameter = call_ctx.active_param;
	return make_response(p_id, help.to_dict());
}

} // namespace lsp

#endif // HOMOT
