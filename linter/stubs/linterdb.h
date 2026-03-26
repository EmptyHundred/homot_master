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

// Documentation for a method argument (from XML docs).
struct DocArgumentData {
	String name;
	String type;
	String enumeration;
	bool is_bitfield = false;
	String default_value;
};

// Documentation for a method (from XML docs).
struct DocMethodData {
	String name;
	String return_type;
	String return_enum;
	String qualifiers;
	String description;
	bool is_deprecated = false;
	String deprecated_message;
	bool is_experimental = false;
	String experimental_message;
	Vector<DocArgumentData> arguments;
};

// Documentation for a property (from XML docs).
struct DocPropertyData {
	String name;
	String type;
	String enumeration;
	String description;
	String setter;
	String getter;
	String default_value;
	bool is_deprecated = false;
	String deprecated_message;
	bool is_experimental = false;
	String experimental_message;
};

// Documentation for a constant (from XML docs).
struct DocConstantData {
	String name;
	String value;
	String type;
	String enumeration;
	String description;
	bool is_deprecated = false;
	String deprecated_message;
	bool is_experimental = false;
	String experimental_message;
};

// Documentation for an enum (from XML docs).
struct DocEnumData {
	String description;
	bool is_deprecated = false;
	String deprecated_message;
	bool is_experimental = false;
	String experimental_message;
};

// Documentation for a theme item (from XML docs).
struct DocThemeItemData {
	String name;
	String type;
	String data_type;
	String description;
	String default_value;
};

// Documentation for a tutorial link.
struct DocTutorialData {
	String title;
	String link;
};

// Full documentation for a class (from XML docs).
struct DocClassData {
	String brief_description;
	String description;
	String keywords;
	bool is_deprecated = false;
	String deprecated_message;
	bool is_experimental = false;
	String experimental_message;

	Vector<DocTutorialData> tutorials;
	Vector<DocMethodData> methods;
	Vector<DocMethodData> constructors;
	Vector<DocMethodData> operators;
	Vector<DocMethodData> signals;
	Vector<DocPropertyData> properties;
	Vector<DocConstantData> constants;
	HashMap<String, DocEnumData> enums;
	Vector<DocMethodData> annotations;
	Vector<DocThemeItemData> theme_properties;

	// Quick lookup helpers.
	const DocMethodData *find_method(const String &p_name) const;
	const DocMethodData *find_signal(const String &p_name) const;
	const DocPropertyData *find_property(const String &p_name) const;
	const DocConstantData *find_constant(const String &p_name) const;
};

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

	// Documentation from XML docs (may be empty if docs not loaded).
	DocClassData doc;
};

// Utility function metadata loaded from JSON.
struct UtilityFunctionData {
	MethodInfo info;
	bool is_vararg = false;
};

// Singleton data store. Loaded once at startup.
class LinterDB {
	static LinterDB *singleton;

	HashMap<StringName, ClassData> classes;
	HashSet<StringName> singletons;

	// Utility functions (print, lerp, etc.) loaded from JSON.
	HashMap<StringName, UtilityFunctionData> utility_functions;

	// Global enums (Error, PropertyHint, etc.) — enum_name -> { const_name -> value }.
	HashMap<StringName, HashMap<StringName, int64_t>> global_enums;

	// Global constants not part of any enum.
	HashMap<StringName, int64_t> global_constants;

	// Documentation for built-in Variant types (int, float, Vector2, etc.).
	HashMap<String, DocClassData> builtin_type_docs;

	// Documentation for special doc classes (@GlobalScope, @GDScript, etc.).
	HashMap<String, DocClassData> doc_classes;

public:
	static LinterDB *get_singleton() { return singleton; }

	Error load_from_json(const String &p_path);
	Error load_from_compressed(const uint8_t *p_data, uint32_t p_compressed_size, uint32_t p_uncompressed_size);

	// Merge additional classes (e.g. from a project classdb export) into the database.
	// The dictionary should have the same format as the "classes" key in linterdb.json.
	// Also accepts optional "singletons" array.
	Error load_additional_classes(const Dictionary &p_root);

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
	void add_singleton(const StringName &p_name);

	// Documentation queries.
	const DocClassData *get_class_doc(const StringName &p_class) const;
	const DocClassData *get_builtin_type_doc(const String &p_type) const;
	const DocMethodData *get_utility_function_doc(const StringName &p_name) const;
	// Walks inheritance to find method doc.
	const DocMethodData *get_method_doc(const StringName &p_class, const StringName &p_method) const;
	const DocPropertyData *get_property_doc(const StringName &p_class, const StringName &p_property) const;
	const DocMethodData *get_signal_doc(const StringName &p_class, const StringName &p_signal) const;
	const DocConstantData *get_constant_doc(const StringName &p_class, const StringName &p_constant) const;

	// Listing.
	void get_class_list(LocalVector<StringName> &r_classes) const;
	void get_method_list(const StringName &p_class, List<MethodInfo> *r_methods, bool p_no_inheritance = false) const;
	void get_property_list(const StringName &p_class, List<PropertyInfo> *r_properties, bool p_no_inheritance = false) const;
	void get_signal_list(const StringName &p_class, List<MethodInfo> *r_signals, bool p_no_inheritance = false) const;
	void get_enum_list(const StringName &p_class, List<StringName> *r_enums, bool p_no_inheritance = false) const;
	void get_integer_constant_list(const StringName &p_class, List<String> *r_constants, bool p_no_inheritance = false) const;

	// Singleton listing.
	void get_singleton_list(LocalVector<StringName> &r_singletons) const;

	// Built-in type listing.
	void get_builtin_type_list(LocalVector<String> &r_types) const;

	// Utility function queries.
	bool has_utility_function(const StringName &p_name) const;
	const UtilityFunctionData *get_utility_function(const StringName &p_name) const;
	void get_utility_function_list(LocalVector<StringName> &r_functions) const;

	// Global enum queries.
	void get_global_enum_list(LocalVector<StringName> &r_enums) const;
	bool has_global_enum(const StringName &p_enum) const;
	void get_global_enum_constants(const StringName &p_enum, HashMap<StringName, int64_t> &r_constants) const;

	// Global constant queries.
	void get_global_constant_list(LocalVector<StringName> &r_constants) const;
	int64_t get_global_constant(const StringName &p_name, bool *r_valid = nullptr) const;

	// Doc class queries (for @GlobalScope etc.).
	const DocClassData *get_doc_class(const String &p_name) const;

	LinterDB();
	~LinterDB();

private:
	Error _load_from_dict(const Dictionary &p_root);
};

} // namespace linter

#endif // HOMOT
