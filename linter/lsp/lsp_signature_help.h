/**************************************************************************/
/*  lsp_signature_help.h                                                  */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

namespace lsp {

class Server;

class SignatureHandler {
	Server &server;

public:
	SignatureHandler(Server &s) : server(s) {}
	Dictionary handle(const Variant &p_id, const Dictionary &p_params);
};

} // namespace lsp

#endif // HOMOT
