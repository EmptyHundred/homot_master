/**************************************************************************/
/*  lsp_server.h                                                          */
/**************************************************************************/
/*  LSP server — manages documents, dispatches requests, publishes        */
/*  diagnostics by running the GDScript parser/analyzer.                  */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "lsp_protocol.h"

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

class GDScriptParser;

namespace lsp {

struct DocumentState {
	String uri;
	String content;
	int version = 0;
};

class Server {
	bool initialized = false;
	bool shutdown_requested = false;

	// Open documents keyed by URI.
	HashMap<String, DocumentState> documents;

	// Project root (from initialize params).
	String root_uri;
	String root_path;

	// LinterDB path (from CLI).
	String db_path;

	// Global class registry (class_name -> file path, class_name -> extends).
	HashMap<String, String> class_to_path;
	HashMap<String, String> class_to_extends;

	// --- Request handlers ---
	Dictionary handle_initialize(const Variant &p_id, const Dictionary &p_params);
	void handle_initialized();
	Dictionary handle_shutdown(const Variant &p_id);

	// --- Notification handlers ---
	void handle_did_open(const Dictionary &p_params);
	void handle_did_change(const Dictionary &p_params);
	void handle_did_close(const Dictionary &p_params);
	void handle_did_save(const Dictionary &p_params);

	// --- Completion ---
	Dictionary handle_completion(const Variant &p_id, const Dictionary &p_params);
	String insert_cursor_sentinel(const String &p_source, int p_line, int p_character);
	void collect_completions_for_context(const GDScriptParser &p_parser, Array &r_items);

	// --- Diagnostics ---
	void publish_diagnostics(const String &p_uri, const String &p_source);
	void clear_diagnostics(const String &p_uri);

	// --- Helpers ---
	static String uri_to_path(const String &p_uri);
	static String path_to_uri(const String &p_path);

	void scan_workspace_classes();
	void register_global_classes();

public:
	void set_db_path(const String &p_path) { db_path = p_path; }

	// Process one JSON-RPC message. Returns true to continue, false to exit.
	bool process_message(const Dictionary &p_msg);
};

} // namespace lsp

#endif // HOMOT
