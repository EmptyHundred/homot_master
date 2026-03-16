/**************************************************************************/
/*  linterdb_dump.cpp                                                     */
/**************************************************************************/

#ifdef HOMOT

#include "linterdb_dump.h"

#include "core/config/engine.h"
#include "core/core_constants.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/object/method_bind.h"
#include "core/variant/variant.h"

#ifdef TOOLS_ENABLED
#include "editor/doc/doc_data_class_path.gen.h"
#include "editor/doc/doc_tools.h"
#endif

static Dictionary _property_info_to_dict(const PropertyInfo &p_info) {
	Dictionary d;
	d["type"] = (int)p_info.type;
	if (!p_info.name.is_empty()) {
		d["name"] = p_info.name;
	}
	if (!p_info.class_name.is_empty()) {
		d["class_name"] = String(p_info.class_name);
	}
	if (p_info.hint != PROPERTY_HINT_NONE) {
		d["hint"] = (int)p_info.hint;
	}
	if (!p_info.hint_string.is_empty()) {
		d["hint_string"] = p_info.hint_string;
	}
	if (p_info.usage != PROPERTY_USAGE_DEFAULT) {
		d["usage"] = (int)p_info.usage;
	}
	return d;
}

static Dictionary _method_info_to_dict(const MethodInfo &p_info) {
	Dictionary d;
	d["name"] = p_info.name;
	d["return_val"] = _property_info_to_dict(p_info.return_val);
	d["flags"] = (int)p_info.flags;
	if (p_info.return_val_metadata != 0) {
		d["return_val_metadata"] = p_info.return_val_metadata;
	}

	Array args;
	for (int i = 0; i < p_info.arguments.size(); i++) {
		args.push_back(_property_info_to_dict(p_info.arguments[i]));
	}
	d["args"] = args;

	d["default_arg_count"] = p_info.default_arguments.size();

	if (!p_info.arguments_metadata.is_empty()) {
		Array meta;
		for (int i = 0; i < p_info.arguments_metadata.size(); i++) {
			meta.push_back(p_info.arguments_metadata[i]);
		}
		d["arguments_metadata"] = meta;
	}

	return d;
}

// Convert a DocData::ArgumentDoc to a JSON dictionary.
static Dictionary _doc_argument_to_dict(const DocData::ArgumentDoc &p_arg) {
	Dictionary d;
	if (!p_arg.name.is_empty()) {
		d["name"] = p_arg.name;
	}
	if (!p_arg.type.is_empty()) {
		d["type"] = p_arg.type;
	}
	if (!p_arg.enumeration.is_empty()) {
		d["enumeration"] = p_arg.enumeration;
		d["is_bitfield"] = p_arg.is_bitfield;
	}
	if (!p_arg.default_value.is_empty()) {
		d["default_value"] = p_arg.default_value;
	}
	return d;
}

// Convert a DocData::MethodDoc to a JSON dictionary.
static Dictionary _doc_method_to_dict(const DocData::MethodDoc &p_method) {
	Dictionary d;
	d["name"] = p_method.name;
	if (!p_method.return_type.is_empty()) {
		d["return_type"] = p_method.return_type;
	}
	if (!p_method.return_enum.is_empty()) {
		d["return_enum"] = p_method.return_enum;
		d["return_is_bitfield"] = p_method.return_is_bitfield;
	}
	if (!p_method.qualifiers.is_empty()) {
		d["qualifiers"] = p_method.qualifiers;
	}
	if (!p_method.description.is_empty()) {
		d["description"] = p_method.description;
	}
	if (p_method.is_deprecated) {
		d["deprecated"] = p_method.deprecated_message;
	}
	if (p_method.is_experimental) {
		d["experimental"] = p_method.experimental_message;
	}
	if (!p_method.arguments.is_empty()) {
		Array args;
		for (int i = 0; i < p_method.arguments.size(); i++) {
			args.push_back(_doc_argument_to_dict(p_method.arguments[i]));
		}
		d["arguments"] = args;
	}
	return d;
}

