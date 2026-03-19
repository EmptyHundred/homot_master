/**************************************************************************/
/*  lsp_completion.h                                                      */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

class GDScriptParser;

namespace lsp {

class Server;

class CompletionHandler {
	Server &server;

	String insert_cursor_sentinel(const String &p_source, int p_line, int p_character);
	void collect_completions_for_context(const GDScriptParser &p_parser, Array &r_items);

public:
	CompletionHandler(Server &s) : server(s) {}
	Dictionary handle(const Variant &p_id, const Dictionary &p_params);
};

} // namespace lsp

#endif // HOMOT
