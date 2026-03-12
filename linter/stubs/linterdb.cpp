/**************************************************************************/
/*  linterdb.cpp                                                          */
/**************************************************************************/

#ifdef HOMOT

#include "linterdb.h"

#include "core/io/file_access.h"
#include "core/io/json.h"

namespace linter {

LinterDB *LinterDB::singleton = nullptr;

LinterDB::LinterDB() {
	singleton = this;
}

LinterDB::~LinterDB() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

// Helper: parse PropertyInfo from a JSON Dictionary.
static PropertyInfo _parse_property_info(const Dictionary &d) {
	PropertyInfo pi;
	pi.type = (Variant::Type)(int)d.get("type", 0);
	pi.name = d.get("name", "");
	if (d.has("class_name")) {
		pi.class_name = StringName(String(d["class_name"]));
	}
	pi.hint = (PropertyHint)(int)d.get("hint", 0);
	pi.hint_string = d.get("hint_string", "");
	pi.usage = (uint32_t)(int)d.get("usage", PROPERTY_USAGE_DEFAULT);
	return pi;
}

// Helper: parse MethodInfo from a JSON Dictionary.
static MethodInfo _parse_method_info(const Dictionary &d) {
	MethodInfo mi;
	mi.name = d.get("name", "");
	if (d.has("return_val")) {
		mi.return_val = _parse_property_info(d["return_val"]);
	}
	mi.flags = (uint32_t)(int)d.get("flags", METHOD_FLAGS_DEFAULT);
	mi.return_val_metadata = d.get("return_val_metadata", 0);

	Array args = d.get("args", Array());
	for (int i = 0; i < args.size(); i++) {
		mi.arguments.push_back(_parse_property_info(args[i]));
	}

	// Reconstruct default_arguments count (we only stored the count).
	int default_arg_count = d.get("default_arg_count", 0);
	for (int i = 0; i < default_arg_count; i++) {
		mi.default_arguments.push_back(Variant());
	}

	if (d.has("arguments_metadata")) {
		Array meta = d["arguments_metadata"];
		for (int i = 0; i < meta.size(); i++) {
			mi.arguments_metadata.push_back(meta[i]);
		}
	}

	return mi;
}

Error LinterDB::load_from_json(const String &p_path) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_FILE_NOT_FOUND, vformat("Cannot open linterdb file: %s", p_path));

	String json_text = f->get_as_text();
	f.unref();

	JSON json;
	Error err = json.parse(json_text);
	ERR_FAIL_COND_V_MSG(err != OK, err, vformat("Failed to parse linterdb JSON: %s", json.get_error_message()));

	Dictionary root = json.get_data();

	// Load singletons.
	{
		Array singleton_arr = root.get("singletons", Array());
		for (int i = 0; i < singleton_arr.size(); i++) {
			singletons.insert(StringName(String(singleton_arr[i])));
		}
	}

	// Load classes.
	{
		Dictionary classes_dict = root.get("classes", Dictionary());
		LocalVector<Variant> class_names = classes_dict.get_key_list();

		for (const Variant &key : class_names) {
			String class_name_str = key;
			Dictionary cls = classes_dict[key];

			ClassData cd;
			cd.name = StringName(class_name_str);
			cd.parent = StringName(String(cls.get("parent", "")));
			cd.is_abstract = cls.get("is_abstract", false);

			// Methods.
			{
				Array methods_arr = cls.get("methods", Array());
				for (int i = 0; i < methods_arr.size(); i++) {
					Dictionary md = methods_arr[i];
					MethodData method;
					method.info = _parse_method_info(md);
					method.is_vararg = md.get("is_vararg", false);
					method.is_static = md.get("is_static", false);
					method.instance_class = StringName(String(md.get("instance_class", class_name_str)));
					cd.methods[StringName(method.info.name)] = method;
				}
			}

			// Properties.
			{
				Array props_arr = cls.get("properties", Array());
				for (int i = 0; i < props_arr.size(); i++) {
					Dictionary pd = props_arr[i];
					PropertyData prop;
					prop.info = _parse_property_info(pd);
					prop.getter = StringName(String(pd.get("getter", "")));
					prop.setter = StringName(String(pd.get("setter", "")));
					cd.properties[StringName(prop.info.name)] = prop;
				}
			}

			// Signals.
			{
				Array signals_arr = cls.get("signals", Array());
				for (int i = 0; i < signals_arr.size(); i++) {
					MethodInfo si = _parse_method_info(signals_arr[i]);
					cd.signals[StringName(si.name)] = si;
				}
			}

			// Enums.
			{
				Dictionary enums_dict = cls.get("enums", Dictionary());
				LocalVector<Variant> enum_keys = enums_dict.get_key_list();

				for (const Variant &ek : enum_keys) {
					StringName enum_name = StringName(String(ek));
					Dictionary values = enums_dict[ek];
					HashMap<StringName, int64_t> enum_values;

					LocalVector<Variant> value_keys = values.get_key_list();
					for (const Variant &vk : value_keys) {
						StringName const_name = StringName(String(vk));
						int64_t val = values[vk];
						enum_values[const_name] = val;
						cd.constant_to_enum[const_name] = enum_name;
					}
					cd.enums[enum_name] = enum_values;
				}
			}

			// Constants (not part of any enum).
			{
				Dictionary consts = cls.get("constants", Dictionary());
				LocalVector<Variant> const_keys = consts.get_key_list();
				for (const Variant &ck : const_keys) {
					cd.constants[StringName(String(ck))] = (int64_t)consts[ck];
				}
			}

			classes[cd.name] = cd;
		}
	}

	return OK;
}