// Convert a DocData::PropertyDoc to a JSON dictionary.
static Dictionary _doc_property_to_dict(const DocData::PropertyDoc &p_prop) {
	Dictionary d;
	d["name"] = p_prop.name;
	if (!p_prop.type.is_empty()) {
		d["type"] = p_prop.type;
	}
	if (!p_prop.enumeration.is_empty()) {
		d["enumeration"] = p_prop.enumeration;
		d["is_bitfield"] = p_prop.is_bitfield;
	}
	if (!p_prop.description.is_empty()) {
		d["description"] = p_prop.description;
	}
	if (!p_prop.setter.is_empty()) {
		d["setter"] = p_prop.setter;
	}
	if (!p_prop.getter.is_empty()) {
		d["getter"] = p_prop.getter;
	}
	if (!p_prop.default_value.is_empty()) {
		d["default_value"] = p_prop.default_value;
	}
	if (p_prop.overridden) {
		d["overridden"] = true;
		if (!p_prop.overrides.is_empty()) {
			d["overrides"] = p_prop.overrides;
		}
	}
	if (p_prop.is_deprecated) {
		d["deprecated"] = p_prop.deprecated_message;
	}
	if (p_prop.is_experimental) {
		d["experimental"] = p_prop.experimental_message;
	}
	return d;
}

// Convert a DocData::ConstantDoc to a JSON dictionary.
static Dictionary _doc_constant_to_dict(const DocData::ConstantDoc &p_const) {
	Dictionary d;
	d["name"] = p_const.name;
	if (!p_const.value.is_empty()) {
		d["value"] = p_const.value;
	}
	d["is_value_valid"] = p_const.is_value_valid;
	if (!p_const.type.is_empty()) {
		d["type"] = p_const.type;
	}
	if (!p_const.enumeration.is_empty()) {
		d["enumeration"] = p_const.enumeration;
		d["is_bitfield"] = p_const.is_bitfield;
	}
	if (!p_const.description.is_empty()) {
		d["description"] = p_const.description;
	}
	if (p_const.is_deprecated) {
		d["deprecated"] = p_const.deprecated_message;
	}
	if (p_const.is_experimental) {
		d["experimental"] = p_const.experimental_message;
	}
	return d;
}

// Convert a DocData::ThemeItemDoc to a JSON dictionary.
static Dictionary _doc_theme_item_to_dict(const DocData::ThemeItemDoc &p_item) {
	Dictionary d;
	d["name"] = p_item.name;
	if (!p_item.type.is_empty()) {
		d["type"] = p_item.type;
	}
	if (!p_item.data_type.is_empty()) {
		d["data_type"] = p_item.data_type;
	}
	if (!p_item.description.is_empty()) {
		d["description"] = p_item.description;
	}
	if (!p_item.default_value.is_empty()) {
		d["default_value"] = p_item.default_value;
	}
	if (p_item.is_deprecated) {
		d["deprecated"] = p_item.deprecated_message;
	}
	if (p_item.is_experimental) {
		d["experimental"] = p_item.experimental_message;
	}
	return d;
}

// Convert a DocData::EnumDoc to a JSON dictionary.
static Dictionary _doc_enum_to_dict(const DocData::EnumDoc &p_enum) {
	Dictionary d;
	if (!p_enum.description.is_empty()) {
		d["description"] = p_enum.description;
	}
	if (p_enum.is_deprecated) {
		d["deprecated"] = p_enum.deprecated_message;
	}
	if (p_enum.is_experimental) {
		d["experimental"] = p_enum.experimental_message;
	}
	return d;
}

// Convert a DocData::TutorialDoc to a JSON dictionary.
static Dictionary _doc_tutorial_to_dict(const DocData::TutorialDoc &p_tutorial) {
	Dictionary d;
	if (!p_tutorial.link.is_empty()) {
		d["link"] = p_tutorial.link;
	}
	if (!p_tutorial.title.is_empty()) {
		d["title"] = p_tutorial.title;
	}
	return d;
}

