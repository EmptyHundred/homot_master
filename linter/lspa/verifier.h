/**************************************************************************/
/*  verifier.h                                                            */
/**************************************************************************/
/*  LSPA VERIFY domain — batch lint and inline code check.                */
/*  Reuses GDScriptParser/Analyzer from the linter infrastructure.        */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "core/variant/dictionary.h"

namespace lspa {

class Verifier {
public:
	// verify/lint — lint files/directories on disk.
	Dictionary handle_lint(const Dictionary &p_params);

	// verify/check — check inline code content (no file on disk needed).
	Dictionary handle_check(const Dictionary &p_params);
};

} // namespace lspa

#endif // HOMOT
