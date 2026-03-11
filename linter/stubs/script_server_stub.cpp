/**************************************************************************/
/*  script_server_stub.cpp                                                */
/**************************************************************************/
/*  Stub for ScriptServer global class queries.                           */
/*  This file is compiled INSTEAD OF the real ScriptServer                */
/*  implementation when building the linter target.                       */
/*                                                                        */
/*  The analyzer calls:                                                   */
/*    - ScriptServer::is_global_class(name)                               */
/*    - ScriptServer::get_global_class_path(name)                         */
/*    - ScriptServer::get_global_class_native_base(name)                  */
/**************************************************************************/

#ifdef HOMOT

#include "script_server_stub.h"

#include "core/object/script_language.h"

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

// --- ScriptServer static method stubs ---

bool ScriptServer::is_global_class(const StringName &p_class) {
	return linter::ScriptServerStub::is_global_class(p_class);
}

String ScriptServer::get_global_class_path(const String &p_class) {
	return linter::ScriptServerStub::get_global_class_path(StringName(p_class));
}

StringName ScriptServer::get_global_class_native_base(const String &p_class) {
	return linter::ScriptServerStub::get_global_class_native_base(StringName(p_class));
}

bool ScriptServer::is_global_class_abstract(const String &p_class) {
	return false;
}

bool ScriptServer::is_global_class_tool(const String &p_class) {
	return false;
}

#endif // HOMOT