// Add a complete "doc" dictionary for a class from its DocData::ClassDoc.
static Dictionary _class_doc_to_dict(const DocData::ClassDoc &p_class_doc) {
	Dictionary doc;

	if (!p_class_doc.brief_description.is_empty()) {
		doc["brief_description"] = p_class_doc.brief_description;
	}
	if (!p_class_doc.description.is_empty()) {
		doc["description"] = p_class_doc.description;
	}
	if (!p_class_doc.keywords.is_empty()) {
		doc["keywords"] = p_class_doc.keywords;
	}
	if (p_class_doc.is_deprecated) {
		doc["deprecated"] = p_class_doc.deprecated_message;
	}
	if (p_class_doc.is_experimental) {
		doc["experimental"] = p_class_doc.experimental_message;
	}

	// Tutorials.
	if (!p_class_doc.tutorials.is_empty()) {
		Array tutorials;
		for (int i = 0; i < p_class_doc.tutorials.size(); i++) {
			tutorials.push_back(_doc_tutorial_to_dict(p_class_doc.tutorials[i]));
		}
		doc["tutorials"] = tutorials;
	}

	// Methods.
	if (!p_class_doc.methods.is_empty()) {
		Array methods;
		for (int i = 0; i < p_class_doc.methods.size(); i++) {
			methods.push_back(_doc_method_to_dict(p_class_doc.methods[i]));
		}
		doc["methods"] = methods;
	}

	// Constructors.
	if (!p_class_doc.constructors.is_empty()) {
		Array constructors;
		for (int i = 0; i < p_class_doc.constructors.size(); i++) {
			constructors.push_back(_doc_method_to_dict(p_class_doc.constructors[i]));
		}
		doc["constructors"] = constructors;
	}

	// Operators.
	if (!p_class_doc.operators.is_empty()) {
		Array operators;
		for (int i = 0; i < p_class_doc.operators.size(); i++) {
			operators.push_back(_doc_method_to_dict(p_class_doc.operators[i]));
		}
		doc["operators"] = operators;
	}

	// Signals.
	if (!p_class_doc.signals.is_empty()) {
		Array signals;
		for (int i = 0; i < p_class_doc.signals.size(); i++) {
			signals.push_back(_doc_method_to_dict(p_class_doc.signals[i]));
		}
		doc["signals"] = signals;
	}

	// Properties.
	if (!p_class_doc.properties.is_empty()) {
		Array properties;
		for (int i = 0; i < p_class_doc.properties.size(); i++) {
			properties.push_back(_doc_property_to_dict(p_class_doc.properties[i]));
		}
		doc["properties"] = properties;
	}

	// Constants.
	if (!p_class_doc.constants.is_empty()) {
		Array constants;
		for (int i = 0; i < p_class_doc.constants.size(); i++) {
			constants.push_back(_doc_constant_to_dict(p_class_doc.constants[i]));
		}
		doc["constants"] = constants;
	}

	// Enums (with descriptions).
	if (!p_class_doc.enums.is_empty()) {
		Dictionary enums;
		for (const KeyValue<String, DocData::EnumDoc> &E : p_class_doc.enums) {
			enums[E.key] = _doc_enum_to_dict(E.value);
		}
		doc["enums"] = enums;
	}

	// Annotations.
	if (!p_class_doc.annotations.is_empty()) {
		Array annotations;
		for (int i = 0; i < p_class_doc.annotations.size(); i++) {
			annotations.push_back(_doc_method_to_dict(p_class_doc.annotations[i]));
		}
		doc["annotations"] = annotations;
	}

	// Theme properties.
	if (!p_class_doc.theme_properties.is_empty()) {
		Array theme_properties;
		for (int i = 0; i < p_class_doc.theme_properties.size(); i++) {
			theme_properties.push_back(_doc_theme_item_to_dict(p_class_doc.theme_properties[i]));
		}
		doc["theme_properties"] = theme_properties;
	}

	return doc;
}

