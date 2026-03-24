/**************************************************************************/
/*  lspa_server.h                                                         */
/**************************************************************************/
/*  LSPA server — stateless JSON-RPC request dispatcher for AI agents.    */
/*  Handles api/* (DISCOVER), code/* (WRITE), and verify/* (VERIFY).      */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "query_engine.h"
#include "verifier.h"

#include "core/variant/dictionary.h"

namespace lspa {

class Server {
	bool initialized = false;
	bool shutdown_requested = false;

	// LinterDB path (from CLI).
	String db_path;

	// Sub-handlers.
	QueryEngine query_engine;
	Verifier verifier;

	// --- Request handlers ---
	Dictionary handle_initialize(const Variant &p_id);
	Dictionary handle_shutdown(const Variant &p_id);

public:
	void set_db_path(const String &p_path) { db_path = p_path; }

	// Process one JSON-RPC message. Returns true to continue, false to exit.
	bool process_message(const Dictionary &p_msg);
};

} // namespace lspa

#endif // HOMOT
