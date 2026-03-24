/**************************************************************************/
/*  lsp_server.cpp                                                        */
/**************************************************************************/
/*  Unified server — handles both LSP (textDocument/*) and LSPA (api/*,  */
/*  verify/*) methods in a single dispatch loop.                          */
/**************************************************************************/

#ifdef HOMOT

#include "lsp_server.h"
#include "lsp_transport.h"

#include "../stubs/linterdb.h"
#include "../workspace.h"

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_parser.h"
#include "modules/gdscript/gdscript_warning.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/variant/variant.h"

using linter::LinterDB;

namespace lsp {

// ---------------------------------------------------------------------------
// URI helpers
// ---------------------------------------------------------------------------

String Server::uri_to_path(const String &p_uri) {
	String path = p_uri;
	if (path.begins_with("file:///")) {
#ifdef _WIN32
		path = path.substr(8);
#else
		path = path.substr(7);
#endif
	}
	path = path.uri_decode();
	path = path.replace("\\", "/");
	return path;
}

String Server::path_to_uri(const String &p_path) {
	String path = p_path.replace("\\", "/");
#ifdef _WIN32
	return "file:///" + path;
#else
	return "file://" + path;
#endif
}

// ---------------------------------------------------------------------------
// Workspace scanning (uses workspace:: utilities)
// ---------------------------------------------------------------------------

void Server::scan_workspace_classes() {
	class_to_path.clear();
	class_to_extends.clear();

	if (root_path.is_empty()) {
		return;
	}

	Vector<String> scripts;
	workspace::collect_scripts(root_path, scripts);

	for (const String &path : scripts) {
		String source = FileAccess::get_file_as_string(path);
		String cname = workspace::extract_class_name(source);
		if (!cname.is_empty()) {
			class_to_path[cname] = path;
			class_to_extends[cname] = workspace::extract_extends(source);
		}
	}

	workspace::register_classes(class_to_path, class_to_extends);
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

	const List<GDScriptParser::ParserError> &errors = parser.get_errors();
	for (const GDScriptParser::ParserError &e : errors) {
		Diagnostic d;
		d.severity = SEVERITY_ERROR;
		d.message = e.message;
		d.range.start.line = MAX(0, e.line - 1);
		d.range.start.character = MAX(0, e.column - 1);
		d.range.end.line = d.range.start.line;
		d.range.end.character = d.range.start.character;
		diag_array.push_back(d.to_dict());
	}

#ifdef DEBUG_ENABLED
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

// Route diagnostics based on file type.
void Server::publish_diagnostics_for_file(const String &p_uri, const String &p_source) {
	String path = uri_to_path(p_uri);

	if (workspace::is_script_file(path)) {
		publish_diagnostics(p_uri, p_source);
		return;
	}

	if (p_source.is_empty()) {
		clear_diagnostics(p_uri);
		return;
	}

	Array diag_array;

	if (workspace::is_resource_file(path)) {
		resource_lint::LintResult res = resource_lint::lint_resource_string(p_source, path);
		for (const resource_lint::Diagnostic &d : res.diagnostics) {
			lsp::Diagnostic ld;
			ld.severity = (d.severity == "error") ? SEVERITY_ERROR : SEVERITY_WARNING;
			ld.message = d.message;
			ld.source = "resource";
			ld.range.start.line = MAX(0, d.line - 1);
			ld.range.end.line = ld.range.start.line;
			diag_array.push_back(ld.to_dict());
		}
	} else if (workspace::is_shader_file(path)) {
		shader_lint::LintResult res = shader_lint::lint_shader_string(p_source, path);
		for (const shader_lint::Diagnostic &d : res.diagnostics) {
			lsp::Diagnostic ld;
			ld.severity = (d.severity == "error") ? SEVERITY_ERROR : SEVERITY_WARNING;
			ld.message = d.message;
			ld.source = "gdshader";
			ld.range.start.line = MAX(0, d.line - 1);
			ld.range.end.line = ld.range.start.line;
			diag_array.push_back(ld.to_dict());
		}
	}

	Dictionary params;
	params["uri"] = p_uri;
	params["diagnostics"] = diag_array;
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

	initialized = true;

	// Build unified initialize result with both LSP and LSPA capabilities.
	Dictionary result = make_initialize_result();

	// Add LSPA capabilities.
	Dictionary lspa_caps;
	{
		Array discover;
		discover.push_back("class");
		discover.push_back("classes");
		discover.push_back("search");
		discover.push_back("hierarchy");
		discover.push_back("catalog");
		discover.push_back("globals");
		lspa_caps["discover"] = discover;
	}
	{
		Array write;
		write.push_back("typeof");
		write.push_back("signature");
		write.push_back("complete");
		lspa_caps["write"] = write;
	}
	{
		Array verify;
		verify.push_back("lint");
		verify.push_back("check");
		verify.push_back("contract");
		lspa_caps["verify"] = verify;
	}
	result["lspaCapabilities"] = lspa_caps;

	// Add linterdb stats.
	LinterDB *db = LinterDB::get_singleton();
	if (db) {
		Dictionary stats;
		LocalVector<StringName> class_list;
		db->get_class_list(class_list);
		stats["classes"] = (int)class_list.size();

		LocalVector<String> builtin_list;
		db->get_builtin_type_list(builtin_list);
		stats["builtin_types"] = (int)builtin_list.size();

		LocalVector<StringName> singleton_list;
		db->get_singleton_list(singleton_list);
		stats["singletons"] = (int)singleton_list.size();

		LocalVector<StringName> uf_list;
		db->get_utility_function_list(uf_list);
		stats["utility_functions"] = (int)uf_list.size();

		result["lspaStats"] = stats;
	}

	return make_response(p_id, result);
}

void Server::handle_initialized() {
	// Client is ready.
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

	// Update class registry if this file declares a class_name (scripts only).
	String path = uri_to_path(uri);
	if (workspace::is_script_file(path)) {
		String cname = workspace::extract_class_name(text);
		if (!cname.is_empty()) {
			class_to_path[cname] = path;
			class_to_extends[cname] = workspace::extract_extends(text);
			workspace::register_classes(class_to_path, class_to_extends);
		}
	}

	publish_diagnostics_for_file(uri, text);
}

void Server::handle_did_change(const Dictionary &p_params) {
	Dictionary td = p_params["textDocument"];
	String uri = td["uri"];

	Array changes = p_params["contentChanges"];
	if (changes.size() == 0) {
		return;
	}
	Dictionary last_change = changes[changes.size() - 1];
	String text = last_change["text"];

	DocumentState &doc = documents[uri];
	doc.content = text;
	doc.version = td.has("version") ? (int)td["version"] : doc.version + 1;

	// Update class registry (scripts only).
	String path = uri_to_path(uri);
	if (workspace::is_script_file(path)) {
		String cname = workspace::extract_class_name(text);
		if (!cname.is_empty()) {
			class_to_path[cname] = path;
			class_to_extends[cname] = workspace::extract_extends(text);
			workspace::register_classes(class_to_path, class_to_extends);
		}
	}

	publish_diagnostics_for_file(uri, text);
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

	if (documents.has(uri)) {
		publish_diagnostics_for_file(uri, documents[uri].content);
	} else {
		String path = uri_to_path(uri);
		String source = FileAccess::get_file_as_string(path);
		publish_diagnostics_for_file(uri, source);
	}
}

void Server::handle_did_change_watched_files(const Dictionary &p_params) {
	Array changes = p_params["changes"];
	bool needs_rescan = false;
	for (int i = 0; i < changes.size(); i++) {
		Dictionary change = changes[i];
		String uri = change["uri"];
		String path = uri_to_path(uri);
		if (workspace::is_lintable_file(path)) {
			needs_rescan = true;
			break;
		}
	}

	if (needs_rescan) {
		scan_workspace_classes();

		// Also pick up class_name from open documents that may not be saved yet.
		for (const KeyValue<String, DocumentState> &kv : documents) {
			String cname = workspace::extract_class_name(kv.value.content);
			if (!cname.is_empty()) {
				String path = uri_to_path(kv.value.uri);
				class_to_path[cname] = path;
				class_to_extends[cname] = workspace::extract_extends(kv.value.content);
			}
		}

		workspace::register_classes(class_to_path, class_to_extends);

		// Re-lint all open documents so diagnostics update.
		for (const KeyValue<String, DocumentState> &kv : documents) {
			publish_diagnostics_for_file(kv.value.uri, kv.value.content);
		}
	}
}

// ---------------------------------------------------------------------------
// Message dispatch (unified: LSP + LSPA)
// ---------------------------------------------------------------------------

bool Server::process_message(const Dictionary &p_msg) {
	String method = p_msg.get("method", "");
	Variant id = p_msg.get("id", Variant());
	Dictionary params = p_msg.get("params", Dictionary());
	bool has_id = p_msg.has("id");

	// --- Lifecycle ---
	if (method == "initialize") {
		Transport::write_message(handle_initialize(id, params));
		return true;
	}

	if (method == "shutdown") {
		Transport::write_message(handle_shutdown(id));
		return true;
	}

	if (method == "exit") {
		return false;
	}

	// Before initialization, reject everything except initialize.
	if (!initialized && has_id) {
		Transport::write_message(make_error_response(id, SERVER_NOT_INITIALIZED, "Server not initialized"));
		return true;
	}

	// --- LSP Notifications ---
	if (method == "initialized") {
		handle_initialized();
		return true;
	}

	// --- LSP Requests (textDocument/*) ---
	if (method == "textDocument/completion") {
		Transport::write_message(completion_handler.handle(id, params));
		return true;
	}

	if (method == "textDocument/signatureHelp") {
		Transport::write_message(signature_handler.handle(id, params));
		return true;
	}

	if (method == "textDocument/definition") {
		Transport::write_message(definition_handler.handle(id, params));
		return true;
	}

	if (method == "textDocument/hover") {
		Transport::write_message(hover_handler.handle(id, params));
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

	// --- LSPA DISCOVER domain (api/*) ---
	if (method == "api/class") {
		Transport::write_message(make_response(id, query_engine.handle_class(params)));
		return true;
	}
	if (method == "api/classes") {
		Transport::write_message(make_response(id, query_engine.handle_classes(params)));
		return true;
	}
	if (method == "api/search") {
		Transport::write_message(make_response(id, query_engine.handle_search(params)));
		return true;
	}
	if (method == "api/hierarchy") {
		Transport::write_message(make_response(id, query_engine.handle_hierarchy(params)));
		return true;
	}
	if (method == "api/catalog") {
		Transport::write_message(make_response(id, query_engine.handle_catalog(params)));
		return true;
	}
	if (method == "api/globals") {
		Transport::write_message(make_response(id, query_engine.handle_globals(params)));
		return true;
	}

	// --- LSPA VERIFY domain (verify/*) ---
	if (method == "verify/lint") {
		Transport::write_message(make_response(id, verifier.handle_lint(params)));
		return true;
	}
	if (method == "verify/check") {
		Transport::write_message(make_response(id, verifier.handle_check(params)));
		return true;
	}

	// --- LSPA WRITE domain (code/*) — stubs ---
	if (method == "code/typeof" || method == "code/signature" || method == "code/complete") {
		Transport::write_message(make_error_response(id, METHOD_NOT_FOUND,
				vformat("Method not yet implemented: %s", method)));
		return true;
	}

	// --- verify/contract — stub ---
	if (method == "verify/contract") {
		Transport::write_message(make_error_response(id, METHOD_NOT_FOUND,
				vformat("Method not yet implemented: %s", method)));
		return true;
	}

	// Unknown request — send MethodNotFound if it has an id.
	if (has_id) {
		Transport::write_message(make_error_response(id, METHOD_NOT_FOUND,
				vformat("Method not found: %s", method)));
	}

	return true;
}

} // namespace lsp

#endif // HOMOT
