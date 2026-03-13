/**************************************************************************/
/*  classdb_stub.cpp                                                      */
/**************************************************************************/
/*  StubMethodBind implementation and lazy creation for the linter.       */
/*  ClassDB method overrides are now in core/object/class_db.cpp behind   */
/*  #ifdef HOMOT_LINTER guards.                                          */
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

	Vector<Variant> defaults;
	defaults.resize(p_default_arg_count);
	set_default_arguments(defaults);
}

StubMethodBind *get_or_create_stub_method(const StringName &p_class, const StringName &p_method) {
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

	// Store argument type info so the analyzer can resolve property/method types.
	Vector<PropertyInfo> arg_infos;
	for (const PropertyInfo &pi : md->info.arguments) {
		arg_infos.push_back(pi);
	}
	mb->set_argument_infos(arg_infos);

	// Populate the argument_types array now that return_info and arg_infos are set.
	mb->finalize_types();

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

#endif // HOMOT
