/**************************************************************************/
/*  resource_loader_stub.h                                                */
/**************************************************************************/
/*  Stub ResourceLoader for the standalone linter.                        */
/*  Provides filesystem existence checks only. load() returns null.       */
/*  Resource type is inferred from file extension.                        */
/**************************************************************************/

#pragma once

#ifdef HOMOT

namespace linter {

// No extra interface needed — ResourceLoader methods are stubbed directly
// in resource_loader_stub.cpp.

} // namespace linter

#endif // HOMOT