// --- Class queries ---

bool LinterDB::class_exists(const StringName &p_class) const {
	return classes.has(p_class);
}

const ClassData *LinterDB::get_class_data(const StringName &p_class) const {
	auto it = classes.find(p_class);
	return it ? &it->value : nullptr;
}

StringName LinterDB::get_parent_class(const StringName &p_class) const {
	const ClassData *cd = get_class_data(p_class);
	return cd ? cd->parent : StringName();
}

bool LinterDB::is_parent_class(const StringName &p_child, const StringName &p_parent) const {
	if (p_child == p_parent) {
		return true;
	}
	StringName current = p_child;
	while (current != StringName()) {
		if (current == p_parent) {
			return true;
		}
		current = get_parent_class(current);
	}
	return false;
}

bool LinterDB::is_abstract(const StringName &p_class) const {
	const ClassData *cd = get_class_data(p_class);
	return cd ? cd->is_abstract : false;
}

// --- Method queries ---

bool LinterDB::has_method(const StringName &p_class, const StringName &p_method, bool p_no_inheritance) const {
	StringName current = p_class;
	while (current != StringName()) {
		const ClassData *cd = get_class_data(current);
		if (!cd) {
			break;
		}
		if (cd->methods.has(p_method)) {
			return true;
		}
		if (p_no_inheritance) {
			break;
		}
		current = cd->parent;
	}
	return false;
}

const MethodData *LinterDB::get_method_data(const StringName &p_class, const StringName &p_method) const {
	StringName current = p_class;
	while (current != StringName()) {
		const ClassData *cd = get_class_data(current);
		if (!cd) {
			break;
		}
		auto it = cd->methods.find(p_method);
		if (it) {
			return &it->value;
		}
		current = cd->parent;
	}
	return nullptr;
}

bool LinterDB::get_method_info(const StringName &p_class, const StringName &p_method, MethodInfo *r_info) const {
	const MethodData *md = get_method_data(p_class, p_method);
	if (md && r_info) {
		*r_info = md->info;
		return true;
	}
	return md != nullptr;
}

// --- Property queries ---

