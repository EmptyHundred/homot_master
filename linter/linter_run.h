/**************************************************************************/
/*  linter_run.h                                                          */
/**************************************************************************/
/*  GDScript directory linter — scans a directory of scripts and runs     */
/*  the GDScript analyzer on each file.                                   */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "core/string/ustring.h"

namespace linter {

// Lint all .gd/.hm/.hmc scripts in p_dir.
// Optionally loads linterdb.json from p_db_path (empty = use engine ClassDB).
// Returns 0 if no errors, 1 if errors found.
int run_lint(const Vector<String> &p_paths, const String &p_db_path = String());

} // namespace linter

#endif // HOMOT
