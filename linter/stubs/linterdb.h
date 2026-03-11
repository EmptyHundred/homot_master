/**************************************************************************/
/*  linterdb.h                                                            */
/**************************************************************************/
/*  Data store for the standalone GDScript linter.                        */
/*  Loads ClassDB metadata, singletons, builtin types, utility functions, */
/*  and global enums/constants from a JSON dump (linterdb.json).          */
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "core/object/object.h"
#include "core/string/string_name.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/variant/variant.h"

namespace linter {

// Per-method metadata loaded from JSON.
struct MethodData {
	MethodInfo info;
	bool is_vararg = false;
	bool is_static = false;
	StringName instance_class;
};

// Per-property metadata loaded from JSON.
struct PropertyData {
	PropertyInfo info;
	StringName getter;
	StringName setter;
};

// Per-class metadata loaded from JSON.
struct ClassData {
	StringName name;
	StringName parent;
	bool is_abstract = false;

	HashMap<StringName, MethodData> methods;
	HashMap<StringName, PropertyData> properties;
	HashMap<StringName, MethodInfo> signals;
	// enum_name -> { constant_name -> value }
	HashMap<StringName, HashMap<StringName, int64_t>> enums;
	// constant_name -> value (constants not part of any enum)
	HashMap<StringName, int64_t> constants;
	// constant_name -> enum_name (reverse lookup)
	HashMap<StringName, StringName> constant_to_enum;
};

// Singleton data store. Loaded once at startup.
class LinterDB {
	static LinterDB *singleton;

	HashMap<StringName, ClassData> classes;
	HashSet<StringName> singletons;

public:
	static LinterDB *get_singleton() { return singleton; }

	Error load_from_json(const String &p_path);

	// Class queries.
	bool class_exists(const StringName &p_class) const;
	const ClassData *get_class_data(const StringName &p_class) const;
	StringName get_parent_class(const StringName &p_class) const;
	bool is_parent_class(const StringName &p_child, const StringName &p_parent) const;
	bool is_abstract(const StringName &p_class) const;

	// Method queries (walks inheritance).
	bool has_method(const StringName &p_class, const StringName &p_method, bool p_no_inheritance = false) const;
	const MethodData *get_method_data(const StringName &p_class, const StringName &p_method) const;
	bool get_method_info(const StringName &p_class, const StringName &p_method, MethodInfo *r_info) const;

	// Property queries (walks inheritance).
	bool has_property(const StringName &p_class, const StringName &p_property, bool p_no_inheritance = false) const;
	const PropertyData *get_property_data(const StringName &p_class, const StringName &p_property) const;

	// Signal queries (walks inheritance).
	bool has_signal(const StringName &p_class, const StringName &p_signal, bool p_no_inheritance = false) const;
	bool get_signal_info(const StringName &p_class, const StringName &p_signal, MethodInfo *r_info) const;

	// Enum queries (walks inheritance).
	bool has_enum(const StringName &p_class, const StringName &p_enum, bool p_no_inheritance = false) const;
	void get_enum_constants(const StringName &p_class, const StringName &p_enum, List<StringName> *r_constants, bool p_no_inheritance = false) const;

	// Integer constant queries (walks inheritance).
	bool has_integer_constant(const StringName &p_class, const StringName &p_constant, bool p_no_inheritance = false) const;
	int64_t get_integer_constant(const StringName &p_class, const StringName &p_constant, bool *r_valid = nullptr) const;
	StringName get_integer_constant_enum(const StringName &p_class, const StringName &p_constant, bool p_no_inheritance = false) const;

	// Singleton queries.
	bool has_singleton(const StringName &p_name) const;

	// Listing.
	void get_class_list(LocalVector<StringName> &r_classes) const;
	void get_method_list(const StringName &p_class, List<MethodInfo> *r_methods, bool p_no_inheritance = false) const;
	void get_property_list(const StringName &p_class, List<PropertyInfo> *r_properties, bool p_no_inheritance = false) const;
	void get_signal_list(const StringName &p_class, List<MethodInfo> *r_signals, bool p_no_inheritance = false) const;
	void get_enum_list(const StringName &p_class, List<StringName> *r_enums, bool p_no_inheritance = false) const;
	void get_integer_constant_list(const StringName &p_class, List<String> *r_constants, bool p_no_inheritance = false) const;

	LinterDB();
	~LinterDB();
};

} // namespace linter

#endif // HOMOT
