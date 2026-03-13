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
