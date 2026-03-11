/**************************************************************************/
/*  classdb_stub.h                                                        */
/**************************************************************************/
/*  Stub MethodBind for the linter. Stores metadata only, no actual       */
/*  method binding. Used by ClassDB::get_method() return values.          */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "core/object/method_bind.h"

namespace linter {

// A MethodBind subclass that stores metadata from JSON.
// The analyzer only reads: is_vararg(), is_static(), get_instance_class(),
// get_return_info(), get_default_argument_count().
class StubMethodBind : public MethodBind {
	PropertyInfo return_info;
	bool _is_vararg = false;

protected:
	virtual Variant::Type _gen_argument_type(int p_arg) const override {
		return Variant::NIL;
	}
	virtual PropertyInfo _gen_argument_type_info(int p_arg) const override {
		return PropertyInfo();
	}
#ifdef DEBUG_ENABLED
	virtual GodotTypeInfo::Metadata get_argument_meta(int p_arg) const override {
		return GodotTypeInfo::METADATA_NONE;
	}
#endif

public:
	virtual Variant call(Object *p_object, const Variant **p_args, int p_arg_count, Callable::CallError &r_error) const override {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
		return Variant();
	}
	virtual void validated_call(Object *p_object, const Variant **p_args, Variant *r_ret) const override {}
	virtual void ptrcall(Object *p_object, const void **p_args, void *r_ret) const override {}
	virtual bool is_vararg() const override { return _is_vararg; }

	void set_return_info(const PropertyInfo &p_info) { return_info = p_info; }
	void setup(const StringName &p_name, const StringName &p_instance_class, bool p_static, bool p_vararg, int p_arg_count, int p_default_arg_count);
};

// Manages lifetime of all stub MethodBind instances.
void init_classdb_stubs();
void cleanup_classdb_stubs();

} // namespace linter

#endif // HOMOT
