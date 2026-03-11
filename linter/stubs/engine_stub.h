/**************************************************************************/
/*  engine_stub.h                                                         */
/**************************************************************************/
/*  Stub Engine singleton for the standalone linter.                      */
/*  Only provides has_singleton() using data from LinterDB.               */
/**************************************************************************/

#pragma once

#ifdef HOMOT

namespace linter {

// No extra interface needed — Engine::has_singleton() is stubbed directly
// in engine_stub.cpp by delegating to LinterDB::has_singleton().

} // namespace linter

#endif // HOMOT
