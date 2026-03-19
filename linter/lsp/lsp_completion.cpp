/**************************************************************************/
/*  lsp_completion.cpp                                                    */
/**************************************************************************/

#ifdef HOMOT

#include "lsp_completion.h"
#include "lsp_protocol.h"
#include "lsp_server.h"
#include "lsp_utils.h"

#include "../stubs/linterdb.h"
#include "../stubs/script_server_stub.h"

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_parser.h"

#include "core/io/file_access.h"
#include "core/variant/variant.h"

using linter::LinterDB;

namespace lsp {

// ---------------------------------------------------------------------------
// Completion
// ---------------------------------------------------------------------------

String CompletionHandler::insert_cursor_sentinel(const String &p_source, int p_line, int p_character) {
	Vector<String> lines = p_source.split("\n");
	if (p_line < 0 || p_line >= lines.size()) {
		return p_source;
	}

	String &target_line = lines.write[p_line];
	int insert_pos = CLAMP(p_character, 0, target_line.length());
	target_line = target_line.insert(insert_pos, String::chr(0xFFFF));

	String result;
	for (int i = 0; i < lines.size(); i++) {
		if (i > 0) {
			result += "\n";
		}
		result += lines[i];
	}
	return result;
}

// GDScript keywords for identifier completion.
static const char *_gdscript_keywords[] = {
	"var", "const", "func", "class", "extends", "class_name", "signal",
	"enum", "static", "if", "elif", "else", "for", "while", "match",
	"when", "break", "continue", "pass", "return", "await",
	"preload", "load", "as", "is", "in", "not", "and", "or",
	"true", "false", "null", "self", "super",
	"void", "bool", "int", "float",
	nullptr
};

void CompletionHandler::collect_completions_for_context(const GDScriptParser &p_parser, Array &r_items) {
	GDScriptParser::CompletionContext ctx = p_parser.get_completion_context();

	switch (ctx.type) {
		case GDScriptParser::COMPLETION_NONE:
			break;

		case GDScriptParser::COMPLETION_IDENTIFIER:
		case GDScriptParser::COMPLETION_METHOD: {
			bool methods_only = (ctx.type == GDScriptParser::COMPLETION_METHOD);

			// 1. Suite locals (variables, constants, parameters).
			if (!methods_only && ctx.current_suite) {
				const GDScriptParser::SuiteNode *suite = ctx.current_suite;
				while (suite) {
					for (int i = 0; i < suite->locals.size(); i++) {
						const GDScriptParser::SuiteNode::Local &local = suite->locals[i];
						CompletionItem item;
						item.label = local.name;
						switch (local.type) {
							case GDScriptParser::SuiteNode::Local::CONSTANT:
								item.kind = COMPLETION_KIND_CONSTANT;
								break;
							case GDScriptParser::SuiteNode::Local::PARAMETER:
								item.kind = COMPLETION_KIND_VARIABLE;
								item.detail = "parameter";
								break;
							default:
								item.kind = COMPLETION_KIND_VARIABLE;
								break;
						}
						r_items.push_back(item.to_dict());
					}
					suite = suite->parent_block;
				}
			}

			// 2. Current function parameters.
			if (!methods_only && ctx.current_function) {
				for (int i = 0; i < ctx.current_function->parameters.size(); i++) {
					const GDScriptParser::ParameterNode *param = ctx.current_function->parameters[i];
					CompletionItem item;
					item.label = param->identifier->name;
					item.kind = COMPLETION_KIND_VARIABLE;
					item.detail = "parameter";
					r_items.push_back(item.to_dict());
				}
			}

			// 3. Class members.
			if (ctx.current_class) {
				for (int i = 0; i < ctx.current_class->members.size(); i++) {
					const GDScriptParser::ClassNode::Member &member = ctx.current_class->members[i];
					CompletionItem item;
					switch (member.type) {
						case GDScriptParser::ClassNode::Member::VARIABLE:
							if (methods_only) continue;
							item.label = member.variable->identifier->name;
							item.kind = COMPLETION_KIND_FIELD;
							break;
						case GDScriptParser::ClassNode::Member::CONSTANT:
							if (methods_only) continue;
							item.label = member.constant->identifier->name;
							item.kind = COMPLETION_KIND_CONSTANT;
							break;
						case GDScriptParser::ClassNode::Member::FUNCTION: {
							item.label = member.function->identifier->name;
							item.kind = COMPLETION_KIND_FUNCTION;
							String sig = "(";
							for (int j = 0; j < member.function->parameters.size(); j++) {
								if (j > 0) sig += ", ";
								sig += member.function->parameters[j]->identifier->name;
								GDScriptParser::DataType pt = member.function->parameters[j]->get_datatype();
								if (pt.is_set() && !pt.is_variant()) {
									sig += ": " + pt.to_string();
								}
							}
							sig += ")";
							GDScriptParser::DataType rt = member.function->get_datatype();
							if (rt.is_set() && !rt.is_variant()) {
								sig += " -> " + rt.to_string();
							}
							item.detail = sig;
						} break;
						case GDScriptParser::ClassNode::Member::SIGNAL:
							if (methods_only) continue;
							item.label = member.signal->identifier->name;
							item.kind = COMPLETION_KIND_EVENT;
							break;
						case GDScriptParser::ClassNode::Member::ENUM:
							if (methods_only) continue;
							item.label = member.m_enum->identifier->name;
							item.kind = COMPLETION_KIND_ENUM;
							break;
						case GDScriptParser::ClassNode::Member::CLASS:
							if (methods_only) continue;
							item.label = member.m_class->identifier->name;
							item.kind = COMPLETION_KIND_CLASS;
							break;
						case GDScriptParser::ClassNode::Member::ENUM_VALUE:
							if (methods_only) continue;
							item.label = member.enum_value.identifier->name;
							item.kind = COMPLETION_KIND_ENUM_MEMBER;
							break;
						default:
							continue;
					}
					r_items.push_back(item.to_dict());
				}

				// 4. Walk native inheritance chain via LinterDB.
				GDScriptParser::DataType base_type = ctx.current_class->base_type;
				while (base_type.is_set() && base_type.kind == GDScriptParser::DataType::NATIVE) {
					StringName native_class = base_type.native_type;
					LinterDB *db = LinterDB::get_singleton();
					if (!db || !db->class_exists(native_class)) {
						break;
					}

					// Methods.
					{
						List<MethodInfo> methods;
						db->get_method_list(native_class, &methods, true);
						for (const MethodInfo &mi : methods) {
							if (mi.name.begins_with("_")) continue;
							CompletionItem item;
							item.label = mi.name;
							item.kind = COMPLETION_KIND_METHOD;
							item.detail = method_signature(mi);
							r_items.push_back(item.to_dict());
						}
					}

					if (!methods_only) {
						// Properties.
						{
							List<PropertyInfo> props;
							db->get_property_list(native_class, &props, true);
							for (const PropertyInfo &pi : props) {
								if (pi.name.begins_with("_")) continue;
								CompletionItem item;
								item.label = pi.name;
								item.kind = COMPLETION_KIND_PROPERTY;
								r_items.push_back(item.to_dict());
							}
						}

						// Signals.
						{
							List<MethodInfo> signals;
							db->get_signal_list(native_class, &signals, true);
							for (const MethodInfo &si : signals) {
								CompletionItem item;
								item.label = si.name;
								item.kind = COMPLETION_KIND_EVENT;
								r_items.push_back(item.to_dict());
							}
						}

						// Constants.
						{
							List<String> constants;
							db->get_integer_constant_list(native_class, &constants, true);
							for (const String &c : constants) {
								CompletionItem item;
								item.label = c;
								item.kind = COMPLETION_KIND_CONSTANT;
								r_items.push_back(item.to_dict());
							}
						}
					}

					// Walk up.
					StringName parent = db->get_parent_class(native_class);
					if (parent == StringName() || parent == native_class) {
						break;
					}
					base_type.native_type = parent;
				}
			}

			// 5. Global classes.
			if (!methods_only) {
				for (const KeyValue<String, String> &kv : server.class_to_path) {
					CompletionItem item;
					item.label = kv.key;
					item.kind = COMPLETION_KIND_CLASS;
					r_items.push_back(item.to_dict());
				}
			}

			// 6. Native class names.
			if (!methods_only) {
				LinterDB *db = LinterDB::get_singleton();
				if (db) {
					LocalVector<StringName> native_classes;
					db->get_class_list(native_classes);
					for (const StringName &cn : native_classes) {
						CompletionItem item;
						item.label = cn;
						item.kind = COMPLETION_KIND_CLASS;
						r_items.push_back(item.to_dict());
					}
				}
			}

			// 6b. Variant built-in type constructors (Vector2, Color, etc.).
			if (!methods_only) {
				for (int i = 1; i < Variant::VARIANT_MAX; i++) {
					Variant::Type vt = (Variant::Type)i;
					String type_name = Variant::get_type_name(vt);
					if (Variant::get_constructor_count(vt) > 0) {
						CompletionItem item;
						item.label = type_name;
						item.kind = COMPLETION_KIND_CONSTRUCTOR;
						for (int j = 0; j < Variant::get_constructor_count(vt); j++) {
							int argc = Variant::get_constructor_argument_count(vt, j);
							if (argc > 0) {
								String sig = "(";
								for (int k = 0; k < argc; k++) {
									if (k > 0) sig += ", ";
									sig += Variant::get_constructor_argument_name(vt, j, k);
									sig += ": " + Variant::get_type_name(Variant::get_constructor_argument_type(vt, j, k));
								}
								sig += ")";
								item.detail = sig;
								break;
							}
						}
						r_items.push_back(item.to_dict());
					}
				}
			}

			// 7. Utility functions.
			{
				List<StringName> utility_funcs;
				Variant::get_utility_function_list(&utility_funcs);
				for (const StringName &fn : utility_funcs) {
					CompletionItem item;
					item.label = fn;
					item.kind = COMPLETION_KIND_FUNCTION;
					MethodInfo mi = Variant::get_utility_function_info(fn);
					item.detail = method_signature(mi);
					r_items.push_back(item.to_dict());
				}
			}

			// 8. Keywords.
			if (!methods_only) {
				for (int i = 0; _gdscript_keywords[i] != nullptr; i++) {
					CompletionItem item;
					item.label = _gdscript_keywords[i];
					item.kind = COMPLETION_KIND_KEYWORD;
					r_items.push_back(item.to_dict());
				}
			}
		} break;

		case GDScriptParser::COMPLETION_ATTRIBUTE:
		case GDScriptParser::COMPLETION_ATTRIBUTE_METHOD: {
			bool methods_only = (ctx.type == GDScriptParser::COMPLETION_ATTRIBUTE_METHOD);

			if (!ctx.node || ctx.node->type != GDScriptParser::Node::SUBSCRIPT) {
				break;
			}
			const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(ctx.node);
			if (!subscript->base) {
				break;
			}

			GDScriptParser::DataType base_dt = subscript->base->datatype;

			if (base_dt.kind == GDScriptParser::DataType::NATIVE) {
				LinterDB *db = LinterDB::get_singleton();
				if (db) {
					StringName native_class = base_dt.native_type;

					if (base_dt.is_meta_type) {
						{
							CompletionItem item;
							item.label = "new";
							item.kind = COMPLETION_KIND_METHOD;
							r_items.push_back(item.to_dict());
						}

						{
							List<MethodInfo> methods;
							db->get_method_list(native_class, &methods);
							for (const MethodInfo &mi : methods) {
								if (mi.name.begins_with("_")) continue;
								if (!(mi.flags & METHOD_FLAG_STATIC)) continue;
								CompletionItem item;
								item.label = mi.name;
								item.kind = COMPLETION_KIND_METHOD;
								item.detail = method_signature(mi);
								r_items.push_back(item.to_dict());
							}
						}

						if (!methods_only) {
							{
								List<String> constants;
								db->get_integer_constant_list(native_class, &constants);
								for (const String &c : constants) {
									CompletionItem item;
									item.label = c;
									item.kind = COMPLETION_KIND_CONSTANT;
									r_items.push_back(item.to_dict());
								}
							}

							{
								List<StringName> enums;
								db->get_enum_list(native_class, &enums);
								for (const StringName &e : enums) {
									CompletionItem item;
									item.label = e;
									item.kind = COMPLETION_KIND_ENUM;
									r_items.push_back(item.to_dict());
								}
							}
						}
					} else {
						{
							List<MethodInfo> methods;
							db->get_method_list(native_class, &methods);
							for (const MethodInfo &mi : methods) {
								if (mi.name.begins_with("_")) continue;
								CompletionItem item;
								item.label = mi.name;
								item.kind = COMPLETION_KIND_METHOD;
								item.detail = method_signature(mi);
								r_items.push_back(item.to_dict());
							}
						}

						if (!methods_only) {
							{
								List<PropertyInfo> props;
								db->get_property_list(native_class, &props);
								for (const PropertyInfo &pi : props) {
									if (pi.name.begins_with("_")) continue;
									CompletionItem item;
									item.label = pi.name;
									item.kind = COMPLETION_KIND_PROPERTY;
									r_items.push_back(item.to_dict());
								}
							}

							{
								List<MethodInfo> signals;
								db->get_signal_list(native_class, &signals);
								for (const MethodInfo &si : signals) {
									CompletionItem item;
									item.label = si.name;
									item.kind = COMPLETION_KIND_EVENT;
									r_items.push_back(item.to_dict());
								}
							}

							{
								List<String> constants;
								db->get_integer_constant_list(native_class, &constants);
								for (const String &c : constants) {
									CompletionItem item;
									item.label = c;
									item.kind = COMPLETION_KIND_CONSTANT;
									r_items.push_back(item.to_dict());
								}
							}
						}
					}
				}
			} else if (base_dt.kind == GDScriptParser::DataType::BUILTIN) {
				Variant::Type vt = base_dt.builtin_type;

				if (base_dt.is_meta_type) {
					if (!methods_only) {
						List<StringName> constants;
						Variant::get_constants_for_type(vt, &constants);
						for (const StringName &c : constants) {
							CompletionItem item;
							item.label = c;
							item.kind = COMPLETION_KIND_CONSTANT;
							r_items.push_back(item.to_dict());
						}
					}
				} else {
					{
						List<StringName> methods;
						Variant::get_builtin_method_list(vt, &methods);
						for (const StringName &m : methods) {
							CompletionItem item;
							item.label = m;
							item.kind = COMPLETION_KIND_METHOD;
							MethodInfo mi = Variant::get_builtin_method_info(vt, m);
							item.detail = method_signature(mi);
							r_items.push_back(item.to_dict());
						}
					}

					if (!methods_only) {
						{
							List<StringName> members;
							Variant::get_member_list(vt, &members);
							for (const StringName &m : members) {
								CompletionItem item;
								item.label = m;
								item.kind = COMPLETION_KIND_FIELD;
								r_items.push_back(item.to_dict());
							}
						}
					}
				}
			} else if (base_dt.kind == GDScriptParser::DataType::CLASS && base_dt.class_type) {
				if (base_dt.is_meta_type) {
					CompletionItem new_item;
					new_item.label = "new";
					new_item.kind = COMPLETION_KIND_METHOD;
					r_items.push_back(new_item.to_dict());
				}
				HashSet<String> added_labels;
				const GDScriptParser::ClassNode *cls = base_dt.class_type;
				for (int i = 0; i < cls->members.size(); i++) {
					const GDScriptParser::ClassNode::Member &member = cls->members[i];
					CompletionItem item;
					switch (member.type) {
						case GDScriptParser::ClassNode::Member::VARIABLE:
							if (methods_only) continue;
							item.label = member.variable->identifier->name;
							item.kind = COMPLETION_KIND_FIELD;
							break;
						case GDScriptParser::ClassNode::Member::CONSTANT:
							if (methods_only) continue;
							item.label = member.constant->identifier->name;
							item.kind = COMPLETION_KIND_CONSTANT;
							break;
						case GDScriptParser::ClassNode::Member::FUNCTION: {
							item.label = member.function->identifier->name;
							item.kind = COMPLETION_KIND_FUNCTION;
							String sig = "(";
							for (int j = 0; j < member.function->parameters.size(); j++) {
								if (j > 0) sig += ", ";
								sig += member.function->parameters[j]->identifier->name;
								GDScriptParser::DataType pt = member.function->parameters[j]->get_datatype();
								if (pt.is_set() && !pt.is_variant()) {
									sig += ": " + pt.to_string();
								}
							}
							sig += ")";
							GDScriptParser::DataType rt = member.function->get_datatype();
							if (rt.is_set() && !rt.is_variant()) {
								sig += " -> " + rt.to_string();
							}
							item.detail = sig;
						} break;
						case GDScriptParser::ClassNode::Member::SIGNAL:
							if (methods_only) continue;
							item.label = member.signal->identifier->name;
							item.kind = COMPLETION_KIND_EVENT;
							break;
						case GDScriptParser::ClassNode::Member::ENUM:
							if (methods_only) continue;
							item.label = member.m_enum->identifier->name;
							item.kind = COMPLETION_KIND_ENUM;
							break;
						case GDScriptParser::ClassNode::Member::ENUM_VALUE:
							if (methods_only) continue;
							item.label = member.enum_value.identifier->name;
							item.kind = COMPLETION_KIND_ENUM_MEMBER;
							break;
						default:
							continue;
					}
					added_labels.insert(item.label);
					r_items.push_back(item.to_dict());
				}

				// Walk up the inheritance chain through script classes, then native classes.
				GDScriptParser::DataType base_type = cls->base_type;
				while (base_type.is_set() && base_type.kind == GDScriptParser::DataType::CLASS && base_type.class_type) {
					const GDScriptParser::ClassNode *parent_cls = base_type.class_type;
					for (int i = 0; i < parent_cls->members.size(); i++) {
						const GDScriptParser::ClassNode::Member &member = parent_cls->members[i];
						CompletionItem item;
						switch (member.type) {
							case GDScriptParser::ClassNode::Member::VARIABLE:
								if (methods_only) continue;
								item.label = member.variable->identifier->name;
								item.kind = COMPLETION_KIND_FIELD;
								break;
							case GDScriptParser::ClassNode::Member::CONSTANT:
								if (methods_only) continue;
								item.label = member.constant->identifier->name;
								item.kind = COMPLETION_KIND_CONSTANT;
								break;
							case GDScriptParser::ClassNode::Member::FUNCTION: {
								item.label = member.function->identifier->name;
								item.kind = COMPLETION_KIND_FUNCTION;
								String sig = "(";
								for (int j = 0; j < member.function->parameters.size(); j++) {
									if (j > 0) sig += ", ";
									sig += member.function->parameters[j]->identifier->name;
									GDScriptParser::DataType pt = member.function->parameters[j]->get_datatype();
									if (pt.is_set() && !pt.is_variant()) {
										sig += ": " + pt.to_string();
									}
								}
								sig += ")";
								GDScriptParser::DataType rt = member.function->get_datatype();
								if (rt.is_set() && !rt.is_variant()) {
									sig += " -> " + rt.to_string();
								}
								item.detail = sig;
							} break;
							case GDScriptParser::ClassNode::Member::SIGNAL:
								if (methods_only) continue;
								item.label = member.signal->identifier->name;
								item.kind = COMPLETION_KIND_EVENT;
								break;
							case GDScriptParser::ClassNode::Member::ENUM:
								if (methods_only) continue;
								item.label = member.m_enum->identifier->name;
								item.kind = COMPLETION_KIND_ENUM;
								break;
							case GDScriptParser::ClassNode::Member::ENUM_VALUE:
								if (methods_only) continue;
								item.label = member.enum_value.identifier->name;
								item.kind = COMPLETION_KIND_ENUM_MEMBER;
								break;
							default:
								continue;
						}
						if (added_labels.has(item.label)) continue;
						added_labels.insert(item.label);
						r_items.push_back(item.to_dict());
					}
					base_type = parent_cls->base_type;
				}

				LinterDB *db = LinterDB::get_singleton();
				while (base_type.is_set() && base_type.kind == GDScriptParser::DataType::NATIVE && db) {
					StringName native_class = base_type.native_type;
					if (!db->class_exists(native_class)) break;

					{
						List<MethodInfo> methods;
						db->get_method_list(native_class, &methods, true);
						for (const MethodInfo &mi : methods) {
							if (mi.name.begins_with("_")) continue;
							if (added_labels.has(mi.name)) continue;
							added_labels.insert(mi.name);
							CompletionItem item;
							item.label = mi.name;
							item.kind = COMPLETION_KIND_METHOD;
							item.detail = method_signature(mi);
							r_items.push_back(item.to_dict());
						}
					}

					if (!methods_only) {
						{
							List<PropertyInfo> props;
							db->get_property_list(native_class, &props, true);
							for (const PropertyInfo &pi : props) {
								if (pi.name.begins_with("_")) continue;
								if (added_labels.has(pi.name)) continue;
								added_labels.insert(pi.name);
								CompletionItem item;
								item.label = pi.name;
								item.kind = COMPLETION_KIND_PROPERTY;
								r_items.push_back(item.to_dict());
							}
						}

						{
							List<MethodInfo> signals;
							db->get_signal_list(native_class, &signals, true);
							for (const MethodInfo &si : signals) {
								if (added_labels.has(si.name)) continue;
								added_labels.insert(si.name);
								CompletionItem item;
								item.label = si.name;
								item.kind = COMPLETION_KIND_EVENT;
								r_items.push_back(item.to_dict());
							}
						}

						{
							List<String> constants;
							db->get_integer_constant_list(native_class, &constants, true);
							for (const String &c : constants) {
								if (added_labels.has(c)) continue;
								added_labels.insert(c);
								CompletionItem item;
								item.label = c;
								item.kind = COMPLETION_KIND_CONSTANT;
								r_items.push_back(item.to_dict());
							}
						}
					}

					StringName parent = db->get_parent_class(native_class);
					if (parent == StringName() || parent == native_class) break;
					base_type.native_type = parent;
				}
			} else if (base_dt.kind == GDScriptParser::DataType::ENUM) {
				if (!methods_only) {
					for (const KeyValue<StringName, int64_t> &kv : base_dt.enum_values) {
						CompletionItem item;
						item.label = kv.key;
						item.kind = COMPLETION_KIND_ENUM_MEMBER;
						r_items.push_back(item.to_dict());
					}
				}
			}

		} break;

		case GDScriptParser::COMPLETION_ANNOTATION: {
			List<MethodInfo> annotations;
			p_parser.get_annotation_list(&annotations);
			for (const MethodInfo &mi : annotations) {
				CompletionItem item;
				item.label = mi.name.substr(1); // Remove leading @.
				item.kind = COMPLETION_KIND_KEYWORD;
				item.insert_text = mi.name.substr(1);
				r_items.push_back(item.to_dict());
			}
		} break;

		case GDScriptParser::COMPLETION_TYPE_NAME:
		case GDScriptParser::COMPLETION_TYPE_NAME_OR_VOID:
		case GDScriptParser::COMPLETION_INHERIT_TYPE: {
			if (ctx.type == GDScriptParser::COMPLETION_TYPE_NAME_OR_VOID) {
				CompletionItem item;
				item.label = "void";
				item.kind = COMPLETION_KIND_KEYWORD;
				r_items.push_back(item.to_dict());
			}

			for (int i = 1; i < Variant::VARIANT_MAX; i++) {
				CompletionItem item;
				item.label = Variant::get_type_name(Variant::Type(i));
				item.kind = COMPLETION_KIND_CLASS;
				r_items.push_back(item.to_dict());
			}

			LinterDB *db = LinterDB::get_singleton();
			if (db) {
				LocalVector<StringName> native_classes;
				db->get_class_list(native_classes);
				for (const StringName &cn : native_classes) {
					CompletionItem item;
					item.label = cn;
					item.kind = COMPLETION_KIND_CLASS;
					r_items.push_back(item.to_dict());
				}
			}

			for (const KeyValue<String, String> &kv : server.class_to_path) {
				CompletionItem item;
				item.label = kv.key;
				item.kind = COMPLETION_KIND_CLASS;
				r_items.push_back(item.to_dict());
			}

			if (ctx.current_class) {
				for (int i = 0; i < ctx.current_class->members.size(); i++) {
					const GDScriptParser::ClassNode::Member &member = ctx.current_class->members[i];
					if (member.type == GDScriptParser::ClassNode::Member::CLASS) {
						CompletionItem item;
						item.label = member.m_class->identifier->name;
						item.kind = COMPLETION_KIND_CLASS;
						r_items.push_back(item.to_dict());
					} else if (member.type == GDScriptParser::ClassNode::Member::ENUM) {
						CompletionItem item;
						item.label = member.m_enum->identifier->name;
						item.kind = COMPLETION_KIND_ENUM;
						r_items.push_back(item.to_dict());
					}
				}
			}
		} break;

		case GDScriptParser::COMPLETION_OVERRIDE_METHOD: {
			if (!ctx.current_class) break;
			GDScriptParser::DataType base_type = ctx.current_class->base_type;
			LinterDB *db = LinterDB::get_singleton();
			if (!db) break;

			while (base_type.is_set() && base_type.kind == GDScriptParser::DataType::NATIVE) {
				StringName native_class = base_type.native_type;
				if (!db->class_exists(native_class)) break;

				List<MethodInfo> methods;
				db->get_method_list(native_class, &methods, true);
				for (const MethodInfo &mi : methods) {
					if (mi.name.begins_with("_")) {
						CompletionItem item;
						item.label = mi.name;
						item.kind = COMPLETION_KIND_METHOD;
						item.detail = method_signature(mi);
						r_items.push_back(item.to_dict());
					}
				}

				StringName parent = db->get_parent_class(native_class);
				if (parent == StringName() || parent == native_class) break;
				base_type.native_type = parent;
			}
		} break;

		case GDScriptParser::COMPLETION_BUILT_IN_TYPE_CONSTANT_OR_STATIC_METHOD: {
			Variant::Type vt = ctx.builtin_type;

			{
				List<StringName> constants;
				Variant::get_constants_for_type(vt, &constants);
				for (const StringName &c : constants) {
					CompletionItem item;
					item.label = c;
					item.kind = COMPLETION_KIND_CONSTANT;
					r_items.push_back(item.to_dict());
				}
			}

			{
				List<StringName> methods;
				Variant::get_builtin_method_list(vt, &methods);
				for (const StringName &m : methods) {
					if (Variant::is_builtin_method_static(vt, m)) {
						CompletionItem item;
						item.label = m;
						item.kind = COMPLETION_KIND_METHOD;
						MethodInfo mi = Variant::get_builtin_method_info(vt, m);
						item.detail = method_signature(mi);
						r_items.push_back(item.to_dict());
					}
				}
			}
		} break;

		default:
			break;
	}
}

Dictionary CompletionHandler::handle(const Variant &p_id, const Dictionary &p_params) {
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
		return make_response(p_id, Array());
	}

	String modified_source = insert_cursor_sentinel(source, line, character);
	String file_path = Server::uri_to_path(uri);

	GDScriptParser parser;
	GDScriptAnalyzer analyzer(&parser);

	parser.parse(modified_source, file_path, true);
	analyzer.analyze();

	Array items;
	collect_completions_for_context(parser, items);

	return make_response(p_id, items);
}

} // namespace lsp

#endif // HOMOT
