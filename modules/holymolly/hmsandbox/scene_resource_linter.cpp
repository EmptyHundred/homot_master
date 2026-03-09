/**************************************************************************/
/*  scene_resource_linter.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                              HOMOT ENGINE                              */
/**************************************************************************/

#include "scene_resource_linter.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"

// ---------------------------------------------------------------------------
// Resource parser callbacks – capture ExtResource/SubResource references
// without actually loading anything.
// ---------------------------------------------------------------------------

Error SceneResourceLinter::_parse_ext_resource_lint(void *p_self, VariantParser::Stream *p_stream, Ref<Resource> &r_res, int &line, String &r_err_str) {
	LintParserData *data = static_cast<LintParserData *>(p_self);

	VariantParser::Token token;
	VariantParser::get_token(p_stream, token, line, r_err_str);
	if (token.type != VariantParser::TK_NUMBER && token.type != VariantParser::TK_STRING) {
		r_err_str = "Expected number or string for ExtResource id";
		return ERR_PARSE_ERROR;
	}

	String id = token.value;
	data->referenced_ext_ids->push_back({ id, line });

	VariantParser::get_token(p_stream, token, line, r_err_str);
	if (token.type != VariantParser::TK_PARENTHESIS_CLOSE) {
		r_err_str = "Expected ')'";
		return ERR_PARSE_ERROR;
	}

	r_res.unref();
	return OK;
}

Error SceneResourceLinter::_parse_sub_resource_lint(void *p_self, VariantParser::Stream *p_stream, Ref<Resource> &r_res, int &line, String &r_err_str) {
	LintParserData *data = static_cast<LintParserData *>(p_self);

	VariantParser::Token token;
	VariantParser::get_token(p_stream, token, line, r_err_str);
	if (token.type != VariantParser::TK_NUMBER && token.type != VariantParser::TK_STRING) {
		r_err_str = "Expected number or string for SubResource id";
		return ERR_PARSE_ERROR;
	}

	String id = token.value;
	data->referenced_sub_ids->push_back({ id, line });

	VariantParser::get_token(p_stream, token, line, r_err_str);
	if (token.type != VariantParser::TK_PARENTHESIS_CLOSE) {
		r_err_str = "Expected ')'";
		return ERR_PARSE_ERROR;
	}

	r_res.unref();
	return OK;
}

// ---------------------------------------------------------------------------
// Property validation against ClassDB
// ---------------------------------------------------------------------------

void SceneResourceLinter::_validate_class_properties(const StringName &p_class, const HashMap<StringName, int> &p_properties, bool p_has_script, bool p_has_instance, List<LintError> &r_warnings) {
	if (!ClassDB::class_exists(p_class)) {
		return;
	}
	// When a script or instance is attached, custom properties may exist that
	// are not in ClassDB.  Skip property validation to avoid false positives.
	if (p_has_script || p_has_instance) {
		return;
	}

	for (const KeyValue<StringName, int> &kv : p_properties) {
		const String prop_name = kv.key;

		// Skip special / dynamic property prefixes.
		if (prop_name == "script") {
			continue;
		}
		if (prop_name.begins_with("metadata/")) {
			continue;
		}
		if (prop_name.begins_with("editor/")) {
			continue;
		}

		if (!ClassDB::has_property(p_class, kv.key)) {
			LintError warn;
			warn.line = kv.value;
			warn.message = vformat("Property '%s' not found in class '%s'.", prop_name, p_class);
			r_warnings.push_back(warn);
		}
	}
}

// ---------------------------------------------------------------------------
// File collection
// ---------------------------------------------------------------------------

void SceneResourceLinter::_collect_resource_files_recursive(const String &p_directory, PackedStringArray &r_result) {
	Ref<DirAccess> dir = DirAccess::open(p_directory);
	if (dir.is_null()) {
		return;
	}

	dir->list_dir_begin();
	String file_name = dir->get_next();

	while (!file_name.is_empty()) {
		if (file_name.begins_with(".")) {
			file_name = dir->get_next();
			continue;
		}

		String full_path = p_directory;
		if (!full_path.ends_with("/")) {
			full_path += "/";
		}
		full_path += file_name;

		if (dir->current_is_dir()) {
			_collect_resource_files_recursive(full_path, r_result);
		} else {
			if (file_name.ends_with(".tscn") || file_name.ends_with(".tres")) {
				r_result.push_back(full_path);
			}
		}

		file_name = dir->get_next();
	}

	dir->list_dir_end();
}

