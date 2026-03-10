/**************************************************************************/
/*  linterdb_dump.h                                                       */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "core/string/ustring.h"

class LinterDBDump {
public:
	static Error generate_linterdb_json_file(const String &p_path);
};

#endif // HOMOT