bool LinterDB::has_property(const StringName &p_class, const StringName &p_property, bool p_no_inheritance) const {
	StringName current = p_class;
	while (current != StringName()) {
		const ClassData *cd = get_class_data(current);
		if (!cd) {
			break;
		}
		if (cd->properties.has(p_property)) {
			return true;
		}
		if (p_no_inheritance) {
			break;
		}
		current = cd->parent;
	}
	return false;
}

const PropertyData *LinterDB::get_property_data(const StringName &p_class, const StringName &p_property) const {
	StringName current = p_class;
	while (current != StringName()) {
		const ClassData *cd = get_class_data(current);
		if (!cd) {
			break;
		}
		auto it = cd->properties.find(p_property);
		if (it) {
			return &it->value;
		}
		current = cd->parent;
	}
	return nullptr;
}

// --- Signal queries ---

bool LinterDB::has_signal(const StringName &p_class, const StringName &p_signal, bool p_no_inheritance) const {
	StringName current = p_class;
	while (current != StringName()) {
		const ClassData *cd = get_class_data(current);
		if (!cd) {
			break;
		}
		if (cd->signals.has(p_signal)) {
			return true;
		}
		if (p_no_inheritance) {
			break;
		}
		current = cd->parent;
	}
	return false;
}

bool LinterDB::get_signal_info(const StringName &p_class, const StringName &p_signal, MethodInfo *r_info) const {
	StringName current = p_class;
	while (current != StringName()) {
		const ClassData *cd = get_class_data(current);
		if (!cd) {
			break;
		}
		auto it = cd->signals.find(p_signal);
		if (it) {
			if (r_info) {
				*r_info = it->value;
			}
			return true;
		}
		current = cd->parent;
	}
	return false;
}

// --- Enum queries ---

bool LinterDB::has_enum(const StringName &p_class, const StringName &p_enum, bool p_no_inheritance) const {
	StringName current = p_class;
	while (current != StringName()) {
		const ClassData *cd = get_class_data(current);
		if (!cd) {
			break;
		}
		if (cd->enums.has(p_enum)) {
			return true;
		}
		if (p_no_inheritance) {
			break;
		}
		current = cd->parent;
	}
	return false;
}

void LinterDB::get_enum_constants(const StringName &p_class, const StringName &p_enum, List<StringName> *r_constants, bool p_no_inheritance) const {
	StringName current = p_class;
	while (current != StringName()) {
		const ClassData *cd = get_class_data(current);
		if (!cd) {
			break;
		}
		auto it = cd->enums.find(p_enum);
		if (it) {
			for (const KeyValue<StringName, int64_t> &kv : it->value) {
				r_constants->push_back(kv.key);
			}
			return;
		}
		if (p_no_inheritance) {
			break;
		}
		current = cd->parent;
	}
}

// --- Integer constant queries ---

bool LinterDB::has_integer_constant(const StringName &p_class, const StringName &p_constant, bool p_no_inheritance) const {
	StringName current = p_class;
	while (current != StringName()) {
		const ClassData *cd = get_class_data(current);
		if (!cd) {
			break;
		}
		if (cd->constants.has(p_constant) || cd->constant_to_enum.has(p_constant)) {
			return true;
		}
		if (p_no_inheritance) {
			break;
		}
		current = cd->parent;
	}
	return false;
}

int64_t LinterDB::get_integer_constant(const StringName &p_class, const StringName &p_constant, bool *r_valid) const {
	StringName current = p_class;
	while (current != StringName()) {
		const ClassData *cd = get_class_data(current);
		if (!cd) {
			break;
		}
		// Check standalone constants.
		auto it = cd->constants.find(p_constant);
		if (it) {
			if (r_valid) {
				*r_valid = true;
			}
			return it->value;
		}
		// Check enum constants.
		auto eit = cd->constant_to_enum.find(p_constant);
		if (eit) {
			auto enum_it = cd->enums.find(eit->value);
			if (enum_it) {
				auto val_it = enum_it->value.find(p_constant);
				if (val_it) {
					if (r_valid) {
						*r_valid = true;
					}
					return val_it->value;
				}
			}
		}
		current = cd->parent;
	}
	if (r_valid) {
		*r_valid = false;
	}
	return 0;
}