PackedStringArray SceneResourceLinter::collect_resource_files(const String &p_dir_path) {
	PackedStringArray result;
	if (p_dir_path.is_empty()) {
		return result;
	}
	_collect_resource_files_recursive(p_dir_path, result);
	return result;
}

static void _collect_shader_files_recursive(const String &p_directory, PackedStringArray &r_result) {
	Ref<DirAccess> dir = DirAccess::open(p_directory);
	if (dir.is_null()) {
		return;
	}

	dir->list_dir_begin();
	String file_name = dir->get_next();

	while (!file_name.is_empty()) {
		if (file_name.begins_with(".")) {
			file_name = dir->get_next();
			continue;
		}

		String full_path = p_directory;
		if (!full_path.ends_with("/")) {
			full_path += "/";
		}
		full_path += file_name;

		if (dir->current_is_dir()) {
			_collect_shader_files_recursive(full_path, r_result);
		} else {
			if (file_name.ends_with(".gdshader")) {
				r_result.push_back(full_path);
			}
		}

		file_name = dir->get_next();
	}

	dir->list_dir_end();
}

PackedStringArray SceneResourceLinter::collect_shader_files(const String &p_dir_path) {
	PackedStringArray result;
	if (p_dir_path.is_empty()) {
		return result;
	}
	_collect_shader_files_recursive(p_dir_path, result);
	return result;
}

// ---------------------------------------------------------------------------
// Main validation entry point
// ---------------------------------------------------------------------------