#ifdef TOOLS_ENABLED
// Load documentation from Godot source XML files.
// p_source_root should be the Godot source root (containing doc/classes/).
static bool _load_doc_data(const String &p_source_root, DocTools &r_doc) {
	// Generate API stubs from ClassDB.
	r_doc.generate();
	print_line(vformat("Generated doc stubs for %d classes.", r_doc.class_list.size()));

	// Load XML doc descriptions from all known paths and merge.
	DocTools docsrc;
	HashSet<String> loaded_paths;

	// Load from module/platform doc_classes paths.
	for (int i = 0; i < _doc_data_class_path_count; i++) {
		String path = _doc_data_class_paths[i].path;
		if (path.is_relative_path()) {
			path = p_source_root.path_join(path);
		}
		if (!loaded_paths.has(path)) {
			loaded_paths.insert(path);
			Error err = docsrc.load_classes(path);
			if (err != OK) {
				print_line(vformat("  Warning: Failed to load docs from: %s", path));
			}
		}
	}

	// Load from the main doc/classes/ directory.
	String main_doc_path = p_source_root.path_join("doc/classes");
	print_line(vformat("Loading main docs from: %s", main_doc_path));
	if (!loaded_paths.has(main_doc_path)) {
		loaded_paths.insert(main_doc_path);
		Error err = docsrc.load_classes(main_doc_path);
		if (err != OK) {
			print_line(vformat("  Warning: Failed to load docs from: %s", main_doc_path));
		}
	}

	print_line(vformat("Loaded %d doc classes from XML.", docsrc.class_list.size()));
	r_doc.merge_from(docsrc);

	// Verify merge worked.
	if (r_doc.class_list.has("Node")) {
		const DocData::ClassDoc &node_doc = r_doc.class_list["Node"];
		print_line(vformat("  Node brief_description length: %d", node_doc.brief_description.length()));
		int desc_count = 0;
		for (int i = 0; i < node_doc.methods.size(); i++) {
			if (!node_doc.methods[i].description.is_empty()) {
				desc_count++;
			}
		}
		print_line(vformat("  Node methods with descriptions: %d/%d", desc_count, node_doc.methods.size()));
	}

	return docsrc.class_list.size() > 0;
}
#endif // TOOLS_ENABLED