StringName LinterDB::get_integer_constant_enum(const StringName &p_class, const StringName &p_constant, bool p_no_inheritance) const {
	StringName current = p_class;
	while (current != StringName()) {
		const ClassData *cd = get_class_data(current);
		if (!cd) {
			break;
		}
		auto it = cd->constant_to_enum.find(p_constant);
		if (it) {
			return it->value;
		}
		if (p_no_inheritance) {
			break;
		}
		current = cd->parent;
	}
	return StringName();
}

// --- Singleton queries ---

bool LinterDB::has_singleton(const StringName &p_name) const {
	return singletons.has(p_name);
}

// --- Listing ---

void LinterDB::get_class_list(LocalVector<StringName> &r_classes) const {
	for (const KeyValue<StringName, ClassData> &kv : classes) {
		r_classes.push_back(kv.key);
	}
}

void LinterDB::get_method_list(const StringName &p_class, List<MethodInfo> *r_methods, bool p_no_inheritance) const {
	StringName current = p_class;
	while (current != StringName()) {
		const ClassData *cd = get_class_data(current);
		if (!cd) {
			break;
		}
		for (const KeyValue<StringName, MethodData> &kv : cd->methods) {
			r_methods->push_back(kv.value.info);
		}
		if (p_no_inheritance) {
			break;
		}
		current = cd->parent;
	}
}

void LinterDB::get_property_list(const StringName &p_class, List<PropertyInfo> *r_properties, bool p_no_inheritance) const {
	StringName current = p_class;
	while (current != StringName()) {
		const ClassData *cd = get_class_data(current);
		if (!cd) {
			break;
		}
		for (const KeyValue<StringName, PropertyData> &kv : cd->properties) {
			r_properties->push_back(kv.value.info);
		}
		if (p_no_inheritance) {
			break;
		}
		current = cd->parent;
	}
}

void LinterDB::get_signal_list(const StringName &p_class, List<MethodInfo> *r_signals, bool p_no_inheritance) const {
	StringName current = p_class;
	while (current != StringName()) {
		const ClassData *cd = get_class_data(current);
		if (!cd) {
			break;
		}
		for (const KeyValue<StringName, MethodInfo> &kv : cd->signals) {
			r_signals->push_back(kv.value);
		}
		if (p_no_inheritance) {
			break;
		}
		current = cd->parent;
	}
}

void LinterDB::get_enum_list(const StringName &p_class, List<StringName> *r_enums, bool p_no_inheritance) const {
	StringName current = p_class;
	while (current != StringName()) {
		const ClassData *cd = get_class_data(current);
		if (!cd) {
			break;
		}
		for (const KeyValue<StringName, HashMap<StringName, int64_t>> &kv : cd->enums) {
			r_enums->push_back(kv.key);
		}
		if (p_no_inheritance) {
			break;
		}
		current = cd->parent;
	}
}

void LinterDB::get_integer_constant_list(const StringName &p_class, List<String> *r_constants, bool p_no_inheritance) const {
	StringName current = p_class;
	while (current != StringName()) {
		const ClassData *cd = get_class_data(current);
		if (!cd) {
			break;
		}
		for (const KeyValue<StringName, int64_t> &kv : cd->constants) {
			r_constants->push_back(String(kv.key));
		}
		for (const KeyValue<StringName, StringName> &kv : cd->constant_to_enum) {
			r_constants->push_back(String(kv.key));
		}
		if (p_no_inheritance) {
			break;
		}
		current = cd->parent;
	}
}

} // namespace linter

#endif // HOMOT
