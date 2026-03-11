/**************************************************************************/
/*  project_settings_stub.cpp                                             */
/**************************************************************************/
/*  Stub for ProjectSettings autoload queries.                            */
/*  This file is compiled INSTEAD OF the real ProjectSettings             */
/*  implementation when building the linter target.                       */
/*                                                                        */
/*  The analyzer calls:                                                   */
/*    - ProjectSettings::get_singleton()                                  */
/*    - ProjectSettings::has_autoload(name)                               */
/*    - ProjectSettings::get_autoload(name)                               */
/*  Sandbox scripts are isolated — no autoloads.                          */
/**************************************************************************/

#ifdef HOMOT

#include "core/config/project_settings.h"

bool ProjectSettings::has_autoload(const StringName &p_autoload) const {
	return false;
}

ProjectSettings::AutoloadInfo ProjectSettings::get_autoload(const StringName &p_name) const {
	return AutoloadInfo();
}

#endif // HOMOT
