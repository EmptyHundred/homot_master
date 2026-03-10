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

Error LinterDBDump::generate_linterdb_json_file(const String &p_path) {
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

	// Write to file.
	String json_text = JSON::stringify(root, "\t");

	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_CREATE, vformat("Cannot open file '%s' for writing.", p_path));

	f->store_string(json_text);
	return OK;
}

#endif // HOMOT
