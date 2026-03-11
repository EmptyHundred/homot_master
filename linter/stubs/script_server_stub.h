/**************************************************************************/
/*  script_server_stub.h                                                  */
/**************************************************************************/
/*  Stub ScriptServer for the standalone linter.                          */
/*  Global class resolution delegates to a registry that the linter       */
/*  populates by scanning the target directory.                           */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "core/string/string_name.h"
#include "core/templates/hash_map.h"

namespace linter {

// Registry for global classes within the linter scope.
// The linter main populates this by scanning the target directory for
// class_name declarations before running analysis.
class ScriptServerStub {
	static HashMap<StringName, String> global_classes; // class_name -> script_path
	static HashMap<StringName, StringName> global_class_bases; // class_name -> native_base

public:
	static void register_global_class(const StringName &p_class, const String &p_path, const StringName &p_native_base);
	static void clear();

	static bool is_global_class(const StringName &p_class);
	static String get_global_class_path(const StringName &p_class);
	static StringName get_global_class_native_base(const StringName &p_class);
};

} // namespace linter

#endif // HOMOT
