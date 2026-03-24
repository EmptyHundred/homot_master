/**************************************************************************/
/*  query_engine.h                                                        */
/**************************************************************************/
/*  LSPA DISCOVER domain — API query engine built on LinterDB.            */
/*  Handles api/class, api/classes, api/search, api/hierarchy,            */
/*  api/catalog, api/globals.                                             */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "formatter.h"

#include "core/variant/dictionary.h"

namespace lspa {

class QueryEngine {
public:
	// api/class — query a single class.
	Dictionary handle_class(const Dictionary &p_params);

	// api/classes — batch query multiple classes.
	Dictionary handle_classes(const Dictionary &p_params);

	// api/search — keyword search across all classes. Returns Array directly.
	Variant handle_search(const Dictionary &p_params);

	// api/hierarchy — get inheritance chain.
	Dictionary handle_hierarchy(const Dictionary &p_params);

	// api/catalog — list available classes by domain.
	Dictionary handle_catalog(const Dictionary &p_params);

	// api/globals — query singletons, utility functions, global enums/constants.
	Variant handle_globals(const Dictionary &p_params);
};

} // namespace lspa

#endif // HOMOT
