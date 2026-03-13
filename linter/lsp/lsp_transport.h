/**************************************************************************/
/*  lsp_transport.h                                                       */
/**************************************************************************/
/*  JSON-RPC transport over stdin/stdout using LSP base protocol.         */
/*  Messages use Content-Length headers as per the LSP specification.      */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "core/variant/dictionary.h"

namespace lsp {

class Transport {
public:
	// Read one JSON-RPC message from stdin. Returns empty Dictionary on EOF.
	static Dictionary read_message();

	// Write a JSON-RPC message to stdout with Content-Length header.
	static void write_message(const Dictionary &p_msg);
};

} // namespace lsp

#endif // HOMOT
