/**************************************************************************/
/*  lsp_protocol.h                                                        */
/**************************************************************************/
/*  LSP type definitions — minimal subset for diagnostics-first LSP.      */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "core/io/json.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

namespace lsp {

// ---------------------------------------------------------------------------
// Basic LSP structures
// ---------------------------------------------------------------------------

struct Position {
	int line = 0; // 0-based
	int character = 0; // 0-based

	Dictionary to_dict() const {
		Dictionary d;
		d["line"] = line;
		d["character"] = character;
		return d;
	}
};

struct Range {
	Position start;
	Position end;

	Dictionary to_dict() const {
		Dictionary d;
		d["start"] = start.to_dict();
		d["end"] = end.to_dict();
		return d;
	}
};

// DiagnosticSeverity
enum DiagnosticSeverity {
	SEVERITY_ERROR = 1,
	SEVERITY_WARNING = 2,
	SEVERITY_INFORMATION = 3,
	SEVERITY_HINT = 4,
};

struct Diagnostic {
	Range range;
	int severity = SEVERITY_ERROR;
	String source = "gdscript";
	String message;
	String code; // warning code name, empty for errors

	Dictionary to_dict() const {
		Dictionary d;
		d["range"] = range.to_dict();
		d["severity"] = severity;
		d["source"] = source;
		d["message"] = message;
		if (!code.is_empty()) {
			d["code"] = code;
		}
		return d;
	}
};

// ---------------------------------------------------------------------------
// Server capabilities
// ---------------------------------------------------------------------------

inline Dictionary make_server_capabilities() {
	Dictionary caps;

	// We support full document sync (client sends entire text on change).
	Dictionary text_doc_sync;
	text_doc_sync["openClose"] = true;
	text_doc_sync["change"] = 1; // TextDocumentSyncKind.Full
	text_doc_sync["save"] = true;
	caps["textDocumentSync"] = text_doc_sync;

	return caps;
}

inline Dictionary make_initialize_result() {
	Dictionary result;
	result["capabilities"] = make_server_capabilities();

	Dictionary server_info;
	server_info["name"] = "homot-lsp";
	server_info["version"] = "0.1.0";
	result["serverInfo"] = server_info;

	return result;
}

// ---------------------------------------------------------------------------
// JSON-RPC helpers
// ---------------------------------------------------------------------------

inline Dictionary make_response(const Variant &p_id, const Variant &p_result) {
	Dictionary msg;
	msg["jsonrpc"] = "2.0";
	msg["id"] = p_id;
	msg["result"] = p_result;
	return msg;
}

inline Dictionary make_error_response(const Variant &p_id, int p_code, const String &p_message) {
	Dictionary err;
	err["code"] = p_code;
	err["message"] = p_message;

	Dictionary msg;
	msg["jsonrpc"] = "2.0";
	msg["id"] = p_id;
	msg["error"] = err;
	return msg;
}

inline Dictionary make_notification(const String &p_method, const Variant &p_params) {
	Dictionary msg;
	msg["jsonrpc"] = "2.0";
	msg["method"] = p_method;
	msg["params"] = p_params;
	return msg;
}

// LSP error codes
enum ErrorCode {
	PARSE_ERROR = -32700,
	INVALID_REQUEST = -32600,
	METHOD_NOT_FOUND = -32601,
	INVALID_PARAMS = -32602,
	INTERNAL_ERROR = -32603,
	SERVER_NOT_INITIALIZED = -32002,
};

} // namespace lsp

#endif // HOMOT
