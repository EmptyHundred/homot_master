/**************************************************************************/
/*  lsp_server.h                                                          */
/**************************************************************************/
/*  LSP server — manages documents, dispatches requests, publishes        */
/*  diagnostics by running the GDScript parser/analyzer.                  */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "lsp_completion.h"
#include "lsp_definition.h"
#include "lsp_hover.h"
#include "lsp_protocol.h"
#include "lsp_signature_help.h"

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

	// Doc cache for go-to-definition on native symbols.
	String doc_cache_dir;
	HashMap<String, String> doc_file_cache; // symbol -> file path

	// Global class registry (class_name -> file path, class_name -> extends).
	HashMap<String, String> class_to_path;
	HashMap<String, String> class_to_extends;

	// --- Handler sub-objects ---
	CompletionHandler completion_handler;
	SignatureHandler signature_handler;
	DefinitionHandler definition_handler;
	HoverHandler hover_handler;

	// --- Request handlers ---
	Dictionary handle_initialize(const Variant &p_id, const Dictionary &p_params);
	void handle_initialized();
	Dictionary handle_shutdown(const Variant &p_id);

	// --- Notification handlers ---
	void handle_did_open(const Dictionary &p_params);
	void handle_did_change(const Dictionary &p_params);
	void handle_did_close(const Dictionary &p_params);
	void handle_did_save(const Dictionary &p_params);
	void handle_did_change_watched_files(const Dictionary &p_params);

	// --- Diagnostics ---
	void publish_diagnostics(const String &p_uri, const String &p_source);
	void clear_diagnostics(const String &p_uri);

	// --- Workspace ---
	void scan_workspace_classes();
	void register_global_classes();

	// Grant handlers access to server state.
	friend class CompletionHandler;
	friend class SignatureHandler;
	friend class DefinitionHandler;
	friend class HoverHandler;

public:
	Server() :
			completion_handler(*this),
			signature_handler(*this),
			definition_handler(*this),
			hover_handler(*this) {}

	void set_db_path(const String &p_path) { db_path = p_path; }

	// URI helpers (public static — used by handlers).
	static String uri_to_path(const String &p_uri);
	static String path_to_uri(const String &p_path);

	// Process one JSON-RPC message. Returns true to continue, false to exit.
	bool process_message(const Dictionary &p_msg);
};

} // namespace lsp

#endif // HOMOT
