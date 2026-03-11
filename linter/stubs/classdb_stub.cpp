/**************************************************************************/
/*  classdb_stub.cpp                                                      */
/**************************************************************************/
/*  Stub ClassDB implementation for the standalone linter.                */
/*  Delegates all queries to linter::LinterDB loaded from JSON.           */
/*  This file is compiled INSTEAD OF core/object/class_db.cpp when        */
/*  building the linter target.                                           */
/**************************************************************************/

#ifdef HOMOT

#include "classdb_stub.h"
#include "linterdb.h"

#include "core/object/class_db.h"

namespace linter {

// Storage for all stub MethodBind instances.
static HashMap<String, StubMethodBind *> stub_method_binds;

void StubMethodBind::setup(const StringName &p_name, const StringName &p_instance_class, bool p_static, bool p_vararg, int p_arg_count, int p_default_arg_count) {
	set_name(p_name);
	set_instance_class(p_instance_class);
	_set_static(p_static);
	_is_vararg = p_vararg;
	set_argument_count(p_arg_count);

	Vector<Variant> defaults;
	defaults.resize(p_default_arg_count);
	set_default_arguments(defaults);
}

static StubMethodBind *get_or_create_stub_method(const StringName &p_class, const StringName &p_method) {
	String key = String(p_class) + "::" + String(p_method);
	auto it = stub_method_binds.find(key);
	if (it) {
		return it->value;
	}

	LinterDB *db = LinterDB::get_singleton();
	if (!db) {
		return nullptr;
	}

	const MethodData *md = db->get_method_data(p_class, p_method);
	if (!md) {
		return nullptr;
	}

	StubMethodBind *mb = memnew(StubMethodBind);
	mb->setup(
			p_method,
			md->instance_class,
			md->is_static,
			md->is_vararg,
			md->info.arguments.size(),
			md->info.default_arguments.size());
	mb->set_return_info(md->info.return_val);

	stub_method_binds[key] = mb;
	return mb;
}

void init_classdb_stubs() {
	// Called after LinterDB is loaded. Pre-creates nothing;
	// StubMethodBind instances are created lazily.
}

void cleanup_classdb_stubs() {
	for (KeyValue<String, StubMethodBind *> &kv : stub_method_binds) {
		memdelete(kv.value);
	}
	stub_method_binds.clear();
}

} // namespace linter

// ==========================================================================
// ClassDB static method stubs.
// These match the signatures in core/object/class_db.h and delegate to
// linter::LinterDB. Only the methods actually called by the GDScript
// analyzer/parser are implemented here.
// ==========================================================================

// Note: These are NOT inside namespace linter — they are the real ClassDB
// static methods, providing the link-time replacement.

#define LINTER_DB() linter::LinterDB::get_singleton()

bool ClassDB::class_exists(const StringName &p_class) {
	return LINTER_DB() && LINTER_DB()->class_exists(p_class);
}

bool ClassDB::is_class_exposed(const StringName &p_class) {
	// All classes in linterdb.json are exposed (we filtered during dump).
	return LINTER_DB() && LINTER_DB()->class_exists(p_class);
}

bool ClassDB::is_abstract(const StringName &p_class) {
	return LINTER_DB() && LINTER_DB()->is_abstract(p_class);
}

StringName ClassDB::get_parent_class(const StringName &p_class) {
	return LINTER_DB() ? LINTER_DB()->get_parent_class(p_class) : StringName();
}

StringName ClassDB::get_parent_class_nocheck(const StringName &p_class) {
	return LINTER_DB() ? LINTER_DB()->get_parent_class(p_class) : StringName();
}

bool ClassDB::is_parent_class(const StringName &p_class, const StringName &p_inherits) {
	return LINTER_DB() && LINTER_DB()->is_parent_class(p_class, p_inherits);
}

void ClassDB::get_class_list(LocalVector<StringName> &p_classes) {
	if (LINTER_DB()) {
		LINTER_DB()->get_class_list(p_classes);
	}
}

// --- Methods ---

bool ClassDB::has_method(const StringName &p_class, const StringName &p_method, bool p_no_inheritance) {
	return LINTER_DB() && LINTER_DB()->has_method(p_class, p_method, p_no_inheritance);
}

bool ClassDB::get_method_info(const StringName &p_class, const StringName &p_method, MethodInfo *r_info, bool p_no_inheritance, bool p_exclude_from_properties) {
	return LINTER_DB() && LINTER_DB()->get_method_info(p_class, p_method, r_info);
}