int SceneResourceLinter::validate(const String &p_path, List<LintError> &r_errors, List<LintError> &r_warnings) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	if (f.is_null()) {
		LintError e;
		e.line = 0;
		e.message = "Cannot open file.";
		r_errors.push_back(e);
		return 1;
	}

	VariantParser::StreamFile stream;
	stream.f = f;
	int lines = 1;
	String error_text;
	VariantParser::Tag next_tag;

	// Tracking data.
	HashSet<String> declared_ext_ids;
	HashSet<String> declared_sub_ids;
	Vector<ResourceRef> referenced_ext_ids;
	Vector<ResourceRef> referenced_sub_ids;

	LintParserData parser_data;
	parser_data.declared_ext_ids = &declared_ext_ids;
	parser_data.declared_sub_ids = &declared_sub_ids;
	parser_data.referenced_ext_ids = &referenced_ext_ids;
	parser_data.referenced_sub_ids = &referenced_sub_ids;

	VariantParser::ResourceParser rp;
	rp.ext_func = _parse_ext_resource_lint;
	rp.sub_func = _parse_sub_resource_lint;
	rp.userdata = &parser_data;

	// -----------------------------------------------------------------------
	// Header
	// -----------------------------------------------------------------------

	Error err = VariantParser::parse_tag(&stream, lines, error_text, next_tag);
	if (err) {
		LintError e;
		e.line = lines;
		e.message = "Failed to parse header: " + error_text;
		r_errors.push_back(e);
		return r_errors.size();
	}

	bool is_scene = false;
	String resource_type;

	if (next_tag.name == "gd_scene") {
		is_scene = true;
	} else if (next_tag.name == "gd_resource") {
		if (!next_tag.fields.has("type")) {
			LintError e;
			e.line = lines;
			e.message = "Missing 'type' field in 'gd_resource' header.";
			r_errors.push_back(e);
		} else {
			resource_type = next_tag.fields["type"];
			if (!ClassDB::class_exists(resource_type)) {
				LintError e;
				e.line = lines;
				e.message = vformat("Unknown resource type '%s'.", resource_type);
				r_errors.push_back(e);
			} else if (!ClassDB::is_parent_class(resource_type, "Resource")) {
				LintError e;
				e.line = lines;
				e.message = vformat("Type '%s' is not a Resource type.", resource_type);
				r_errors.push_back(e);
			}
		}
	} else {
		LintError e;
		e.line = lines;
		e.message = vformat("Unrecognized file type '%s'. Expected 'gd_scene' or 'gd_resource'.", next_tag.name);
		r_errors.push_back(e);
		return r_errors.size();
	}

	if (next_tag.fields.has("format")) {
		int format = next_tag.fields["format"];
		if (format > 4) {
			LintError e;
			e.line = lines;
			e.message = vformat("Format version %d is newer than supported version 4.", format);
			r_errors.push_back(e);
		}
	}

	// Parse the first body tag.
	err = VariantParser::parse_tag(&stream, lines, error_text, next_tag, &rp);
	if (err) {
		if (err == ERR_FILE_EOF) {
			goto validate_references;
		}
		LintError e;
		e.line = lines;
		e.message = "Parse error after header: " + error_text;
		r_errors.push_back(e);
		return r_errors.size();
	}

	// -----------------------------------------------------------------------
	// ext_resource section
	// -----------------------------------------------------------------------

	while (next_tag.name == "ext_resource") {
		int tag_line = lines;

		if (!next_tag.fields.has("path")) {
			LintError e;
			e.line = tag_line;
			e.message = "Missing 'path' in ext_resource.";
			r_errors.push_back(e);
		}
		if (!next_tag.fields.has("type")) {
			LintError e;
			e.line = tag_line;
			e.message = "Missing 'type' in ext_resource.";
			r_errors.push_back(e);
		}
		if (!next_tag.fields.has("id")) {
			LintError e;
			e.line = tag_line;
			e.message = "Missing 'id' in ext_resource.";
			r_errors.push_back(e);
		} else {
			String id = next_tag.fields["id"];
			if (declared_ext_ids.has(id)) {
				LintError e;
				e.line = tag_line;
				e.message = vformat("Duplicate ext_resource id '%s'.", id);
				r_errors.push_back(e);
			}
			declared_ext_ids.insert(id);
		}

		err = VariantParser::parse_tag(&stream, lines, error_text, next_tag, &rp);
		if (err) {
			if (err == ERR_FILE_EOF) {
				goto validate_references;
			}
			LintError e;
			e.line = lines;
			e.message = "Parse error in ext_resource section: " + error_text;
			r_errors.push_back(e);
			return r_errors.size();
		}
	}

	// -----------------------------------------------------------------------
	// sub_resource section
	// -----------------------------------------------------------------------

	while (next_tag.name == "sub_resource") {
		int tag_line = lines;
		String sub_type;

		if (!next_tag.fields.has("type")) {
			LintError e;
			e.line = tag_line;
			e.message = "Missing 'type' in sub_resource.";
			r_errors.push_back(e);
		} else {
			sub_type = next_tag.fields["type"];
			if (!ClassDB::class_exists(sub_type)) {
				LintError e;
				e.line = tag_line;
				e.message = vformat("Unknown sub_resource type '%s'.", sub_type);
				r_errors.push_back(e);
			} else if (!ClassDB::is_parent_class(sub_type, "Resource")) {
				LintError e;
				e.line = tag_line;
				e.message = vformat("sub_resource type '%s' is not a Resource.", sub_type);
				r_errors.push_back(e);
			}
		}

		if (!next_tag.fields.has("id")) {
			LintError e;
			e.line = tag_line;
			e.message = "Missing 'id' in sub_resource.";
			r_errors.push_back(e);
		} else {
			String id = next_tag.fields["id"];
			if (declared_sub_ids.has(id)) {
				LintError e;
				e.line = tag_line;
				e.message = vformat("Duplicate sub_resource id '%s'.", id);
				r_errors.push_back(e);
			}
			declared_sub_ids.insert(id);
		}

		// Parse sub_resource properties.
		HashMap<StringName, int> properties;
		while (true) {
			String assign;
			Variant value;
			err = VariantParser::parse_tag_assign_eof(&stream, lines, error_text, next_tag, assign, value, &rp);
			if (err) {
				if (err == ERR_FILE_EOF) {
					goto validate_sub_props;
				}
				LintError e;
				e.line = lines;
				e.message = "Parse error in sub_resource properties: " + error_text;
				r_errors.push_back(e);
				return r_errors.size();
			}
			if (!assign.is_empty()) {
				properties[assign] = lines;
			} else if (!next_tag.name.is_empty()) {
				break;
			}
		}

	validate_sub_props:
		if (!sub_type.is_empty() && ClassDB::class_exists(sub_type) && ClassDB::is_parent_class(sub_type, "Resource")) {
			_validate_class_properties(sub_type, properties, false, false, r_warnings);
		}

		if (err == ERR_FILE_EOF) {
			goto validate_references;
		}
	}

	// -----------------------------------------------------------------------
	// Scene file: nodes, connections, editable
	// -----------------------------------------------------------------------

	if (is_scene) {
		// Track node tree for path validation and signal checks.
		HashMap<String, StringName> node_path_to_type;
		bool has_root = false;

		while (next_tag.name == "node") {
			int tag_line = lines;
			String node_name;
			String node_type;
			bool has_parent_field = next_tag.fields.has("parent");
			bool has_instance = next_tag.fields.has("instance") || next_tag.fields.has("instance_placeholder");

			// -- name --
			if (!next_tag.fields.has("name")) {
				LintError e;
				e.line = tag_line;
				e.message = "Missing 'name' in node tag.";
				r_errors.push_back(e);
			} else {
				node_name = next_tag.fields["name"];
			}

			// -- type (Layer 2) --
			if (next_tag.fields.has("type")) {
				node_type = next_tag.fields["type"];
				if (!ClassDB::class_exists(node_type)) {
					LintError e;
					e.line = tag_line;
					e.message = vformat("Unknown node type '%s'.", node_type);
					r_errors.push_back(e);
				} else if (!ClassDB::is_parent_class(node_type, "Node")) {
					LintError e;
					e.line = tag_line;
					e.message = vformat("Type '%s' is not a Node type.", node_type);
					r_errors.push_back(e);
				}
			}

			// -- parent / node path --
			if (!has_parent_field) {
				// Root node.
				if (has_root) {
					LintError e;
					e.line = tag_line;
					e.message = "Multiple root nodes found (node without 'parent' field).";
					r_errors.push_back(e);
				}
				has_root = true;
				node_path_to_type.insert(".", node_type);
			} else {
				String parent_path = next_tag.fields["parent"];

				if (parent_path != "." && !node_path_to_type.has(parent_path)) {
					LintError warn;
					warn.line = tag_line;
					warn.message = vformat("Node '%s' references parent path '%s' which hasn't been declared.", node_name, parent_path);
					r_warnings.push_back(warn);
				}

				if (!node_name.is_empty()) {
					String full_path = (parent_path == ".") ? node_name : (parent_path + "/" + node_name);
					node_path_to_type.insert(full_path, node_type);
				}
			}

			// -- properties --
			HashMap<StringName, int> properties;
			bool has_script = false;
			while (true) {
				String assign;
				Variant value;
				err = VariantParser::parse_tag_assign_eof(&stream, lines, error_text, next_tag, assign, value, &rp);
				if (err) {
					if (err == ERR_FILE_EOF) {
						goto validate_node_props;
					}
					LintError e;
					e.line = lines;
					e.message = "Parse error in node properties: " + error_text;
					r_errors.push_back(e);
					return r_errors.size();
				}
				if (!assign.is_empty()) {
					if (assign == "script") {
						has_script = true;
					}
					properties[assign] = lines;
				} else if (!next_tag.name.is_empty()) {
					break;
				}
			}

		validate_node_props:
			if (!node_type.is_empty() && ClassDB::class_exists(node_type) && ClassDB::is_parent_class(node_type, "Node")) {
				_validate_class_properties(node_type, properties, has_script, has_instance, r_warnings);
			}

			if (err == ERR_FILE_EOF) {
				goto validate_references;
			}
		}

		if (!has_root && next_tag.name.is_empty()) {
			LintError warn;
			warn.line = 1;
			warn.message = "Scene file has no root node.";
			r_warnings.push_back(warn);
		}

		// -- connections --
		while (next_tag.name == "connection") {
			int tag_line = lines;

			if (!next_tag.fields.has("from")) {
				LintError e;
				e.line = tag_line;
				e.message = "Missing 'from' in connection.";
				r_errors.push_back(e);
			} else {
				String from_path = next_tag.fields["from"];
				if (from_path != "." && !node_path_to_type.has(from_path)) {
					LintError warn;
					warn.line = tag_line;
					warn.message = vformat("Connection 'from' references unknown node path '%s'.", from_path);
					r_warnings.push_back(warn);
				}

				// Layer 2: validate signal on source node type.
				if (next_tag.fields.has("signal")) {
					StringName signal_name = next_tag.fields["signal"];
					String lookup = (from_path == ".") ? String(".") : from_path;
					if (node_path_to_type.has(lookup)) {
						StringName from_type = node_path_to_type[lookup];
						if (!from_type.is_empty() && ClassDB::class_exists(from_type)) {
							if (!ClassDB::has_signal(from_type, signal_name)) {
								LintError warn;
								warn.line = tag_line;
								warn.message = vformat("Signal '%s' not found in class '%s'.", signal_name, from_type);
								r_warnings.push_back(warn);
							}
						}
					}
				}
			}

			if (!next_tag.fields.has("to")) {
				LintError e;
				e.line = tag_line;
				e.message = "Missing 'to' in connection.";
				r_errors.push_back(e);
			} else {
				String to_path = next_tag.fields["to"];
				if (to_path != "." && !node_path_to_type.has(to_path)) {
					LintError warn;
					warn.line = tag_line;
					warn.message = vformat("Connection 'to' references unknown node path '%s'.", to_path);
					r_warnings.push_back(warn);
				}
			}

			if (!next_tag.fields.has("signal")) {
				LintError e;
				e.line = tag_line;
				e.message = "Missing 'signal' in connection.";
				r_errors.push_back(e);
			}
			if (!next_tag.fields.has("method")) {
				LintError e;
				e.line = tag_line;
				e.message = "Missing 'method' in connection.";
				r_errors.push_back(e);
			}

			err = VariantParser::parse_tag(&stream, lines, error_text, next_tag, &rp);
			if (err) {
				if (err == ERR_FILE_EOF) {
					goto validate_references;
				}
				LintError e;
				e.line = lines;
				e.message = "Parse error in connection section: " + error_text;
				r_errors.push_back(e);
				return r_errors.size();
			}
		}

		// -- editable --
		while (next_tag.name == "editable") {
			if (!next_tag.fields.has("path")) {
				LintError e;
				e.line = lines;
				e.message = "Missing 'path' in editable tag.";
				r_errors.push_back(e);
			}

			err = VariantParser::parse_tag(&stream, lines, error_text, next_tag, &rp);
			if (err) {
				if (err == ERR_FILE_EOF) {
					goto validate_references;
				}
				LintError e;
				e.line = lines;
				e.message = "Parse error in editable section: " + error_text;
				r_errors.push_back(e);
				return r_errors.size();
			}
		}

		// Unknown trailing tag.
		if (!next_tag.name.is_empty()) {
			LintError e;
			e.line = lines;
			e.message = vformat("Unknown tag '%s' in scene file.", next_tag.name);
			r_errors.push_back(e);
		}

	} else {
		// -------------------------------------------------------------------
		// Resource file (.tres): [resource] section
		// -------------------------------------------------------------------

		if (next_tag.name == "resource") {
			HashMap<StringName, int> properties;
			while (true) {
				String assign;
				Variant value;
				err = VariantParser::parse_tag_assign_eof(&stream, lines, error_text, next_tag, assign, value, &rp);
				if (err) {
					if (err == ERR_FILE_EOF) {
						goto validate_res_props;
					}
					LintError e;
					e.line = lines;
					e.message = "Parse error in resource properties: " + error_text;
					r_errors.push_back(e);
					return r_errors.size();
				}
				if (!assign.is_empty()) {
					properties[assign] = lines;
				} else if (!next_tag.name.is_empty()) {
					break;
				}
			}

		validate_res_props:
			if (!resource_type.is_empty() && ClassDB::class_exists(resource_type)) {
				_validate_class_properties(resource_type, properties, false, false, r_warnings);
			}

			if (err != ERR_FILE_EOF && !next_tag.name.is_empty()) {
				LintError e;
				e.line = lines;
				e.message = vformat("Unexpected tag '%s' after [resource] section.", next_tag.name);
				r_errors.push_back(e);
			}
		} else if (next_tag.name == "node") {
			LintError e;
			e.line = lines;
			e.message = "'node' tag found in a resource file (.tres). Did you mean to use .tscn?";
			r_errors.push_back(e);
		} else if (!next_tag.name.is_empty() && next_tag.name != "sub_resource" && next_tag.name != "ext_resource") {
			LintError e;
			e.line = lines;
			e.message = vformat("Expected 'resource' tag, found '%s'.", next_tag.name);
			r_errors.push_back(e);
		}
	}

	// -----------------------------------------------------------------------
	// Cross-reference validation
	// -----------------------------------------------------------------------

validate_references:
	for (const ResourceRef &ref : referenced_ext_ids) {
		if (!declared_ext_ids.has(ref.id)) {
			LintError e;
			e.line = ref.line;
			e.message = vformat("ExtResource references undeclared id '%s'.", ref.id);
			r_errors.push_back(e);
		}
	}

	for (const ResourceRef &ref : referenced_sub_ids) {
		if (!declared_sub_ids.has(ref.id)) {
			LintError e;
			e.line = ref.line;
			e.message = vformat("SubResource references undeclared id '%s'.", ref.id);
			r_errors.push_back(e);
		}
	}

	return r_errors.size();
}
