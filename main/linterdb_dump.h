/**************************************************************************/
/*  linterdb_dump.h                                                       */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "core/string/ustring.h"

class LinterDBDump {
public:
	// p_path: output JSON file path.
	// p_doc_source_path: Godot source root for loading doc XML (e.g. ".").
	//   If empty, documentation fields are omitted.
	static Error generate_linterdb_json_file(const String &p_path, const String &p_doc_source_path = "");
};

#endif // HOMOT
