/**************************************************************************/
/*  formatter.cpp                                                         */
/**************************************************************************/

#ifdef HOMOT

#include "formatter.h"

namespace lspa {

DetailLevel parse_detail_level(const String &p_str) {
	if (p_str == "names_only" || p_str == "compact") {
		return DETAIL_NAMES_ONLY;
	} else if (p_str == "full" || p_str == "verbose") {
		return DETAIL_FULL;
	}
	return DETAIL_STANDARD;
}

String format_type(Variant::Type p_type, const StringName &p_class_name) {
	if (p_type == Variant::OBJECT && p_class_name != StringName()) {
		return String(p_class_name);
	}
	if (p_type == Variant::NIL) {
		return "Variant";
	}
	return Variant::get_type_name(p_type);
}

String format_method_signature(const MethodInfo &p_info) {
	String sig = "(";
	for (int i = 0; i < p_info.arguments.size(); i++) {
		if (i > 0) {
			sig += ", ";
		}
		const PropertyInfo &arg = p_info.arguments[i];
		sig += String(arg.name) + ": " + format_type(arg.type, arg.class_name);
	}
	sig += ")";

	// Return type.
	if (p_info.return_val.type != Variant::NIL || p_info.return_val.class_name != StringName()) {
		sig += " -> " + format_type(p_info.return_val.type, p_info.return_val.class_name);
	} else {
		sig += " -> void";
	}
	return sig;
}

// Helper: format method signature from doc data.
static String _format_doc_method_sig(const DocMethodData &p_doc) {
	String sig = "(";
	for (int i = 0; i < p_doc.arguments.size(); i++) {
		if (i > 0) {
			sig += ", ";
		}
		sig += p_doc.arguments[i].name + ": " + p_doc.arguments[i].type;
		if (!p_doc.arguments[i].default_value.is_empty()) {
			sig += " = " + p_doc.arguments[i].default_value;
		}
	}
	sig += ")";
	if (!p_doc.return_type.is_empty() && p_doc.return_type != "void") {
		sig += " -> " + p_doc.return_type;
	} else {
		sig += " -> void";
	}
	return sig;
}

// Check if a method name looks like a getter/setter to filter in standard mode.
static bool _is_getter_setter(const StringName &p_name) {
	String n = String(p_name);
	return n.begins_with("get_") || n.begins_with("set_") || n.begins_with("is_") || n.begins_with("has_");
}

Dictionary format_class(const StringName &p_class, DetailLevel p_detail, const Vector<String> &p_sections) {
	LinterDB *db = LinterDB::get_singleton();
	if (!db) {
		return Dictionary();
	}
	const ClassData *cd = db->get_class_data(p_class);
	if (!cd) {
		return Dictionary();
	}

	bool all_sections = p_sections.is_empty();
	auto want_section = [&](const String &s) {
		return all_sections || p_sections.has(s);
	};

	Dictionary result;
	result["class"] = String(cd->name);
	result["extends"] = String(cd->parent);

	// Brief description (always include if available).
	const DocClassData *doc = db->get_class_doc(p_class);
	if (doc && !doc->brief_description.is_empty()) {
		result["brief"] = doc->brief_description;
	}

	// --- NAMES_ONLY ---
	if (p_detail == DETAIL_NAMES_ONLY) {
		if (want_section("properties")) {
			Array names;
			for (const KeyValue<StringName, PropertyData> &kv : cd->properties) {
				names.push_back(String(kv.key));
			}
			result["properties"] = names;
		}
		if (want_section("methods")) {
			Array names;
			for (const KeyValue<StringName, MethodData> &kv : cd->methods) {
				names.push_back(String(kv.key));
			}
			result["methods"] = names;
		}
		if (want_section("signals")) {
			Array names;
			for (const KeyValue<StringName, MethodInfo> &kv : cd->signals) {
				names.push_back(String(kv.key));
			}
			result["signals"] = names;
		}
		if (want_section("enums")) {
			Array names;
			for (const KeyValue<StringName, HashMap<StringName, int64_t>> &kv : cd->enums) {
				names.push_back(String(kv.key));
			}
			result["enums"] = names;
		}
		if (want_section("constants")) {
			Array names;
			for (const KeyValue<StringName, int64_t> &kv : cd->constants) {
				names.push_back(String(kv.key));
			}
			result["constants"] = names;
		}
		return result;
	}

	// --- STANDARD ---
	if (p_detail == DETAIL_STANDARD) {
		if (want_section("properties")) {
			Array props;
			for (const KeyValue<StringName, PropertyData> &kv : cd->properties) {
				Dictionary p;
				p["name"] = String(kv.key);
				p["type"] = format_type(kv.value.info.type, kv.value.info.class_name);
				// Add doc description if available and non-empty.
				if (doc) {
					const DocPropertyData *pd = doc->find_property(String(kv.key));
					if (pd && !pd->description.is_empty()) {
						p["desc"] = pd->description;
					}
				}
				props.push_back(p);
			}
			result["properties"] = props;
		}
		if (want_section("methods")) {
			Array methods;
			for (const KeyValue<StringName, MethodData> &kv : cd->methods) {
				// Filter out simple getters/setters that correspond to properties.
				if (_is_getter_setter(kv.key) && cd->properties.size() > 0) {
					// Check if there's a corresponding property.
					String method_name = String(kv.key);
					String prop_name;
					if (method_name.begins_with("get_") || method_name.begins_with("set_")) {
						prop_name = method_name.substr(4);
					} else if (method_name.begins_with("is_")) {
						prop_name = method_name.substr(3);
					} else if (method_name.begins_with("has_")) {
						prop_name = method_name.substr(4);
					}
					if (!prop_name.is_empty() && cd->properties.has(StringName(prop_name))) {
						continue; // Skip — property already exposes this.
					}
				}

				Dictionary m;
				m["name"] = String(kv.key);
				m["sig"] = format_method_signature(kv.value.info);
				if (doc) {
					const DocMethodData *md = doc->find_method(String(kv.key));
					if (md && !md->description.is_empty()) {
						m["desc"] = md->description;
					}
				}
				methods.push_back(m);
			}
			result["methods"] = methods;
		}
		if (want_section("signals")) {
			Array sigs;
			for (const KeyValue<StringName, MethodInfo> &kv : cd->signals) {
				Dictionary s;
				s["name"] = String(kv.key);
				String args;
				for (int i = 0; i < kv.value.arguments.size(); i++) {
					if (i > 0) {
						args += ", ";
					}
					args += String(kv.value.arguments[i].name) + ": " + format_type(kv.value.arguments[i].type, kv.value.arguments[i].class_name);
				}
				s["args"] = args;
				sigs.push_back(s);
			}
			result["signals"] = sigs;
		}
		if (want_section("enums")) {
			Dictionary enums;
			for (const KeyValue<StringName, HashMap<StringName, int64_t>> &kv : cd->enums) {
				Dictionary values;
				for (const KeyValue<StringName, int64_t> &ev : kv.value) {
					values[String(ev.key)] = ev.value;
				}
				enums[String(kv.key)] = values;
			}
			result["enums"] = enums;
		}
		if (want_section("constants")) {
			Dictionary consts;
			for (const KeyValue<StringName, int64_t> &kv : cd->constants) {
				consts[String(kv.key)] = kv.value;
			}
			result["constants"] = consts;
		}
		return result;
	}

	// --- FULL ---
	if (want_section("properties")) {
		Array props;
		for (const KeyValue<StringName, PropertyData> &kv : cd->properties) {
			Dictionary p;
			p["name"] = String(kv.key);
			p["type"] = format_type(kv.value.info.type, kv.value.info.class_name);
			if (!String(kv.value.getter).is_empty()) {
				p["getter"] = String(kv.value.getter);
			}
			if (!String(kv.value.setter).is_empty()) {
				p["setter"] = String(kv.value.setter);
			}
			if (doc) {
				const DocPropertyData *pd = doc->find_property(String(kv.key));
				if (pd) {
					if (!pd->description.is_empty()) {
						p["desc"] = pd->description;
					}
					if (!pd->default_value.is_empty()) {
						p["default"] = pd->default_value;
					}
				}
			}
			props.push_back(p);
		}
		result["properties"] = props;
	}
	if (want_section("methods")) {
		Array methods;
		for (const KeyValue<StringName, MethodData> &kv : cd->methods) {
			Dictionary m;
			m["name"] = String(kv.key);
			m["sig"] = format_method_signature(kv.value.info);
			if (kv.value.is_static) {
				m["static"] = true;
			}
			if (kv.value.is_vararg) {
				m["vararg"] = true;
			}
			if (doc) {
				const DocMethodData *md = doc->find_method(String(kv.key));
				if (md) {
					if (!md->description.is_empty()) {
						m["desc"] = md->description;
					}
					if (md->is_deprecated) {
						m["deprecated"] = !md->deprecated_message.is_empty() ? Variant(md->deprecated_message) : Variant(true);
					}
				}
			}
			methods.push_back(m);
		}
		result["methods"] = methods;
	}
	if (want_section("signals")) {
		Array sigs;
		for (const KeyValue<StringName, MethodInfo> &kv : cd->signals) {
			Dictionary s;
			s["name"] = String(kv.key);
			String args;
			for (int i = 0; i < kv.value.arguments.size(); i++) {
				if (i > 0) {
					args += ", ";
				}
				args += String(kv.value.arguments[i].name) + ": " + format_type(kv.value.arguments[i].type, kv.value.arguments[i].class_name);
			}
			s["args"] = args;
			if (doc) {
				const DocMethodData *sd = doc->find_signal(String(kv.key));
				if (sd && !sd->description.is_empty()) {
					s["desc"] = sd->description;
				}
			}
			sigs.push_back(s);
		}
		result["signals"] = sigs;
	}
	if (want_section("enums")) {
		Dictionary enums;
		for (const KeyValue<StringName, HashMap<StringName, int64_t>> &kv : cd->enums) {
			Dictionary values;
			for (const KeyValue<StringName, int64_t> &ev : kv.value) {
				values[String(ev.key)] = ev.value;
			}
			enums[String(kv.key)] = values;
		}
		result["enums"] = enums;
	}
	if (want_section("constants")) {
		Dictionary consts;
		for (const KeyValue<StringName, int64_t> &kv : cd->constants) {
			consts[String(kv.key)] = kv.value;
		}
		result["constants"] = consts;
	}

	// Full description.
	if (doc && !doc->description.is_empty()) {
		result["description"] = doc->description;
	}

	return result;
}

Dictionary format_search_result(const StringName &p_class, const String &p_name, const String &p_kind, const MethodInfo *p_method_info, const PropertyInfo *p_property_info) {
	Dictionary r;
	r["class"] = String(p_class);
	r["name"] = p_name;
	r["kind"] = p_kind;
	if (p_method_info) {
		r["sig"] = format_method_signature(*p_method_info);
	}
	if (p_property_info) {
		r["type"] = format_type(p_property_info->type, p_property_info->class_name);
	}
	return r;
}

} // namespace lspa

#endif // HOMOT
