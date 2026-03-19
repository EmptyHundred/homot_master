/**************************************************************************/
/*  lsp_definition.h                                                      */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

namespace lsp {

class Server;

class DefinitionHandler {
	Server &server;

	String get_or_create_doc_file(const String &p_symbol);
	int find_doc_line(const String &p_file_path, const String &p_member);

public:
	DefinitionHandler(Server &s) : server(s) {}
	Dictionary handle(const Variant &p_id, const Dictionary &p_params);
};

} // namespace lsp

#endif // HOMOT
