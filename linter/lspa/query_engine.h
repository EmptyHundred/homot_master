/**************************************************************************/
/*  query_engine.h                                                        */
/**************************************************************************/
/*  LSPA DISCOVER domain — API query engine built on LinterDB.            */
/*  Also queries ScriptServerStub for project script classes.             */
/*  Handles api/class, api/classes, api/search, api/hierarchy,            */
/*  api/catalog, api/globals.                                             */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "formatter.h"

#include "core/string/string_name.h"
#include "core/templates/hash_map.h"
#include "core/variant/dictionary.h"

namespace lspa {

// Cached data extracted from a parsed GDScript file.
struct ScriptClassInfo {
	StringName name;
	StringName extends; // Script-level extends (could be another script class).
	StringName native_base; // Resolved native engine base class.
	String path;

	// Extracted members.
	Vector<Dictionary> methods; // [{name, sig}]
	Vector<Dictionary> properties; // [{name, type}]
	Vector<Dictionary> signals; // [{name, args}]
	Vector<Dictionary> enums; // [{name, values}]
	Vector<Dictionary> constants; // [{name, value}]
};

class QueryEngine {
	// Cache of parsed script classes. Populated lazily on first access.
	HashMap<StringName, ScriptClassInfo> script_cache;
	bool script_cache_built = false;

	void ensure_script_cache();
	ScriptClassInfo *get_script_class(const StringName &p_name);

	// Format a ScriptClassInfo into the same Dictionary shape as format_class.
	Dictionary format_script_class(const ScriptClassInfo &p_info, DetailLevel p_detail, const Vector<String> &p_sections = Vector<String>());

	// Resolve a class name (case-insensitive) across both LinterDB and script cache.
	StringName _resolve_class_name(const String &p_name, bool &r_is_script);

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

	// Invalidate the script class cache (e.g., after verify/lint re-scans).
	void invalidate_script_cache();
};

} // namespace lspa

#endif // HOMOT