Error LinterDBDump::generate_linterdb_json_file(const String &p_path, const String &p_doc_source_path) {
	// Optionally load documentation.
#ifdef TOOLS_ENABLED
	DocTools doc;
	bool has_docs = false;
	if (!p_doc_source_path.is_empty()) {
		has_docs = _load_doc_data(p_doc_source_path, doc);
		if (has_docs) {
			print_line(vformat("Loaded documentation for %d classes.", doc.class_list.size()));
		}
	}
#else
	bool has_docs = false;
#endif

	Dictionary root;

	// Dump engine singletons.
	{
		Array singletons;
		List<Engine::Singleton> singleton_list;
		Engine::get_singleton()->get_singletons(&singleton_list);
		for (const Engine::Singleton &s : singleton_list) {
			singletons.push_back(String(s.name));
		}
		root["singletons"] = singletons;
	}

	// Dump ClassDB.
	Dictionary classes_dict;
	{
		LocalVector<StringName> class_list;
		ClassDB::get_class_list(class_list);

		for (const StringName &class_name : class_list) {
			if (!ClassDB::is_class_exposed(class_name)) {
				continue;
			}

			Dictionary cls;

			// Parent class.
			StringName parent = ClassDB::get_parent_class(class_name);
			cls["parent"] = String(parent);

			// Abstract flag.
			cls["is_abstract"] = ClassDB::is_abstract(class_name);

			// Methods.
			{
				Array methods_arr;
				List<MethodInfo> methods;
				ClassDB::get_method_list(class_name, &methods, true, false);
				for (const MethodInfo &mi : methods) {
					Dictionary md = _method_info_to_dict(mi);

					// Get extra MethodBind info (is_vararg, is_static, instance_class).
					MethodBind *mb = ClassDB::get_method(class_name, mi.name);
					if (mb) {
						md["is_vararg"] = mb->is_vararg();
						md["is_static"] = mb->is_static();
						md["instance_class"] = String(mb->get_instance_class());
					}

					methods_arr.push_back(md);
				}
				cls["methods"] = methods_arr;
			}

			// Properties.
			{
				Array props_arr;
				List<PropertyInfo> props;
				ClassDB::get_property_list(class_name, &props, true);
				for (const PropertyInfo &pi : props) {
					Dictionary pd = _property_info_to_dict(pi);
					pd["getter"] = String(ClassDB::get_property_getter(class_name, pi.name));
					pd["setter"] = String(ClassDB::get_property_setter(class_name, pi.name));
					props_arr.push_back(pd);
				}
				cls["properties"] = props_arr;
			}

			// Signals.
			{
				Array signals_arr;
				List<MethodInfo> signals;
				ClassDB::get_signal_list(class_name, &signals, true);
				for (const MethodInfo &si : signals) {
					signals_arr.push_back(_method_info_to_dict(si));
				}
				cls["signals"] = signals_arr;
			}

			// Enums.
			{
				Dictionary enums_dict;
				List<StringName> enums;
				ClassDB::get_enum_list(class_name, &enums, true);
				for (const StringName &enum_name : enums) {
					Dictionary enum_values;
					List<StringName> constants;
					ClassDB::get_enum_constants(class_name, enum_name, &constants, true);
					for (const StringName &c : constants) {
						enum_values[String(c)] = ClassDB::get_integer_constant(class_name, c);
					}
					enums_dict[String(enum_name)] = enum_values;
				}
				cls["enums"] = enums_dict;
			}

			// Integer constants (not part of any enum).
			{
				Dictionary constants_dict;
				List<String> constants;
				ClassDB::get_integer_constant_list(class_name, &constants, true);
				for (const String &c : constants) {
					StringName enum_name = ClassDB::get_integer_constant_enum(class_name, c, true);
					if (!enum_name.is_empty()) {
						continue; // Already covered in enums section.
					}
					constants_dict[c] = ClassDB::get_integer_constant(class_name, c);
				}
				if (!constants_dict.is_empty()) {
					cls["constants"] = constants_dict;
				}
			}

			// Documentation from XML docs.
#ifdef TOOLS_ENABLED
			if (has_docs) {
				HashMap<String, DocData::ClassDoc>::Iterator it = doc.class_list.find(String(class_name));
				if (it) {
					cls["doc"] = _class_doc_to_dict(it->value);
				}
			}
#endif

			classes_dict[String(class_name)] = cls;
		}
	}
	root["classes"] = classes_dict;

	// Dump Variant built-in type info for completeness.
	{
		Dictionary builtins;
		for (int i = 0; i < Variant::VARIANT_MAX; i++) {
			Variant::Type vt = (Variant::Type)i;
			String type_name = Variant::get_type_name(vt);

			Dictionary type_dict;

			// Built-in methods.
			{
				Array methods_arr;
				List<StringName> method_names;
				Variant::get_builtin_method_list(vt, &method_names);
				for (const StringName &method_name : method_names) {
					MethodInfo mi = Variant::get_builtin_method_info(vt, method_name);
					Dictionary md = _method_info_to_dict(mi);
					md["is_vararg"] = Variant::is_builtin_method_vararg(vt, method_name);
					md["is_static"] = Variant::is_builtin_method_static(vt, method_name);
					methods_arr.push_back(md);
				}
				if (!methods_arr.is_empty()) {
					type_dict["methods"] = methods_arr;
				}
			}

			// Built-in properties (members).
			{
				Array props_arr;
				List<StringName> members;
				Variant::get_member_list(vt, &members);
				for (const StringName &member : members) {
					Dictionary pd;
					pd["name"] = String(member);
					pd["type"] = (int)Variant::get_member_type(vt, member);
					props_arr.push_back(pd);
				}
				if (!props_arr.is_empty()) {
					type_dict["members"] = props_arr;
				}
			}

			// Constants.
			{
				Dictionary consts;
				List<StringName> constant_names;
				Variant::get_constants_for_type(vt, &constant_names);
				for (const StringName &cn : constant_names) {
					Variant val = Variant::get_constant_value(vt, cn);
					// Store as string representation since values can be various types.
					consts[String(cn)] = val.get_construct_string();
				}
				if (!consts.is_empty()) {
					type_dict["constants"] = consts;
				}
			}

			// Enums.
			{
				Dictionary enums;
				List<StringName> enum_names;
				Variant::get_enums_for_type(vt, &enum_names);
				for (const StringName &en : enum_names) {
					Dictionary enum_values;
					List<StringName> enum_constants;
					Variant::get_enumerations_for_enum(vt, en, &enum_constants);
					for (const StringName &ec : enum_constants) {
						enum_values[String(ec)] = Variant::get_enum_value(vt, en, ec);
					}
					enums[String(en)] = enum_values;
				}
				if (!enums.is_empty()) {
					type_dict["enums"] = enums;
				}
			}

			// Constructors.
			{
				Array ctors_arr;
				for (int j = 0; j < Variant::get_constructor_count(vt); j++) {
					Array args;
					for (int k = 0; k < Variant::get_constructor_argument_count(vt, j); k++) {
						Dictionary arg;
						arg["name"] = Variant::get_constructor_argument_name(vt, j, k);
						arg["type"] = (int)Variant::get_constructor_argument_type(vt, j, k);
						args.push_back(arg);
					}
					ctors_arr.push_back(args);
				}
				if (!ctors_arr.is_empty()) {
					type_dict["constructors"] = ctors_arr;
				}
			}

			// Operators.
			{
				Array ops_arr;
				for (int j = 0; j < Variant::OP_MAX; j++) {
					Variant::Operator op = (Variant::Operator)j;
					for (int k = 0; k < Variant::VARIANT_MAX; k++) {
						Variant::Type right_type = (Variant::Type)k;
						Variant::ValidatedOperatorEvaluator eval = Variant::get_validated_operator_evaluator(op, vt, right_type);
						if (eval) {
							Dictionary od;
							od["operator"] = j;
							od["right_type"] = k;
							od["return_type"] = (int)Variant::get_operator_return_type(op, vt, right_type);
							ops_arr.push_back(od);
						}
					}
				}
				if (!ops_arr.is_empty()) {
					type_dict["operators"] = ops_arr;
				}
			}

			// Documentation for built-in types.
#ifdef TOOLS_ENABLED
			if (has_docs) {
				HashMap<String, DocData::ClassDoc>::Iterator it = doc.class_list.find(type_name);
				if (it) {
					type_dict["doc"] = _class_doc_to_dict(it->value);
				}
			}
#endif

			if (!type_dict.is_empty()) {
				builtins[type_name] = type_dict;
			}
		}
		root["builtin_types"] = builtins;
	}

	// Dump utility functions (global scope functions).
	{
		Array utility_arr;
		List<StringName> utility_functions;
		Variant::get_utility_function_list(&utility_functions);
		for (const StringName &fn : utility_functions) {
			Dictionary fd;
			fd["name"] = String(fn);
			fd["return_type"] = (int)Variant::get_utility_function_return_type(fn);
			fd["is_vararg"] = Variant::is_utility_function_vararg(fn);
			fd["type"] = (int)Variant::get_utility_function_type(fn); // MATH, RANDOM, GENERAL

			if (!Variant::is_utility_function_vararg(fn)) {
				Array args;
				for (int i = 0; i < Variant::get_utility_function_argument_count(fn); i++) {
					Dictionary arg;
					arg["name"] = Variant::get_utility_function_argument_name(fn, i);
					arg["type"] = (int)Variant::get_utility_function_argument_type(fn, i);
					args.push_back(arg);
				}
				fd["args"] = args;
			}

			// Documentation for utility functions.
#ifdef TOOLS_ENABLED
			if (has_docs) {
				HashMap<String, DocData::ClassDoc>::Iterator it = doc.class_list.find("@GlobalScope");
				if (it) {
					for (int i = 0; i < it->value.methods.size(); i++) {
						if (it->value.methods[i].name == String(fn)) {
							if (!it->value.methods[i].description.is_empty()) {
								fd["description"] = it->value.methods[i].description;
							}
							break;
						}
					}
				}
			}
#endif

			utility_arr.push_back(fd);
		}
		root["utility_functions"] = utility_arr;
	}

	// Dump global enums and constants.
	{
		Dictionary global_enums;
		Dictionary global_constants;

		for (int i = 0; i < CoreConstants::get_global_constant_count(); i++) {
			StringName enum_name = CoreConstants::get_global_constant_enum(i);
			String const_name = CoreConstants::get_global_constant_name(i);
			int64_t const_value = CoreConstants::get_global_constant_value(i);

			if (!enum_name.is_empty()) {
				if (!global_enums.has(String(enum_name))) {
					global_enums[String(enum_name)] = Dictionary();
				}
				Dictionary enum_values = global_enums[String(enum_name)];
				enum_values[const_name] = const_value;
				global_enums[String(enum_name)] = enum_values;
			} else {
				global_constants[const_name] = const_value;
			}
		}
		root["global_enums"] = global_enums;
		root["global_constants"] = global_constants;
	}

	// Dump documentation for classes only found in docs (e.g. @GlobalScope, @GDScript).
#ifdef TOOLS_ENABLED
	if (has_docs) {
		Dictionary doc_only_classes;
		for (const KeyValue<String, DocData::ClassDoc> &E : doc.class_list) {
			if (E.key.begins_with("@")) {
				doc_only_classes[E.key] = _class_doc_to_dict(E.value);
			}
		}
		if (!doc_only_classes.is_empty()) {
			root["doc_classes"] = doc_only_classes;
		}
	}
#endif

	// Write to file.
	String json_text = JSON::stringify(root, "\t");

	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_CREATE, vformat("Cannot open file '%s' for writing.", p_path));

	f->store_string(json_text);
	return OK;
}

#endif // HOMOT
