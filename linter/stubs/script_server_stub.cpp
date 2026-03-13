/**************************************************************************/
/*  script_server_stub.cpp                                                */
/**************************************************************************/
/*  ScriptServerStub registry for the standalone linter.                  */
/*  ScriptServer method overrides are now in                              */
/*  core/object/script_language.cpp behind #ifdef HOMOT_LINTER guards.   */
/**************************************************************************/

#ifdef HOMOT

#include "script_server_stub.h"

namespace linter {

HashMap<StringName, String> ScriptServerStub::global_classes;
HashMap<StringName, StringName> ScriptServerStub::global_class_bases;

void ScriptServerStub::register_global_class(const StringName &p_class, const String &p_path, const StringName &p_native_base) {
	global_classes[p_class] = p_path;
	global_class_bases[p_class] = p_native_base;
}

void ScriptServerStub::clear() {
	global_classes.clear();
	global_class_bases.clear();
}

bool ScriptServerStub::is_global_class(const StringName &p_class) {
	return global_classes.has(p_class);
}

String ScriptServerStub::get_global_class_path(const StringName &p_class) {
	auto it = global_classes.find(p_class);
	return it ? it->value : String();
}

StringName ScriptServerStub::get_global_class_native_base(const StringName &p_class) {
	auto it = global_class_bases.find(p_class);
	return it ? it->value : StringName();
}

} // namespace linter

#endif // HOMOT
