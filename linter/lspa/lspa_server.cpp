/**************************************************************************/
/*  lspa_server.cpp                                                       */
/**************************************************************************/

#ifdef HOMOT

#include "lspa_server.h"

#include "../stubs/linterdb.h"
#include "../stubs/script_server_stub.h"

// Reuse JSON-RPC transport and protocol helpers from the LSP implementation.
#include "../lsp/lsp_protocol.h"
#include "../lsp/lsp_transport.h"

using linter::LinterDB;
using linter::ScriptServerStub;

namespace lspa {

// ---------------------------------------------------------------------------
// JSON-RPC helpers (reuse lsp:: helpers)
// ---------------------------------------------------------------------------

static Dictionary _make_response(const Variant &p_id, const Variant &p_result) {
	return lsp::make_response(p_id, p_result);
}

static Dictionary _make_error(const Variant &p_id, int p_code, const String &p_message) {
	return lsp::make_error_response(p_id, p_code, p_message);
}

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------

Dictionary Server::handle_initialize(const Variant &p_id) {
	// Load linter database.
	if (!db_path.is_empty() && !LinterDB::get_singleton()) {
		LinterDB *ldb = memnew(LinterDB);
		Error err = ldb->load_from_json(db_path);
		if (err != OK) {
			memdelete(ldb);
			return _make_error(p_id, lsp::INTERNAL_ERROR,
					vformat("Failed to load linterdb: %s", db_path));
		}
	}

	initialized = true;

	// Build capabilities and stats.
	Dictionary caps;
	{
		Array discover;
		discover.push_back("class");
		discover.push_back("classes");
		discover.push_back("search");
		discover.push_back("hierarchy");
		discover.push_back("catalog");
		discover.push_back("globals");
		caps["discover"] = discover;
	}
	{
		Array write;
		write.push_back("typeof");
		write.push_back("signature");
		write.push_back("complete");
		caps["write"] = write;
	}
	{
		Array verify;
		verify.push_back("lint");
		verify.push_back("check");
		verify.push_back("contract");
		caps["verify"] = verify;
	}

	Dictionary stats;
	LinterDB *db = LinterDB::get_singleton();
	if (db) {
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
	}

	int script_class_count = ScriptServerStub::get_global_class_count();
	if (script_class_count > 0) {
		stats["script_classes"] = script_class_count;
	}

	Dictionary result;
	result["name"] = "homot-lspa";
	result["version"] = "0.1.0";
	result["capabilities"] = caps;
	result["stats"] = stats;

	return _make_response(p_id, result);
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------

Dictionary Server::handle_shutdown(const Variant &p_id) {
	shutdown_requested = true;
	return _make_response(p_id, Variant());
}

// ---------------------------------------------------------------------------
// Message dispatch
// ---------------------------------------------------------------------------

bool Server::process_message(const Dictionary &p_msg) {
	String method = p_msg.get("method", "");
	Variant id = p_msg.get("id", Variant());
	Dictionary params = p_msg.get("params", Dictionary());
	bool has_id = p_msg.has("id");

	// --- Lifecycle ---
	if (method == "initialize") {
		lsp::Transport::write_message(handle_initialize(id));
		return true;
	}
	if (method == "shutdown") {
		lsp::Transport::write_message(handle_shutdown(id));
		return true;
	}
	if (method == "exit") {
		return false;
	}

	// Before initialization, reject everything.
	if (!initialized && has_id) {
		lsp::Transport::write_message(_make_error(id, lsp::SERVER_NOT_INITIALIZED, "Server not initialized"));
		return true;
	}

	// --- DISCOVER domain ---
	if (method == "api/class") {
		lsp::Transport::write_message(_make_response(id, query_engine.handle_class(params)));
		return true;
	}
	if (method == "api/classes") {
		lsp::Transport::write_message(_make_response(id, query_engine.handle_classes(params)));
		return true;
	}
	if (method == "api/search") {
		lsp::Transport::write_message(_make_response(id, query_engine.handle_search(params)));
		return true;
	}
	if (method == "api/hierarchy") {
		lsp::Transport::write_message(_make_response(id, query_engine.handle_hierarchy(params)));
		return true;
	}
	if (method == "api/catalog") {
		lsp::Transport::write_message(_make_response(id, query_engine.handle_catalog(params)));
		return true;
	}
	if (method == "api/globals") {
		lsp::Transport::write_message(_make_response(id, query_engine.handle_globals(params)));
		return true;
	}

	// --- VERIFY domain ---
	if (method == "verify/lint") {
		lsp::Transport::write_message(_make_response(id, verifier.handle_lint(params)));
		return true;
	}
	if (method == "verify/check") {
		lsp::Transport::write_message(_make_response(id, verifier.handle_check(params)));
		return true;
	}

	// --- WRITE domain (Phase 2 — stubs for now) ---
	if (method == "code/typeof" || method == "code/signature" || method == "code/complete") {
		lsp::Transport::write_message(_make_error(id, lsp::METHOD_NOT_FOUND,
				vformat("Method not yet implemented: %s", method)));
		return true;
	}

	// --- verify/contract (Phase 3 — stub) ---
	if (method == "verify/contract") {
		lsp::Transport::write_message(_make_error(id, lsp::METHOD_NOT_FOUND,
				vformat("Method not yet implemented: %s", method)));
		return true;
	}

	// Unknown request.
	if (has_id) {
		lsp::Transport::write_message(_make_error(id, lsp::METHOD_NOT_FOUND,
				vformat("Method not found: %s", method)));
	}

	return true;
}

} // namespace lspa

#endif // HOMOT
