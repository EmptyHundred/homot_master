/**************************************************************************/
/*  lsp_hover.h                                                           */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

namespace lsp {

class Server;

class HoverHandler {
	Server &server;

public:
	HoverHandler(Server &s) : server(s) {}
	Dictionary handle(const Variant &p_id, const Dictionary &p_params);
};

} // namespace lsp

#endif // HOMOT
