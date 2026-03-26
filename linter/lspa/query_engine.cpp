/**************************************************************************/
/*  query_engine.cpp                                                      */
/**************************************************************************/

#ifdef HOMOT

#include "query_engine.h"

#include "../stubs/script_server_stub.h"

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_parser.h"

#include "core/io/file_access.h"

using linter::ScriptServerStub;

namespace lspa {

// ===========================================================================
// Script class cache — parse GDScript files to extract member info
// ===========================================================================

// Format a GDScriptParser::DataType into a readable string.
static String _datatype_to_string(const GDScriptParser::DataType &p_type) {
	switch (p_type.kind) {
		case GDScriptParser::DataType::BUILTIN:
			return Variant::get_type_name(p_type.builtin_type);
		case GDScriptParser::DataType::NATIVE:
			return String(p_type.native_type);
		case GDScriptParser::DataType::SCRIPT:
		case GDScriptParser::DataType::CLASS:
			if (p_type.class_type && p_type.class_type->identifier) {
				return String(p_type.class_type->identifier->name);
			}
			return p_type.script_path.get_file().get_basename();
		case GDScriptParser::DataType::ENUM:
			return String(p_type.enum_type);
		case GDScriptParser::DataType::VARIANT:
		default:
			return "Variant";
	}
}

// Build a signature string from a FunctionNode.
static String _function_signature(const GDScriptParser::FunctionNode *p_func) {
	String sig = "(";
	for (int i = 0; i < p_func->parameters.size(); i++) {
		if (i > 0) {
			sig += ", ";
		}
		const GDScriptParser::ParameterNode *param = p_func->parameters[i];
		sig += String(param->identifier->name);
		if (param->get_datatype().kind != GDScriptParser::DataType::VARIANT || param->datatype_specifier) {
			sig += ": " + _datatype_to_string(param->get_datatype());
		}
	}
	sig += ")";

	// Return type.
	if (p_func->return_type) {
		sig += " -> " + _datatype_to_string(p_func->get_datatype());
	} else {
		sig += " -> void";
	}
	return sig;
}

// Build an args string from a SignalNode.
static String _signal_args(const GDScriptParser::SignalNode *p_signal) {
	String args;
	for (int i = 0; i < p_signal->parameters.size(); i++) {
		if (i > 0) {
			args += ", ";
		}
		const GDScriptParser::ParameterNode *param = p_signal->parameters[i];
		args += String(param->identifier->name);
		if (param->get_datatype().kind != GDScriptParser::DataType::VARIANT || param->datatype_specifier) {
			args += ": " + _datatype_to_string(param->get_datatype());
		}
	}
	return args;
}

static ScriptClassInfo _parse_script_file(const StringName &p_class_name, const String &p_path, const StringName &p_native_base) {
	ScriptClassInfo info;
	info.name = p_class_name;
	info.native_base = p_native_base;
	info.path = p_path;

	String source = FileAccess::get_file_as_string(p_path);
	if (source.is_empty()) {
		info.extends = p_native_base;
		return info;
	}

	GDScriptParser parser;
	parser.parse(source, p_path, false);

	const GDScriptParser::ClassNode *tree = parser.get_tree();
	if (!tree) {
		info.extends = p_native_base;
		return info;
	}

	// Resolve extends.
	if (tree->extends.size() > 0) {
		info.extends = tree->extends[0]->name;
	} else if (!tree->extends_path.is_empty()) {
		info.extends = p_native_base;
	} else {
		info.extends = p_native_base;
	}

	// Extract members from the class tree.
	for (int i = 0; i < tree->members.size(); i++) {
		const GDScriptParser::ClassNode::Member &member = tree->members[i];

		switch (member.type) {
			case GDScriptParser::ClassNode::Member::FUNCTION: {
				const GDScriptParser::FunctionNode *func = member.function;
				if (!func->identifier) {
					break;
				}
				// Skip private functions (starting with _) except virtual overrides.
				String fname = String(func->identifier->name);
				Dictionary m;
				m["name"] = fname;
				m["sig"] = _function_signature(func);
				info.methods.push_back(m);
			} break;

			case GDScriptParser::ClassNode::Member::VARIABLE: {
				const GDScriptParser::VariableNode *var = member.variable;
				if (!var->identifier) {
					break;
				}
				Dictionary p;
				p["name"] = String(var->identifier->name);
				if (var->get_datatype().kind != GDScriptParser::DataType::VARIANT || var->datatype_specifier) {
					p["type"] = _datatype_to_string(var->get_datatype());
				} else {
					p["type"] = "Variant";
				}
				info.properties.push_back(p);
			} break;

			case GDScriptParser::ClassNode::Member::SIGNAL: {
				const GDScriptParser::SignalNode *sig = member.signal;
				if (!sig->identifier) {
					break;
				}
				Dictionary s;
				s["name"] = String(sig->identifier->name);
				s["args"] = _signal_args(sig);
				info.signals.push_back(s);
			} break;

			case GDScriptParser::ClassNode::Member::ENUM: {
				const GDScriptParser::EnumNode *en = member.m_enum;
				if (!en->identifier) {
					break;
				}
				Dictionary e;
				e["name"] = String(en->identifier->name);
				Dictionary values;
				for (int j = 0; j < en->values.size(); j++) {
					// Use the literal value if resolved, otherwise store index.
					values[String(en->values[j].identifier->name)] = en->values[j].value;
				}
				e["values"] = values;
				info.enums.push_back(e);
			} break;

			case GDScriptParser::ClassNode::Member::CONSTANT: {
				const GDScriptParser::ConstantNode *cn = member.constant;
				if (!cn->identifier) {
					break;
				}
				Dictionary c;
				c["name"] = String(cn->identifier->name);
				if (cn->initializer) {
					c["type"] = _datatype_to_string(cn->get_datatype());
				}
				info.constants.push_back(c);
			} break;

			default:
				break;
		}
	}

	return info;
}

void QueryEngine::ensure_script_cache() {
	if (script_cache_built) {
		return;
	}
	script_cache_built = true;

	LocalVector<StringName> class_list;
	ScriptServerStub::get_global_class_list(class_list);

	for (const StringName &cn : class_list) {
		String path = ScriptServerStub::get_global_class_path(cn);
		StringName native_base = ScriptServerStub::get_global_class_native_base(cn);
		script_cache[cn] = _parse_script_file(cn, path, native_base);
	}
}

ScriptClassInfo *QueryEngine::get_script_class(const StringName &p_name) {
	ensure_script_cache();
	auto it = script_cache.find(p_name);
	return it ? &it->value : nullptr;
}

void QueryEngine::invalidate_script_cache() {
	script_cache.clear();
	script_cache_built = false;
}

// Format a ScriptClassInfo into the same Dictionary shape as format_class.
Dictionary QueryEngine::format_script_class(const ScriptClassInfo &p_info, DetailLevel p_detail, const Vector<String> &p_sections) {
	bool all_sections = p_sections.is_empty();
	auto want_section = [&](const String &s) {
		return all_sections || p_sections.has(s);
	};

	Dictionary result;
	result["class"] = String(p_info.name);
	result["extends"] = String(p_info.extends);
	result["source"] = "script"; // Mark as script class.

	if (p_detail == DETAIL_NAMES_ONLY) {
		if (want_section("properties")) {
			Array names;
			for (const Dictionary &p : p_info.properties) {
				names.push_back(p["name"]);
			}
			result["properties"] = names;
		}
		if (want_section("methods")) {
			Array names;
			for (const Dictionary &m : p_info.methods) {
				names.push_back(m["name"]);
			}
			result["methods"] = names;
		}
		if (want_section("signals")) {
			Array names;
			for (const Dictionary &s : p_info.signals) {
				names.push_back(s["name"]);
			}
			result["signals"] = names;
		}
		if (want_section("enums")) {
			Array names;
			for (const Dictionary &e : p_info.enums) {
				names.push_back(e["name"]);
			}
			result["enums"] = names;
		}
		if (want_section("constants")) {
			Array names;
			for (const Dictionary &c : p_info.constants) {
				names.push_back(c["name"]);
			}
			result["constants"] = names;
		}
		return result;
	}

	// STANDARD and FULL — script classes don't have doc strings, so they're the same.
	auto vec_to_array = [](const Vector<Dictionary> &p_vec) -> Array {
		Array arr;
		for (const Dictionary &d : p_vec) {
			arr.push_back(d);
		}
		return arr;
	};

	if (want_section("properties")) {
		result["properties"] = vec_to_array(p_info.properties);
	}
	if (want_section("methods")) {
		result["methods"] = vec_to_array(p_info.methods);
	}
	if (want_section("signals")) {
		result["signals"] = vec_to_array(p_info.signals);
	}
	if (want_section("enums")) {
		result["enums"] = vec_to_array(p_info.enums);
	}
	if (want_section("constants")) {
		result["constants"] = vec_to_array(p_info.constants);
	}

	return result;
}

// ===========================================================================
// Shared helpers
// ===========================================================================

// Case-insensitive class lookup across both LinterDB and ScriptServerStub.
// Returns the resolved class name, or StringName() if not found.
// Sets r_is_script to true if found in script cache.
StringName QueryEngine::_resolve_class_name(const String &p_name, bool &r_is_script) {
	r_is_script = false;
	LinterDB *db = LinterDB::get_singleton();

	// Try LinterDB first (exact).
	if (db && db->class_exists(StringName(p_name))) {
		return StringName(p_name);
	}

	// Try script cache (exact).
	ensure_script_cache();
	if (script_cache.has(StringName(p_name))) {
		r_is_script = true;
		return StringName(p_name);
	}

	// Case-insensitive fallback — LinterDB.
	String name_lower = p_name.to_lower();
	if (db) {
		LocalVector<StringName> all_classes;
		db->get_class_list(all_classes);
		for (const StringName &cn : all_classes) {
			if (String(cn).to_lower() == name_lower) {
				return cn;
			}
		}
	}

	// Case-insensitive fallback — script cache.
	for (const KeyValue<StringName, ScriptClassInfo> &kv : script_cache) {
		if (String(kv.key).to_lower() == name_lower) {
			r_is_script = true;
			return kv.key;
		}
	}

	return StringName();
}

// ===========================================================================
// api/class
// ===========================================================================

Dictionary QueryEngine::handle_class(const Dictionary &p_params) {
	String name = p_params.get("name", "");
	String detail_str = p_params.get("detail", "standard");
	DetailLevel detail = parse_detail_level(detail_str);

	bool is_script = false;
	StringName class_name = _resolve_class_name(name, is_script);

	if (class_name == StringName()) {
		Dictionary err;
		err["error"] = vformat("Class not found: %s", name);
		return err;
	}

	// Parse sections filter.
	Vector<String> sections;
	if (p_params.has("sections") && p_params["sections"].get_type() == Variant::ARRAY) {
		Array sec = p_params["sections"];
		for (int i = 0; i < sec.size(); i++) {
			sections.push_back(sec[i]);
		}
	}

	if (is_script) {
		ScriptClassInfo *sci = get_script_class(class_name);
		if (sci) {
			return format_script_class(*sci, detail, sections);
		}
	}

	return format_class(class_name, detail, sections);
}

// ===========================================================================
// api/classes
// ===========================================================================

Dictionary QueryEngine::handle_classes(const Dictionary &p_params) {
	Array names = p_params.get("names", Array());
	String detail_str = p_params.get("detail", "standard");
	DetailLevel detail = parse_detail_level(detail_str);

	Dictionary found;
	Array not_found;

	for (int i = 0; i < names.size(); i++) {
		String name = names[i];
		bool is_script = false;
		StringName sn = _resolve_class_name(name, is_script);

		if (sn == StringName()) {
			not_found.push_back(name);
			continue;
		}

		if (is_script) {
			ScriptClassInfo *sci = get_script_class(sn);
			if (sci) {
				found[String(sn)] = format_script_class(*sci, detail);
			} else {
				not_found.push_back(name);
			}
		} else {
			found[String(sn)] = format_class(sn, detail);
		}
	}

	Dictionary result;
	result["found"] = found;
	result["not_found"] = not_found;
	return result;
}

// ===========================================================================
// api/search
// ===========================================================================

// Simple keyword matching: split query into words, check if all words appear
// in the target string (case-insensitive).
static bool _matches_keywords(const String &p_target, const Vector<String> &p_keywords) {
	String target_lower = p_target.to_lower();
	for (const String &kw : p_keywords) {
		if (target_lower.find(kw) == -1) {
			return false;
		}
	}
	return true;
}

// Score a match: higher = more relevant.
// 3 = class name matches, 2 = member name matches, 1 = description only.
static int _score_match(const String &p_class_name, const String &p_member_name,
		const String &p_description, const Vector<String> &p_keywords) {
	if (_matches_keywords(p_class_name, p_keywords)) {
		return 3;
	}
	if (_matches_keywords(p_member_name, p_keywords)) {
		return 2;
	}
	String combined = p_member_name + " " + p_description;
	if (_matches_keywords(combined, p_keywords)) {
		return 1;
	}
	return 0;
}

struct _SearchHit {
	Dictionary entry;
	int score;
	StringName class_name;
};

struct _SearchHitComparator {
	bool operator()(const _SearchHit &a, const _SearchHit &b) const {
		return a.score > b.score; // Higher score first.
	}
};

Variant QueryEngine::handle_search(const Dictionary &p_params) {
	LinterDB *db = LinterDB::get_singleton();

	String query = p_params.get("query", "");
	String filter = p_params.get("filter", ""); // method/property/signal/enum/constant or empty
	String class_filter = p_params.get("class_filter", "");
	int limit = p_params.get("limit", 20);

	// Split query into lowercase keywords.
	Vector<String> keywords;
	Vector<String> parts = query.to_lower().split(" ", false);
	for (const String &p : parts) {
		String stripped = p.strip_edges();
		if (!stripped.is_empty()) {
			keywords.push_back(stripped);
		}
	}

	if (keywords.is_empty()) {
		return Array();
	}

	// Collect all matches with scores, then sort.
	LocalVector<_SearchHit> hits;

	// Cap per-class to avoid a single class dominating results.
	const int per_class_cap = MAX(3, limit / 3);

	// --- Search LinterDB classes ---
	if (db) {
		LocalVector<StringName> search_classes;
		if (!class_filter.is_empty()) {
			StringName sn(class_filter);
			if (db->class_exists(sn)) {
				search_classes.push_back(sn);
			}
		} else {
			db->get_class_list(search_classes);
		}

		for (const StringName &cn : search_classes) {
			const ClassData *cd = db->get_class_data(cn);
			if (!cd) {
				continue;
			}
			int class_hits = 0;
			String cn_str = String(cn);

			// Search methods.
			if (filter.is_empty() || filter == "method") {
				for (const KeyValue<StringName, MethodData> &kv : cd->methods) {
					String desc;
					const DocMethodData *md = cd->doc.find_method(String(kv.key));
					if (md) {
						desc = md->description;
					}
					int score = _score_match(cn_str, String(kv.key), desc, keywords);
					if (score > 0 && class_hits < per_class_cap) {
						_SearchHit hit;
						hit.entry = format_search_result(cn, String(kv.key), "method", &kv.value.info, nullptr);
						hit.score = score;
						hit.class_name = cn;
						hits.push_back(hit);
						class_hits++;
					}
				}
			}

			// Search properties.
			if (filter.is_empty() || filter == "property") {
				for (const KeyValue<StringName, PropertyData> &kv : cd->properties) {
					String desc;
					const DocPropertyData *pd = cd->doc.find_property(String(kv.key));
					if (pd) {
						desc = pd->description;
					}
					int score = _score_match(cn_str, String(kv.key), desc, keywords);
					if (score > 0 && class_hits < per_class_cap) {
						_SearchHit hit;
						hit.entry = format_search_result(cn, String(kv.key), "property", nullptr, &kv.value.info);
						hit.score = score;
						hit.class_name = cn;
						hits.push_back(hit);
						class_hits++;
					}
				}
			}

			// Search signals.
			if (filter.is_empty() || filter == "signal") {
				for (const KeyValue<StringName, MethodInfo> &kv : cd->signals) {
					String desc;
					const DocMethodData *sd = cd->doc.find_signal(String(kv.key));
					if (sd) {
						desc = sd->description;
					}
					int score = _score_match(cn_str, String(kv.key), desc, keywords);
					if (score > 0 && class_hits < per_class_cap) {
						_SearchHit hit;
						hit.entry = format_search_result(cn, String(kv.key), "signal", &kv.value, nullptr);
						hit.score = score;
						hit.class_name = cn;
						hits.push_back(hit);
						class_hits++;
					}
				}
			}

			// Search enums.
			if (filter.is_empty() || filter == "enum") {
				for (const KeyValue<StringName, HashMap<StringName, int64_t>> &kv : cd->enums) {
					int score = _score_match(cn_str, String(kv.key), "", keywords);
					if (score > 0 && class_hits < per_class_cap) {
						Dictionary r;
						r["class"] = cn_str;
						r["name"] = String(kv.key);
						r["kind"] = "enum";
						_SearchHit hit;
						hit.entry = r;
						hit.score = score;
						hit.class_name = cn;
						hits.push_back(hit);
						class_hits++;
					}
				}
			}

			// Search constants.
			if (filter.is_empty() || filter == "constant") {
				for (const KeyValue<StringName, int64_t> &kv : cd->constants) {
					int score = _score_match(cn_str, String(kv.key), "", keywords);
					if (score > 0 && class_hits < per_class_cap) {
						Dictionary r;
						r["class"] = cn_str;
						r["name"] = String(kv.key);
						r["kind"] = "constant";
						r["value"] = kv.value;
						_SearchHit hit;
						hit.entry = r;
						hit.score = score;
						hit.class_name = cn;
						hits.push_back(hit);
						class_hits++;
					}
				}
			}
		}
	}

	// --- Search script classes ---
	ensure_script_cache();

	for (const KeyValue<StringName, ScriptClassInfo> &kv : script_cache) {
		const ScriptClassInfo &sci = kv.value;

		// If class_filter is set and doesn't match, skip.
		if (!class_filter.is_empty() && String(sci.name) != class_filter) {
			continue;
		}

		int class_hits = 0;
		String cn_str = String(sci.name);

		// Search methods.
		if (filter.is_empty() || filter == "method") {
			for (const Dictionary &m : sci.methods) {
				String mname = m["name"];
				int score = _score_match(cn_str, mname, "", keywords);
				if (score > 0 && class_hits < per_class_cap) {
					Dictionary r;
					r["class"] = cn_str;
					r["name"] = mname;
					r["kind"] = "method";
					r["sig"] = m["sig"];
					r["source"] = "script";
					_SearchHit hit;
					hit.entry = r;
					hit.score = score;
					hit.class_name = sci.name;
					hits.push_back(hit);
					class_hits++;
				}
			}
		}

		// Search properties.
		if (filter.is_empty() || filter == "property") {
			for (const Dictionary &p : sci.properties) {
				String pname = p["name"];
				int score = _score_match(cn_str, pname, "", keywords);
				if (score > 0 && class_hits < per_class_cap) {
					Dictionary r;
					r["class"] = cn_str;
					r["name"] = pname;
					r["kind"] = "property";
					r["type"] = p["type"];
					r["source"] = "script";
					_SearchHit hit;
					hit.entry = r;
					hit.score = score;
					hit.class_name = sci.name;
					hits.push_back(hit);
					class_hits++;
				}
			}
		}

		// Search signals.
		if (filter.is_empty() || filter == "signal") {
			for (const Dictionary &s : sci.signals) {
				String sname = s["name"];
				int score = _score_match(cn_str, sname, "", keywords);
				if (score > 0 && class_hits < per_class_cap) {
					Dictionary r;
					r["class"] = cn_str;
					r["name"] = sname;
					r["kind"] = "signal";
					r["args"] = s["args"];
					r["source"] = "script";
					_SearchHit hit;
					hit.entry = r;
					hit.score = score;
					hit.class_name = sci.name;
					hits.push_back(hit);
					class_hits++;
				}
			}
		}

		// Search enums.
		if (filter.is_empty() || filter == "enum") {
			for (const Dictionary &e : sci.enums) {
				String ename = e["name"];
				int score = _score_match(cn_str, ename, "", keywords);
				if (score > 0 && class_hits < per_class_cap) {
					Dictionary r;
					r["class"] = cn_str;
					r["name"] = ename;
					r["kind"] = "enum";
					r["source"] = "script";
					_SearchHit hit;
					hit.entry = r;
					hit.score = score;
					hit.class_name = sci.name;
					hits.push_back(hit);
					class_hits++;
				}
			}
		}

		// Search constants.
		if (filter.is_empty() || filter == "constant") {
			for (const Dictionary &c : sci.constants) {
				String cname = c["name"];
				int score = _score_match(cn_str, cname, "", keywords);
				if (score > 0 && class_hits < per_class_cap) {
					Dictionary r;
					r["class"] = cn_str;
					r["name"] = cname;
					r["kind"] = "constant";
					r["source"] = "script";
					_SearchHit hit;
					hit.entry = r;
					hit.score = score;
					hit.class_name = sci.name;
					hits.push_back(hit);
					class_hits++;
				}
			}
		}
	}

	// Sort by score descending.
	hits.sort_custom<_SearchHitComparator>();

	// Take top results up to limit.
	Array results;
	for (uint32_t i = 0; i < hits.size() && results.size() < limit; i++) {
		results.push_back(hits[i].entry);
	}

	return results;
}

// ===========================================================================
// api/hierarchy
// ===========================================================================

Dictionary QueryEngine::handle_hierarchy(const Dictionary &p_params) {
	LinterDB *db = LinterDB::get_singleton();

	String name = p_params.get("name", "");
	String direction = p_params.get("direction", "up");

	bool is_script = false;
	StringName class_name = _resolve_class_name(name, is_script);

	if (class_name == StringName()) {
		Dictionary err;
		err["error"] = vformat("Class not found: %s", name);
		return err;
	}

	Dictionary result;

	if (direction == "up") {
		// Walk up the inheritance chain.
		Array chain;
		HashSet<StringName> visited;

		if (is_script) {
			// Start from script class, walk through script extends chain,
			// then continue into native class chain.
			StringName current = class_name;
			while (current != StringName() && !visited.has(current)) {
				visited.insert(current);
				chain.push_back(String(current));
				ScriptClassInfo *sci = get_script_class(current);
				if (sci) {
					current = sci->extends;
				} else {
					break;
				}
			}
			// Now continue into native chain if the last extends is a native class.
			if (current != StringName() && !visited.has(current) && db && db->class_exists(current)) {
				// current is already the first native class — add it and walk up.
				while (current != StringName() && !visited.has(current)) {
					visited.insert(current);
					chain.push_back(String(current));
					current = db->get_parent_class(current);
				}
			}
		} else if (db) {
			StringName current = class_name;
			while (current != StringName() && !visited.has(current)) {
				visited.insert(current);
				chain.push_back(String(current));
				current = db->get_parent_class(current);
			}
		}
		result["chain"] = chain;
	} else {
		// Find all direct children (both native and script).
		Array children;

		if (db) {
			LocalVector<StringName> all_classes;
			db->get_class_list(all_classes);
			for (const StringName &cn : all_classes) {
				if (db->get_parent_class(cn) == class_name) {
					children.push_back(String(cn));
				}
			}
		}

		// Also check script classes that extend this class.
		ensure_script_cache();
		for (const KeyValue<StringName, ScriptClassInfo> &kv : script_cache) {
			if (kv.value.extends == class_name) {
				children.push_back(String(kv.key));
			}
		}

		result["children"] = children;
	}

	return result;
}

// ===========================================================================
// api/catalog
// ===========================================================================

// Simple domain classification based on class name prefix and inheritance.
static String _classify_domain(const StringName &p_class, LinterDB *p_db) {
	String name = String(p_class);

	// Check inheritance for broad categories.
	if (p_db && p_db->is_parent_class(p_class, "Node3D")) {
		return "3d";
	}
	if (p_db && p_db->is_parent_class(p_class, "Control")) {
		return "ui";
	}
	if (p_db && (p_db->is_parent_class(p_class, "Node2D") || p_db->is_parent_class(p_class, "CanvasItem"))) {
		return "2d";
	}

	// Name prefix heuristics.
	if (name.begins_with("Physics") || name.begins_with("Collision") || name.begins_with("Joint") ||
			name.begins_with("RigidBody") || name.begins_with("CharacterBody") || name.begins_with("StaticBody")) {
		return "physics";
	}
	if (name.begins_with("Audio") || name.begins_with("Sound")) {
		return "audio";
	}
	if (name.begins_with("Visual") || name.begins_with("Mesh") || name.begins_with("Material") ||
			name.begins_with("Shader") || name.begins_with("Light") || name.begins_with("Camera") ||
			name.begins_with("Texture") || name.begins_with("Sprite")) {
		return "visual";
	}

	return "other";
}

// Classify a script class domain using its native base.
static String _classify_script_domain(const ScriptClassInfo &p_info, LinterDB *p_db) {
	if (!p_db) {
		return "script";
	}
	// Use the native base for domain classification.
	if (p_info.native_base != StringName() && p_db->class_exists(p_info.native_base)) {
		return _classify_domain(p_info.native_base, p_db);
	}
	return "script";
}

Dictionary QueryEngine::handle_catalog(const Dictionary &p_params) {
	LinterDB *db = LinterDB::get_singleton();

	String domain = p_params.get("domain", "");

	Array categories;
	int total = 0;

	// Native classes.
	if (db) {
		LocalVector<StringName> all_classes;
		db->get_class_list(all_classes);

		for (const StringName &cn : all_classes) {
			if (!domain.is_empty() && _classify_domain(cn, db) != domain) {
				continue;
			}
			total++;

			Dictionary entry;
			entry["name"] = String(cn);
			entry["extends"] = String(db->get_parent_class(cn));

			const ClassData *cd = db->get_class_data(cn);
			if (cd) {
				entry["entries"] = (int)(cd->methods.size() + cd->properties.size() + cd->signals.size());
			}

			const DocClassData *doc = db->get_class_doc(cn);
			if (doc && !doc->brief_description.is_empty()) {
				entry["brief"] = doc->brief_description;
			}

			categories.push_back(entry);
		}
	}

	// Script classes.
	ensure_script_cache();

	for (const KeyValue<StringName, ScriptClassInfo> &kv : script_cache) {
		const ScriptClassInfo &sci = kv.value;
		String sci_domain = _classify_script_domain(sci, db);

		if (!domain.is_empty() && sci_domain != domain) {
			continue;
		}
		total++;

		Dictionary entry;
		entry["name"] = String(sci.name);
		entry["extends"] = String(sci.extends);
		entry["entries"] = sci.methods.size() + sci.properties.size() + sci.signals.size();
		entry["source"] = "script";
		categories.push_back(entry);
	}

	Dictionary result;
	result["total"] = total;
	result["categories"] = categories;
	return result;
}

// ===========================================================================
// api/globals
// ===========================================================================

Variant QueryEngine::handle_globals(const Dictionary &p_params) {
	LinterDB *db = LinterDB::get_singleton();
	if (!db) {
		return Dictionary();
	}

	String category = p_params.get("category", "singletons");

	if (category == "singletons") {
		Array items;
		LocalVector<StringName> singleton_list;
		db->get_singleton_list(singleton_list);
		for (const StringName &s : singleton_list) {
			Dictionary entry;
			entry["name"] = String(s);
			entry["class"] = String(s);
			const DocClassData *doc = db->get_class_doc(s);
			if (doc && !doc->brief_description.is_empty()) {
				entry["desc"] = doc->brief_description;
			}
			items.push_back(entry);
		}
		return items;
	}

	if (category == "utility_functions") {
		Array items;
		LocalVector<StringName> func_list;
		db->get_utility_function_list(func_list);
		for (const StringName &fn : func_list) {
			const linter::UtilityFunctionData *ufd = db->get_utility_function(fn);
			if (!ufd) {
				continue;
			}
			Dictionary entry;
			entry["name"] = String(fn);
			entry["sig"] = format_method_signature(ufd->info);
			const DocMethodData *doc = db->get_utility_function_doc(fn);
			if (doc && !doc->description.is_empty()) {
				entry["desc"] = doc->description;
			}
			items.push_back(entry);
		}
		return items;
	}

	if (category == "global_enums") {
		Dictionary result;
		LocalVector<StringName> enum_list;
		db->get_global_enum_list(enum_list);
		for (const StringName &en : enum_list) {
			HashMap<StringName, int64_t> constants;
			db->get_global_enum_constants(en, constants);
			Dictionary values;
			for (const KeyValue<StringName, int64_t> &kv : constants) {
				values[String(kv.key)] = kv.value;
			}
			result[String(en)] = values;
		}
		return result;
	}

	if (category == "global_constants") {
		Dictionary result;
		LocalVector<StringName> const_list;
		db->get_global_constant_list(const_list);
		for (const StringName &cn : const_list) {
			bool valid = false;
			int64_t val = db->get_global_constant(cn, &valid);
			if (valid) {
				result[String(cn)] = val;
			}
		}
		return result;
	}

	Dictionary err;
	err["error"] = vformat("Unknown category: %s. Expected: singletons, utility_functions, global_enums, global_constants", category);
	return err;
}

} // namespace lspa

#endif // HOMOT