MethodBind *ClassDB::get_method(const StringName &p_class, const StringName &p_name) {
	return linter::get_or_create_stub_method(p_class, p_name);
}

void ClassDB::get_method_list(const StringName &p_class, List<MethodInfo> *p_methods, bool p_no_inheritance, bool p_exclude_from_properties) {
	if (LINTER_DB()) {
		LINTER_DB()->get_method_list(p_class, p_methods, p_no_inheritance);
	}
}

// --- Properties ---

bool ClassDB::has_property(const StringName &p_class, const StringName &p_property, bool p_no_inheritance) {
	return LINTER_DB() && LINTER_DB()->has_property(p_class, p_property, p_no_inheritance);
}

bool ClassDB::get_property_info(const StringName &p_class, const StringName &p_property, PropertyInfo *r_info, bool p_no_inheritance, const Object *p_validator) {
	if (!LINTER_DB()) {
		return false;
	}
	const linter::PropertyData *pd = LINTER_DB()->get_property_data(p_class, p_property);
	if (pd && r_info) {
		*r_info = pd->info;
	}
	return pd != nullptr;
}

StringName ClassDB::get_property_getter(const StringName &p_class, const StringName &p_property) {
	if (!LINTER_DB()) {
		return StringName();
	}
	const linter::PropertyData *pd = LINTER_DB()->get_property_data(p_class, p_property);
	return pd ? pd->getter : StringName();
}

StringName ClassDB::get_property_setter(const StringName &p_class, const StringName &p_property) {
	if (!LINTER_DB()) {
		return StringName();
	}
	const linter::PropertyData *pd = LINTER_DB()->get_property_data(p_class, p_property);
	return pd ? pd->setter : StringName();
}

void ClassDB::get_property_list(const StringName &p_class, List<PropertyInfo> *p_list, bool p_no_inheritance, const Object *p_validator) {
	if (LINTER_DB()) {
		LINTER_DB()->get_property_list(p_class, p_list, p_no_inheritance);
	}
}

// --- Signals ---

bool ClassDB::has_signal(const StringName &p_class, const StringName &p_signal, bool p_no_inheritance) {
	return LINTER_DB() && LINTER_DB()->has_signal(p_class, p_signal, p_no_inheritance);
}

bool ClassDB::get_signal(const StringName &p_class, const StringName &p_signal, MethodInfo *r_signal) {
	return LINTER_DB() && LINTER_DB()->get_signal_info(p_class, p_signal, r_signal);
}

void ClassDB::get_signal_list(const StringName &p_class, List<MethodInfo> *p_signals, bool p_no_inheritance) {
	if (LINTER_DB()) {
		LINTER_DB()->get_signal_list(p_class, p_signals, p_no_inheritance);
	}
}

// --- Enums ---

bool ClassDB::has_enum(const StringName &p_class, const StringName &p_name, bool p_no_inheritance) {
	return LINTER_DB() && LINTER_DB()->has_enum(p_class, p_name, p_no_inheritance);
}

void ClassDB::get_enum_list(const StringName &p_class, List<StringName> *p_enums, bool p_no_inheritance) {
	if (LINTER_DB()) {
		LINTER_DB()->get_enum_list(p_class, p_enums, p_no_inheritance);
	}
}

void ClassDB::get_enum_constants(const StringName &p_class, const StringName &p_enum, List<StringName> *p_constants, bool p_no_inheritance) {
	if (LINTER_DB()) {
		LINTER_DB()->get_enum_constants(p_class, p_enum, p_constants, p_no_inheritance);
	}
}

// --- Integer constants ---

bool ClassDB::has_integer_constant(const StringName &p_class, const StringName &p_name, bool p_no_inheritance) {
	return LINTER_DB() && LINTER_DB()->has_integer_constant(p_class, p_name, p_no_inheritance);
}

int64_t ClassDB::get_integer_constant(const StringName &p_class, const StringName &p_name, bool *p_success) {
	return LINTER_DB() ? LINTER_DB()->get_integer_constant(p_class, p_name, p_success) : 0;
}

StringName ClassDB::get_integer_constant_enum(const StringName &p_class, const StringName &p_name, bool p_no_inheritance) {
	return LINTER_DB() ? LINTER_DB()->get_integer_constant_enum(p_class, p_name, p_no_inheritance) : StringName();
}

void ClassDB::get_integer_constant_list(const StringName &p_class, List<String> *p_constants, bool p_no_inheritance) {
	if (LINTER_DB()) {
		LINTER_DB()->get_integer_constant_list(p_class, p_constants, p_no_inheritance);
	}
}

#undef LINTER_DB

#endif // HOMOT
