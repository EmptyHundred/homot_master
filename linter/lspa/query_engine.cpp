/**************************************************************************/
/*  query_engine.cpp                                                      */
/**************************************************************************/

#ifdef HOMOT

#include "query_engine.h"

namespace lspa {

// ---------------------------------------------------------------------------
// api/class
// ---------------------------------------------------------------------------

Dictionary QueryEngine::handle_class(const Dictionary &p_params) {
	LinterDB *db = LinterDB::get_singleton();
	if (!db) {
		return Dictionary();
	}

	String name = p_params.get("name", "");
	String detail_str = p_params.get("detail", "standard");
	DetailLevel detail = parse_detail_level(detail_str);

	// Case-insensitive lookup.
	StringName class_name;
	if (db->class_exists(StringName(name))) {
		class_name = StringName(name);
	} else {
		// Try case-insensitive match.
		LocalVector<StringName> all_classes;
		db->get_class_list(all_classes);
		String name_lower = name.to_lower();
		for (const StringName &cn : all_classes) {
			if (String(cn).to_lower() == name_lower) {
				class_name = cn;
				break;
			}
		}
	}

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

	return format_class(class_name, detail, sections);
}

// ---------------------------------------------------------------------------
// api/classes
// ---------------------------------------------------------------------------

Dictionary QueryEngine::handle_classes(const Dictionary &p_params) {
	LinterDB *db = LinterDB::get_singleton();
	if (!db) {
		return Dictionary();
	}

	Array names = p_params.get("names", Array());
	String detail_str = p_params.get("detail", "standard");
	DetailLevel detail = parse_detail_level(detail_str);

	Dictionary found;
	Array not_found;

	for (int i = 0; i < names.size(); i++) {
		String name = names[i];
		StringName sn(name);

		if (!db->class_exists(sn)) {
			// Case-insensitive fallback.
			LocalVector<StringName> all_classes;
			db->get_class_list(all_classes);
			String name_lower = name.to_lower();
			sn = StringName();
			for (const StringName &cn : all_classes) {
				if (String(cn).to_lower() == name_lower) {
					sn = cn;
					break;
				}
			}
		}

		if (sn == StringName()) {
			not_found.push_back(name);
			continue;
		}

		found[String(sn)] = format_class(sn, detail);
	}

	Dictionary result;
	result["found"] = found;
	result["not_found"] = not_found;
	return result;
}

// ---------------------------------------------------------------------------
// api/search
// ---------------------------------------------------------------------------

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
	if (!db) {
		return Dictionary();
	}

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

	// Determine which classes to search.
	LocalVector<StringName> search_classes;
	if (!class_filter.is_empty()) {
		StringName sn(class_filter);
		if (db->class_exists(sn)) {
			search_classes.push_back(sn);
		}
	} else {
		db->get_class_list(search_classes);
	}

	// Cap per-class to avoid a single class dominating results.
	const int per_class_cap = MAX(3, limit / 3);

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

	// Sort by score descending.
	hits.sort_custom<_SearchHitComparator>();

	// Take top results up to limit.
	Array results;
	for (uint32_t i = 0; i < hits.size() && results.size() < limit; i++) {
		results.push_back(hits[i].entry);
	}

	return results;
}

// ---------------------------------------------------------------------------
// api/hierarchy
// ---------------------------------------------------------------------------

Dictionary QueryEngine::handle_hierarchy(const Dictionary &p_params) {
	LinterDB *db = LinterDB::get_singleton();
	if (!db) {
		return Dictionary();
	}

	String name = p_params.get("name", "");
	String direction = p_params.get("direction", "up");

	StringName class_name(name);
	if (!db->class_exists(class_name)) {
		// Case-insensitive fallback.
		LocalVector<StringName> all_classes;
		db->get_class_list(all_classes);
		String name_lower = name.to_lower();
		class_name = StringName();
		for (const StringName &cn : all_classes) {
			if (String(cn).to_lower() == name_lower) {
				class_name = cn;
				break;
			}
		}
	}

	if (class_name == StringName()) {
		Dictionary err;
		err["error"] = vformat("Class not found: %s", name);
		return err;
	}

	Dictionary result;

	if (direction == "up") {
		// Walk up to Object.
		Array chain;
		StringName current = class_name;
		HashSet<StringName> visited;
		while (current != StringName() && !visited.has(current)) {
			visited.insert(current);
			chain.push_back(String(current));
			current = db->get_parent_class(current);
		}
		result["chain"] = chain;
	} else {
		// Find all direct children.
		Array children;
		LocalVector<StringName> all_classes;
		db->get_class_list(all_classes);
		for (const StringName &cn : all_classes) {
			if (db->get_parent_class(cn) == class_name) {
				children.push_back(String(cn));
			}
		}
		result["children"] = children;
	}

	return result;
}

// ---------------------------------------------------------------------------
// api/catalog
// ---------------------------------------------------------------------------

// Simple domain classification based on class name prefix and inheritance.
static String _classify_domain(const StringName &p_class, LinterDB *p_db) {
	String name = String(p_class);

	// Check inheritance for broad categories.
	// Control must be checked before CanvasItem — Control inherits CanvasItem,
	// so checking CanvasItem first would misclassify all UI controls as "2d".
	if (p_db->is_parent_class(p_class, "Node3D")) {
		return "3d";
	}
	if (p_db->is_parent_class(p_class, "Control")) {
		return "ui";
	}
	if (p_db->is_parent_class(p_class, "Node2D") || p_db->is_parent_class(p_class, "CanvasItem")) {
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

Dictionary QueryEngine::handle_catalog(const Dictionary &p_params) {
	LinterDB *db = LinterDB::get_singleton();
	if (!db) {
		return Dictionary();
	}

	String domain = p_params.get("domain", "");

	LocalVector<StringName> all_classes;
	db->get_class_list(all_classes);

	// Group classes by their parent for a tree overview.
	Array categories;
	int total = 0;

	for (const StringName &cn : all_classes) {
		if (!domain.is_empty() && _classify_domain(cn, db) != domain) {
			continue;
		}
		total++;

		Dictionary entry;
		entry["name"] = String(cn);
		entry["extends"] = String(db->get_parent_class(cn));

		// Count members (methods + properties + signals).
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

	Dictionary result;
	result["total"] = total;
	result["categories"] = categories;
	return result;
}

// ---------------------------------------------------------------------------
// api/globals
// ---------------------------------------------------------------------------

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
