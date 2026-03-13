/**************************************************************************/
/*  lsp_server.cpp                                                        */
/**************************************************************************/

#ifdef HOMOT

#include "lsp_server.h"
#include "lsp_transport.h"

#include "../stubs/classdb_stub.h"
#include "../stubs/linterdb.h"
#include "../stubs/script_server_stub.h"

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_parser.h"
#include "modules/gdscript/gdscript_warning.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/variant/variant.h"

using linter::LinterDB;
using linter::ScriptServerStub;

namespace lsp {

// ---------------------------------------------------------------------------
// URI helpers
// ---------------------------------------------------------------------------

String Server::uri_to_path(const String &p_uri) {
	// file:///C:/foo/bar.gd -> C:/foo/bar.gd
	// file:///home/user/foo.gd -> /home/user/foo.gd
	String path = p_uri;
	if (path.begins_with("file:///")) {
#ifdef _WIN32
		// file:///C:/path -> C:/path
		path = path.substr(8);
#else
		// file:///path -> /path
		path = path.substr(7);
#endif
	}
	// Decode percent-encoded characters.
	path = path.uri_decode();
	// Normalize to forward slashes.
	path = path.replace("\\", "/");
	return path;
}

String Server::path_to_uri(const String &p_path) {
	String path = p_path.replace("\\", "/");
#ifdef _WIN32
	// C:/foo/bar.gd -> file:///C:/foo/bar.gd
	return "file:///" + path;
#else
	// /home/user/foo.gd -> file:///home/user/foo.gd
	return "file://" + path;
#endif
}

// ---------------------------------------------------------------------------
// Workspace scanning
// ---------------------------------------------------------------------------

static void _collect_scripts_recursive(const String &p_dir, Vector<String> &r_scripts) {
	Ref<DirAccess> da = DirAccess::open(p_dir);
	if (da.is_null()) {
		return;
	}
	da->list_dir_begin();
	String file = da->get_next();
	while (!file.is_empty()) {
		if (da->current_is_dir()) {
			if (file != "." && file != "..") {
				_collect_scripts_recursive(p_dir.path_join(file), r_scripts);
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

static String _extract_class_name(const String &p_source) {
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

static String _extract_extends(const String &p_source) {
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

static StringName _resolve_native_base(const String &p_extends, const HashMap<String, String> &p_class_to_extends) {
	String current = p_extends;
	HashSet<String> visited;
	while (!current.is_empty() && !visited.has(current)) {
		visited.insert(current);
		if (ClassDB::class_exists(StringName(current))) {
			return StringName(current);
		}
		LinterDB *db = LinterDB::get_singleton();
		if (db && db->class_exists(StringName(current))) {
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

void Server::scan_workspace_classes() {
	class_to_path.clear();
	class_to_extends.clear();

	if (root_path.is_empty()) {
		return;
	}

	Vector<String> scripts;
	_collect_scripts_recursive(root_path, scripts);

	for (const String &path : scripts) {
		String source = FileAccess::get_file_as_string(path);
		String cname = _extract_class_name(source);
		if (!cname.is_empty()) {
			class_to_path[cname] = path;
			class_to_extends[cname] = _extract_extends(source);
		}
	}
}

void Server::register_global_classes() {
	ScriptServerStub::clear();
	for (const KeyValue<String, String> &kv : class_to_path) {
		StringName native_base = _resolve_native_base(
				class_to_extends.has(kv.key) ? class_to_extends[kv.key] : "RefCounted",
				class_to_extends);
		ScriptServerStub::register_global_class(StringName(kv.key), kv.value, native_base);
	}
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

void Server::publish_diagnostics(const String &p_uri, const String &p_source) {
	if (p_source.is_empty()) {
		clear_diagnostics(p_uri);
		return;
	}

	String file_path = uri_to_path(p_uri);

	GDScriptParser parser;
	GDScriptAnalyzer analyzer(&parser);

	Error parse_err = parser.parse(p_source, file_path, false);
	if (parse_err == OK) {
		parse_err = analyzer.analyze();
	}

	Array diag_array;

	// Collect errors.
	const List<GDScriptParser::ParserError> &errors = parser.get_errors();
	for (const GDScriptParser::ParserError &e : errors) {
		Diagnostic d;
		d.severity = SEVERITY_ERROR;
		d.message = e.message;
		// LSP lines are 0-based; parser lines are 1-based.
		d.range.start.line = MAX(0, e.line - 1);
		d.range.start.character = MAX(0, e.column - 1);
		d.range.end.line = d.range.start.line;
		d.range.end.character = d.range.start.character;
		diag_array.push_back(d.to_dict());
	}

#ifdef DEBUG_ENABLED
	// Collect warnings.
	const List<GDScriptWarning> &warnings = parser.get_warnings();
	for (const GDScriptWarning &w : warnings) {
		Diagnostic d;
		d.severity = SEVERITY_WARNING;
		d.message = w.get_message();
		d.code = GDScriptWarning::get_name_from_code(w.code);
		d.range.start.line = MAX(0, w.start_line - 1);
		d.range.start.character = 0;
		d.range.end.line = MAX(0, (w.end_line > 0 ? w.end_line : w.start_line) - 1);
		d.range.end.character = 0;
		diag_array.push_back(d.to_dict());
	}
#endif

	Dictionary params;
	params["uri"] = p_uri;
	params["diagnostics"] = diag_array;

	Transport::write_message(make_notification("textDocument/publishDiagnostics", params));
}

void Server::clear_diagnostics(const String &p_uri) {
	Dictionary params;
	params["uri"] = p_uri;
	params["diagnostics"] = Array();
	Transport::write_message(make_notification("textDocument/publishDiagnostics", params));
}

// ---------------------------------------------------------------------------
// Request handlers
// ---------------------------------------------------------------------------

Dictionary Server::handle_initialize(const Variant &p_id, const Dictionary &p_params) {
	if (p_params.has("rootUri")) {
		root_uri = p_params["rootUri"];
		root_path = uri_to_path(root_uri);
	} else if (p_params.has("rootPath")) {
		root_path = p_params["rootPath"];
		root_uri = path_to_uri(root_path);
	}

	// Load linter database if specified.
	if (!db_path.is_empty() && !LinterDB::get_singleton()) {
		LinterDB *ldb = memnew(LinterDB);
		Error err = ldb->load_from_json(db_path);
		if (err != OK) {
			memdelete(ldb);
		}
	}

	// Scan workspace for global classes.
	scan_workspace_classes();
	register_global_classes();

	initialized = true;
	return make_response(p_id, make_initialize_result());
}

void Server::handle_initialized() {
	// Client is ready. Lint all open documents (none yet, but future-proof).
	// We could also do initial workspace diagnostics here.
}

Dictionary Server::handle_shutdown(const Variant &p_id) {
	shutdown_requested = true;
	return make_response(p_id, Variant());
}

// ---------------------------------------------------------------------------
// Notification handlers
// ---------------------------------------------------------------------------

void Server::handle_did_open(const Dictionary &p_params) {
	Dictionary td = p_params["textDocument"];
	String uri = td["uri"];
	String text = td["text"];

	DocumentState doc;
	doc.uri = uri;
	doc.content = text;
	doc.version = td.has("version") ? (int)td["version"] : 0;
	documents[uri] = doc;

	// Update class registry if this file declares a class_name.
	String cname = _extract_class_name(text);
	if (!cname.is_empty()) {
		String path = uri_to_path(uri);
		class_to_path[cname] = path;
		class_to_extends[cname] = _extract_extends(text);
		register_global_classes();
	}

	publish_diagnostics(uri, text);
}

void Server::handle_did_change(const Dictionary &p_params) {
	Dictionary td = p_params["textDocument"];
	String uri = td["uri"];

	// Full sync — take the last content change.
	Array changes = p_params["contentChanges"];
	if (changes.size() == 0) {
		return;
	}
	Dictionary last_change = changes[changes.size() - 1];
	String text = last_change["text"];

	DocumentState &doc = documents[uri];
	doc.content = text;
	doc.version = td.has("version") ? (int)td["version"] : doc.version + 1;

	// Update class registry.
	String cname = _extract_class_name(text);
	if (!cname.is_empty()) {
		String path = uri_to_path(uri);
		class_to_path[cname] = path;
		class_to_extends[cname] = _extract_extends(text);
		register_global_classes();
	}

	publish_diagnostics(uri, text);
}

void Server::handle_did_close(const Dictionary &p_params) {
	Dictionary td = p_params["textDocument"];
	String uri = td["uri"];
	documents.erase(uri);
	clear_diagnostics(uri);
}

void Server::handle_did_save(const Dictionary &p_params) {
	Dictionary td = p_params["textDocument"];
	String uri = td["uri"];

	// Re-lint from the in-memory content if we have it, otherwise read from disk.
	if (documents.has(uri)) {
		publish_diagnostics(uri, documents[uri].content);
	} else {
		String path = uri_to_path(uri);
		String source = FileAccess::get_file_as_string(path);
		publish_diagnostics(uri, source);
	}
}

void Server::handle_did_change_watched_files(const Dictionary &p_params) {
	// Re-scan the workspace for global classes whenever files are
	// created, deleted, or renamed on disk.
	Array changes = p_params["changes"];
	bool needs_rescan = false;
	for (int i = 0; i < changes.size(); i++) {
		Dictionary change = changes[i];
		String uri = change["uri"];
		String path = uri_to_path(uri);
		String ext = path.get_extension().to_lower();
		if (ext == "gd" || ext == "hm" || ext == "hmc") {
			needs_rescan = true;
			break;
		}
	}

	if (needs_rescan) {
		scan_workspace_classes();

		// Also pick up class_name from open documents that may not be saved yet.
		for (const KeyValue<String, DocumentState> &kv : documents) {
			String cname = _extract_class_name(kv.value.content);
			if (!cname.is_empty()) {
				String path = uri_to_path(kv.value.uri);
				class_to_path[cname] = path;
				class_to_extends[cname] = _extract_extends(kv.value.content);
			}
		}

		register_global_classes();

		// Re-lint all open documents so diagnostics update.
		for (const KeyValue<String, DocumentState> &kv : documents) {
			publish_diagnostics(kv.value.uri, kv.value.content);
		}
	}
}

// ---------------------------------------------------------------------------
// Completion
// ---------------------------------------------------------------------------

String Server::insert_cursor_sentinel(const String &p_source, int p_line, int p_character) {
	// Insert U+FFFF at the cursor position. The parser recognizes this sentinel
	// when for_completion=true and uses it to track cursor location.
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

void Server::collect_completions_for_context(const GDScriptParser &p_parser, Array &r_items) {
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
						case GDScriptParser::ClassNode::Member::FUNCTION:
							item.label = member.function->identifier->name;
							item.kind = COMPLETION_KIND_FUNCTION;
							break;
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
				for (const KeyValue<String, String> &kv : class_to_path) {
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

			// 7. Utility functions.
			{
				List<StringName> utility_funcs;
				Variant::get_utility_function_list(&utility_funcs);
				for (const StringName &fn : utility_funcs) {
					CompletionItem item;
					item.label = fn;
					item.kind = COMPLETION_KIND_FUNCTION;
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
				// Native class — query LinterDB.
				LinterDB *db = LinterDB::get_singleton();
				if (db) {
					StringName native_class = base_dt.native_type;

					// Methods (walks inheritance).
					{
						List<MethodInfo> methods;
						db->get_method_list(native_class, &methods);
						for (const MethodInfo &mi : methods) {
							if (mi.name.begins_with("_")) continue;
							CompletionItem item;
							item.label = mi.name;
							item.kind = COMPLETION_KIND_METHOD;
							r_items.push_back(item.to_dict());
						}
					}

					if (!methods_only) {
						// Properties.
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

						// Signals.
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

						// Enums and constants.
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
			} else if (base_dt.kind == GDScriptParser::DataType::BUILTIN) {
				// Builtin type (Vector2, Color, etc.) — query Variant API.
				Variant::Type vt = base_dt.builtin_type;

				// Methods.
				{
					List<StringName> methods;
					Variant::get_builtin_method_list(vt, &methods);
					for (const StringName &m : methods) {
						CompletionItem item;
						item.label = m;
						item.kind = COMPLETION_KIND_METHOD;
						r_items.push_back(item.to_dict());
					}
				}

				if (!methods_only) {
					// Members (x, y, z, r, g, b, etc.).
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

					// Constants.
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
				}
			} else if (base_dt.kind == GDScriptParser::DataType::CLASS && base_dt.class_type) {
				// Script class — walk AST members.
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
						case GDScriptParser::ClassNode::Member::FUNCTION:
							item.label = member.function->identifier->name;
							item.kind = COMPLETION_KIND_FUNCTION;
							break;
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
					r_items.push_back(item.to_dict());
				}
			} else if (base_dt.kind == GDScriptParser::DataType::ENUM) {
				// Enum type — list enum values.
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

			// Builtin variant types.
			for (int i = 1; i < Variant::VARIANT_MAX; i++) {
				CompletionItem item;
				item.label = Variant::get_type_name(Variant::Type(i));
				item.kind = COMPLETION_KIND_CLASS;
				r_items.push_back(item.to_dict());
			}

			// Native classes.
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

			// Global classes.
			for (const KeyValue<String, String> &kv : class_to_path) {
				CompletionItem item;
				item.label = kv.key;
				item.kind = COMPLETION_KIND_CLASS;
				r_items.push_back(item.to_dict());
			}

			// Script-local classes/enums.
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
						r_items.push_back(item.to_dict());
					}
				}

				StringName parent = db->get_parent_class(native_class);
				if (parent == StringName() || parent == native_class) break;
				base_type.native_type = parent;
			}
		} break;

		default:
			break;
	}
}

Dictionary Server::handle_completion(const Variant &p_id, const Dictionary &p_params) {
	Dictionary td = p_params["textDocument"];
	String uri = td["uri"];
	Dictionary pos_dict = p_params["position"];
	int line = pos_dict["line"];       // 0-based
	int character = pos_dict["character"]; // 0-based

	// Get the document source.
	String source;
	if (documents.has(uri)) {
		source = documents[uri].content;
	} else {
		source = FileAccess::get_file_as_string(uri_to_path(uri));
	}

	if (source.is_empty()) {
		return make_response(p_id, Array());
	}

	// Insert cursor sentinel and parse with for_completion=true.
	String modified_source = insert_cursor_sentinel(source, line, character);
	String file_path = uri_to_path(uri);

	GDScriptParser parser;
	GDScriptAnalyzer analyzer(&parser);

	parser.parse(modified_source, file_path, true);
	analyzer.analyze();

	Array items;
	collect_completions_for_context(parser, items);

	return make_response(p_id, items);
}

// ---------------------------------------------------------------------------
// Go-to-definition — AST node finder
// ---------------------------------------------------------------------------

// Convert a 0-based LSP character offset to the 1-based tab-expanded column
// that the GDScript tokenizer uses. The tokenizer expands each tab to tab_size
// columns (default 4), so a single tab character shifts all subsequent columns.
static int _lsp_to_parser_column(const String &p_source, int p_lsp_line, int p_lsp_character, int p_tab_size = 4) {
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

// Convert a 1-based parser column back to 0-based LSP character offset.
static int _parser_column_to_lsp(const String &p_source, int p_parser_line, int p_parser_col, int p_tab_size = 4) {
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

// Check whether a parser node (1-based coordinates) contains the given
// 1-based parser line and column.
static bool _node_contains_position(const GDScriptParser::Node *p_node, int p_line, int p_col) {
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

// Walk statements in a suite to find the deepest IdentifierNode at position.
static const GDScriptParser::IdentifierNode *_find_identifier_in_expression(
		const GDScriptParser::ExpressionNode *p_expr, int p_line, int p_col);

static const GDScriptParser::IdentifierNode *_find_identifier_in_suite(
		const GDScriptParser::SuiteNode *p_suite, int p_line, int p_col) {
	if (!p_suite) return nullptr;

	for (int i = 0; i < p_suite->statements.size(); i++) {
		const GDScriptParser::Node *stmt = p_suite->statements[i];
		if (!_node_contains_position(stmt, p_line, p_col)) {
			continue;
		}

		switch (stmt->type) {
			case GDScriptParser::Node::VARIABLE: {
				auto *var = static_cast<const GDScriptParser::VariableNode *>(stmt);
				if (var->initializer) {
					auto *found = _find_identifier_in_expression(var->initializer, p_line, p_col);
					if (found) return found;
				}
			} break;
			case GDScriptParser::Node::ASSIGNMENT: {
				auto *assign = static_cast<const GDScriptParser::AssignmentNode *>(stmt);
				auto *found = _find_identifier_in_expression(assign->assignee, p_line, p_col);
				if (found) return found;
				found = _find_identifier_in_expression(assign->assigned_value, p_line, p_col);
				if (found) return found;
			} break;
			case GDScriptParser::Node::IF: {
				auto *if_node = static_cast<const GDScriptParser::IfNode *>(stmt);
				auto *found = _find_identifier_in_expression(if_node->condition, p_line, p_col);
				if (found) return found;
				found = _find_identifier_in_suite(if_node->true_block, p_line, p_col);
				if (found) return found;
				found = _find_identifier_in_suite(if_node->false_block, p_line, p_col);
				if (found) return found;
			} break;
			case GDScriptParser::Node::FOR: {
				auto *for_node = static_cast<const GDScriptParser::ForNode *>(stmt);
				auto *found = _find_identifier_in_expression(for_node->list, p_line, p_col);
				if (found) return found;
				found = _find_identifier_in_suite(for_node->loop, p_line, p_col);
				if (found) return found;
			} break;
			case GDScriptParser::Node::WHILE: {
				auto *while_node = static_cast<const GDScriptParser::WhileNode *>(stmt);
				auto *found = _find_identifier_in_expression(while_node->condition, p_line, p_col);
				if (found) return found;
				found = _find_identifier_in_suite(while_node->loop, p_line, p_col);
				if (found) return found;
			} break;
			case GDScriptParser::Node::RETURN: {
				auto *ret = static_cast<const GDScriptParser::ReturnNode *>(stmt);
				if (ret->return_value) {
					auto *found = _find_identifier_in_expression(ret->return_value, p_line, p_col);
					if (found) return found;
				}
			} break;
			case GDScriptParser::Node::MATCH: {
				auto *match_node = static_cast<const GDScriptParser::MatchNode *>(stmt);
				auto *found = _find_identifier_in_expression(match_node->test, p_line, p_col);
				if (found) return found;
				for (int j = 0; j < match_node->branches.size(); j++) {
					found = _find_identifier_in_suite(match_node->branches[j]->block, p_line, p_col);
					if (found) return found;
				}
			} break;
			default: {
				// For expression statements (bare function calls, etc.)
				if (stmt->is_expression()) {
					auto *found = _find_identifier_in_expression(
							static_cast<const GDScriptParser::ExpressionNode *>(stmt), p_line, p_col);
					if (found) return found;
				}
			} break;
		}
	}
	return nullptr;
}

static const GDScriptParser::IdentifierNode *_find_identifier_in_expression(
		const GDScriptParser::ExpressionNode *p_expr, int p_line, int p_col) {
	if (!p_expr) return nullptr;

	switch (p_expr->type) {
		case GDScriptParser::Node::IDENTIFIER: {
			if (_node_contains_position(p_expr, p_line, p_col)) {
				return static_cast<const GDScriptParser::IdentifierNode *>(p_expr);
			}
		} break;
		case GDScriptParser::Node::SUBSCRIPT: {
			auto *sub = static_cast<const GDScriptParser::SubscriptNode *>(p_expr);
			if (sub->is_attribute && sub->attribute) {
				// Check the attribute identifier first (more specific).
				if (_node_contains_position(sub->attribute, p_line, p_col)) {
					return sub->attribute;
				}
			}
			// Check base expression.
			auto *found = _find_identifier_in_expression(sub->base, p_line, p_col);
			if (found) return found;
			if (!sub->is_attribute && sub->index) {
				found = _find_identifier_in_expression(sub->index, p_line, p_col);
				if (found) return found;
			}
		} break;
		case GDScriptParser::Node::CALL: {
			auto *call = static_cast<const GDScriptParser::CallNode *>(p_expr);
			// Check callee.
			auto *found = _find_identifier_in_expression(call->callee, p_line, p_col);
			if (found) return found;
			// Check arguments.
			for (int i = 0; i < call->arguments.size(); i++) {
				found = _find_identifier_in_expression(call->arguments[i], p_line, p_col);
				if (found) return found;
			}
		} break;
		case GDScriptParser::Node::BINARY_OPERATOR: {
			auto *binop = static_cast<const GDScriptParser::BinaryOpNode *>(p_expr);
			auto *found = _find_identifier_in_expression(binop->left_operand, p_line, p_col);
			if (found) return found;
			found = _find_identifier_in_expression(binop->right_operand, p_line, p_col);
			if (found) return found;
		} break;
		case GDScriptParser::Node::UNARY_OPERATOR: {
			auto *unop = static_cast<const GDScriptParser::UnaryOpNode *>(p_expr);
			auto *found = _find_identifier_in_expression(unop->operand, p_line, p_col);
			if (found) return found;
		} break;
		case GDScriptParser::Node::TERNARY_OPERATOR: {
			auto *ternop = static_cast<const GDScriptParser::TernaryOpNode *>(p_expr);
			auto *found = _find_identifier_in_expression(ternop->true_expr, p_line, p_col);
			if (found) return found;
			found = _find_identifier_in_expression(ternop->false_expr, p_line, p_col);
			if (found) return found;
			found = _find_identifier_in_expression(ternop->condition, p_line, p_col);
			if (found) return found;
		} break;
		case GDScriptParser::Node::ASSIGNMENT: {
			auto *assign = static_cast<const GDScriptParser::AssignmentNode *>(p_expr);
			auto *found = _find_identifier_in_expression(assign->assignee, p_line, p_col);
			if (found) return found;
			found = _find_identifier_in_expression(assign->assigned_value, p_line, p_col);
			if (found) return found;
		} break;
		case GDScriptParser::Node::CAST: {
			auto *cast = static_cast<const GDScriptParser::CastNode *>(p_expr);
			auto *found = _find_identifier_in_expression(cast->operand, p_line, p_col);
			if (found) return found;
		} break;
		case GDScriptParser::Node::AWAIT: {
			auto *aw = static_cast<const GDScriptParser::AwaitNode *>(p_expr);
			auto *found = _find_identifier_in_expression(aw->to_await, p_line, p_col);
			if (found) return found;
		} break;
		default:
			break;
	}
	return nullptr;
}

// Find an IdentifierNode at position by walking the full class AST.
static const GDScriptParser::IdentifierNode *_find_identifier_at_position(
		const GDScriptParser::ClassNode *p_class, int p_line, int p_col) {
	if (!p_class) return nullptr;

	for (int i = 0; i < p_class->members.size(); i++) {
		const GDScriptParser::ClassNode::Member &member = p_class->members[i];

		switch (member.type) {
			case GDScriptParser::ClassNode::Member::FUNCTION: {
				const GDScriptParser::FunctionNode *func = member.function;
				if (!func->body) continue;
				if (!_node_contains_position(func, p_line, p_col)) continue;
				auto *found = _find_identifier_in_suite(func->body, p_line, p_col);
				if (found) return found;
			} break;
			case GDScriptParser::ClassNode::Member::VARIABLE: {
				const GDScriptParser::VariableNode *var = member.variable;
				if (var->initializer && _node_contains_position(var, p_line, p_col)) {
					auto *found = _find_identifier_in_expression(var->initializer, p_line, p_col);
					if (found) return found;
				}
			} break;
			case GDScriptParser::ClassNode::Member::CLASS: {
				// Recurse into inner classes.
				auto *found = _find_identifier_at_position(member.m_class, p_line, p_col);
				if (found) return found;
			} break;
			default:
				break;
		}
	}
	return nullptr;
}

// Resolve an IdentifierNode's definition location. Returns false if no location.
static bool _resolve_identifier_location(
		const GDScriptParser::IdentifierNode *p_id,
		const String &p_current_path,
		const HashMap<String, String> &p_class_to_path,
		String &r_path, int &r_line, int &r_col) {

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
		case GDScriptParser::IdentifierNode::MEMBER_VARIABLE:
		case GDScriptParser::IdentifierNode::INHERITED_VARIABLE:
		case GDScriptParser::IdentifierNode::STATIC_VARIABLE: {
			if (p_id->variable_source) {
				r_path = p_current_path;
				r_line = p_id->variable_source->start_line;
				r_col = p_id->variable_source->start_column;
				return true;
			}
		} break;
		case GDScriptParser::IdentifierNode::LOCAL_CONSTANT:
		case GDScriptParser::IdentifierNode::MEMBER_CONSTANT: {
			if (p_id->constant_source) {
				r_path = p_current_path;
				r_line = p_id->constant_source->start_line;
				r_col = p_id->constant_source->start_column;
				return true;
			}
		} break;
		case GDScriptParser::IdentifierNode::MEMBER_FUNCTION: {
			if (p_id->function_source) {
				r_path = p_current_path;
				r_line = p_id->function_source->start_line;
				r_col = p_id->function_source->start_column;
				return true;
			}
		} break;
		case GDScriptParser::IdentifierNode::MEMBER_SIGNAL: {
			if (p_id->signal_source) {
				r_path = p_current_path;
				r_line = p_id->signal_source->start_line;
				r_col = p_id->signal_source->start_column;
				return true;
			}
		} break;
		case GDScriptParser::IdentifierNode::MEMBER_CLASS: {
			// Inner class — the identifier's datatype should have the class_type.
			if (p_id->datatype.kind == GDScriptParser::DataType::CLASS && p_id->datatype.class_type) {
				r_path = p_current_path;
				r_line = p_id->datatype.class_type->start_line;
				r_col = p_id->datatype.class_type->start_column;
				return true;
			}
		} break;
		case GDScriptParser::IdentifierNode::LOCAL_ITERATOR:
		case GDScriptParser::IdentifierNode::LOCAL_BIND: {
			if (p_id->bind_source) {
				r_path = p_current_path;
				r_line = p_id->bind_source->start_line;
				r_col = p_id->bind_source->start_column;
				return true;
			}
		} break;
		case GDScriptParser::IdentifierNode::NATIVE_CLASS: {
			// No source location for native classes.
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
		r_col = 0;
		return true;
	}

	return false;
}

Dictionary Server::handle_definition(const Variant &p_id, const Dictionary &p_params) {
	Dictionary td = p_params["textDocument"];
	String uri = td["uri"];
	Dictionary pos_dict = p_params["position"];
	int line = pos_dict["line"];       // 0-based
	int character = pos_dict["character"]; // 0-based

	// Get document source.
	String source;
	if (documents.has(uri)) {
		source = documents[uri].content;
	} else {
		source = FileAccess::get_file_as_string(uri_to_path(uri));
	}

	if (source.is_empty()) {
		return make_response(p_id, Variant());
	}

	String file_path = uri_to_path(uri);

	GDScriptParser parser;
	GDScriptAnalyzer analyzer(&parser);

	parser.parse(source, file_path, false);
	analyzer.analyze();

	// Convert LSP 0-based position to parser 1-based coordinates.
	int parser_line = line + 1;
	int parser_col = _lsp_to_parser_column(source, line, character);

	const GDScriptParser::IdentifierNode *ident = _find_identifier_at_position(parser.get_tree(), parser_line, parser_col);
	if (!ident) {
		return make_response(p_id, Variant());
	}

	// Resolve location.
	String def_path;
	int def_line = 0;
	int def_col = 0;
	if (!_resolve_identifier_location(ident, file_path, class_to_path, def_path, def_line, def_col)) {
		return make_response(p_id, Variant());
	}

	// Convert to LSP Location (0-based lines, tab-aware columns).
	String def_source;
	if (def_path == file_path) {
		def_source = source;
	} else {
		String def_uri = path_to_uri(def_path);
		if (documents.has(def_uri)) {
			def_source = documents[def_uri].content;
		} else {
			def_source = FileAccess::get_file_as_string(def_path);
		}
	}

	Location loc;
	loc.uri = path_to_uri(def_path);
	loc.range.start.line = MAX(0, def_line - 1);
	loc.range.start.character = def_source.is_empty() ? MAX(0, def_col - 1) : _parser_column_to_lsp(def_source, def_line, def_col);
	loc.range.end.line = loc.range.start.line;
	loc.range.end.character = loc.range.start.character;

	return make_response(p_id, loc.to_dict());
}

// ---------------------------------------------------------------------------
// Hover
// ---------------------------------------------------------------------------

// Format a DataType into a readable hover string.
static String _format_datatype(const GDScriptParser::DataType &p_type) {
	if (!p_type.is_set()) {
		return "Variant";
	}
	return p_type.to_string();
}

// Build a hover string for an identifier based on its source and type.
static String _build_hover_text(const GDScriptParser::IdentifierNode *p_id) {
	String type_str = _format_datatype(p_id->datatype);
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
						sig += ": " + _format_datatype(param->get_datatype());
					}
				}
				sig += ")";
				GDScriptParser::DataType ret = p_id->function_source->get_datatype();
				if (ret.is_set() && ret.builtin_type != Variant::NIL) {
					sig += " -> " + _format_datatype(ret);
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

Dictionary Server::handle_hover(const Variant &p_id, const Dictionary &p_params) {
	Dictionary td = p_params["textDocument"];
	String uri = td["uri"];
	Dictionary pos_dict = p_params["position"];
	int line = pos_dict["line"];
	int character = pos_dict["character"];

	String source;
	if (documents.has(uri)) {
		source = documents[uri].content;
	} else {
		source = FileAccess::get_file_as_string(uri_to_path(uri));
	}

	if (source.is_empty()) {
		return make_response(p_id, Variant());
	}

	String file_path = uri_to_path(uri);

	GDScriptParser parser;
	GDScriptAnalyzer analyzer(&parser);

	parser.parse(source, file_path, false);
	analyzer.analyze();

	// Convert LSP 0-based position to parser 1-based coordinates.
	int parser_line = line + 1;
	int parser_col = _lsp_to_parser_column(source, line, character);

	const GDScriptParser::IdentifierNode *ident = _find_identifier_at_position(parser.get_tree(), parser_line, parser_col);
	if (!ident) {
		return make_response(p_id, Variant());
	}

	String hover_text = _build_hover_text(ident);
	if (hover_text.is_empty()) {
		return make_response(p_id, Variant());
	}

	// Build Hover response with markdown content.
	Dictionary contents;
	contents["kind"] = "markdown";
	contents["value"] = "```gdscript\n" + hover_text + "\n```";

	Dictionary hover;
	hover["contents"] = contents;

	// Include the identifier's range (convert parser coords to LSP).
	Range range;
	range.start.line = MAX(0, ident->start_line - 1);
	range.start.character = _parser_column_to_lsp(source, ident->start_line, ident->start_column);
	range.end.line = MAX(0, ident->end_line - 1);
	range.end.character = _parser_column_to_lsp(source, ident->end_line, ident->end_column);
	hover["range"] = range.to_dict();

	return make_response(p_id, hover);
}

// ---------------------------------------------------------------------------
// Message dispatch
// ---------------------------------------------------------------------------

bool Server::process_message(const Dictionary &p_msg) {
	String method = p_msg.get("method", "");
	Variant id = p_msg.get("id", Variant());
	Dictionary params = p_msg.get("params", Dictionary());
	bool has_id = p_msg.has("id");

	// --- Requests (have an id) ---
	if (method == "initialize") {
		Transport::write_message(handle_initialize(id, params));
		return true;
	}

	if (method == "shutdown") {
		Transport::write_message(handle_shutdown(id));
		return true;
	}

	if (method == "exit") {
		return false; // Stop the message loop.
	}

	// Before initialization, reject everything except initialize.
	if (!initialized && has_id) {
		Transport::write_message(make_error_response(id, SERVER_NOT_INITIALIZED, "Server not initialized"));
		return true;
	}

	// --- Notifications (no id) ---
	if (method == "initialized") {
		handle_initialized();
		return true;
	}

	if (method == "textDocument/completion") {
		Transport::write_message(handle_completion(id, params));
		return true;
	}

	if (method == "textDocument/definition") {
		Transport::write_message(handle_definition(id, params));
		return true;
	}

	if (method == "textDocument/hover") {
		Transport::write_message(handle_hover(id, params));
		return true;
	}

	if (method == "textDocument/didOpen") {
		handle_did_open(params);
		return true;
	}

	if (method == "textDocument/didChange") {
		handle_did_change(params);
		return true;
	}

	if (method == "textDocument/didClose") {
		handle_did_close(params);
		return true;
	}

	if (method == "textDocument/didSave") {
		handle_did_save(params);
		return true;
	}

	if (method == "workspace/didChangeWatchedFiles") {
		handle_did_change_watched_files(params);
		return true;
	}

	// Unknown request — send MethodNotFound if it has an id.
	if (has_id) {
		Transport::write_message(make_error_response(id, METHOD_NOT_FOUND,
				vformat("Method not found: %s", method)));
	}
	// Unknown notifications are silently ignored per LSP spec.

	return true;
}

} // namespace lsp

#endif // HOMOT
