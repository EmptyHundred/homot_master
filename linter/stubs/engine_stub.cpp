/**************************************************************************/
/*  engine_stub.cpp                                                       */
/**************************************************************************/
/*  Stub for Engine::has_singleton().                                     */
/*  This file is compiled INSTEAD OF the real Engine implementation        */
/*  when building the linter target.                                      */
/*                                                                        */
/*  The analyzer only calls:                                              */
/*    - Engine::get_singleton()  (returns the singleton)                  */
/*    - Engine::has_singleton(name)  (checks if name is an engine         */
/*      singleton, e.g. "Input", "OS")                                    */
/*    - Engine::set_editor_hint() (no-op for linter)                      */
/**************************************************************************/

#ifdef HOMOT

#include "linterdb.h"

#include "core/config/engine.h"

// Stub implementation of Engine::has_singleton().
// Delegates to LinterDB which loaded the singleton list from JSON.
bool Engine::has_singleton(const StringName &p_name) const {
	linter::LinterDB *db = linter::LinterDB::get_singleton();
	return db && db->has_singleton(p_name);
}

#endif // HOMOT
