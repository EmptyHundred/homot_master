/**************************************************************************/
/*  scene_resource_linter.h                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                              HOMOT ENGINE                              */
/**************************************************************************/

#pragma once

#include "core/io/resource.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/list.h"
#include "core/variant/variant_parser.h"

class SceneResourceLinter {
public:
	struct LintError {
		int line = 0;
		int column = 0;
		String message;
	};

	// Validate a .tscn or .tres file. Returns the number of errors found.
	static int validate(const String &p_path, List<LintError> &r_errors, List<LintError> &r_warnings);

	// Collect .tscn and .tres files recursively from a directory.
	static PackedStringArray collect_resource_files(const String &p_dir_path);

private:
	struct ResourceRef {
		String id;
		int line = 0;
	};

	struct LintParserData {
		HashSet<String> *declared_ext_ids = nullptr;
		HashSet<String> *declared_sub_ids = nullptr;
		Vector<ResourceRef> *referenced_ext_ids = nullptr;
		Vector<ResourceRef> *referenced_sub_ids = nullptr;
	};

	static Error _parse_ext_resource_lint(void *p_self, VariantParser::Stream *p_stream, Ref<Resource> &r_res, int &line, String &r_err_str);
	static Error _parse_sub_resource_lint(void *p_self, VariantParser::Stream *p_stream, Ref<Resource> &r_res, int &line, String &r_err_str);

	static void _validate_class_properties(const StringName &p_class, const HashMap<StringName, int> &p_properties, bool p_has_script, bool p_has_instance, List<LintError> &r_warnings);
	static void _collect_resource_files_recursive(const String &p_directory, PackedStringArray &r_result);
};
