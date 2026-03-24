/**************************************************************************/
/*  formatter.h                                                           */
/**************************************************************************/
/*  Token-budget-aware output formatting for LSPA responses.              */
/*  Supports names_only/compact, standard, and full/verbose detail levels.*/
/**************************************************************************/

#pragma once

#ifdef HOMOT

#include "../stubs/linterdb.h"

#include "core/variant/array.h"
#include "core/variant/dictionary.h"

namespace lspa {

using linter::ClassData;
using linter::DocClassData;
using linter::DocMethodData;
using linter::DocPropertyData;
using linter::LinterDB;
using linter::MethodData;
using linter::PropertyData;

// Detail levels for class info formatting.
enum DetailLevel {
	DETAIL_NAMES_ONLY, // Just member name lists (~200 tokens)
	DETAIL_STANDARD, // Names + signatures, skip empty descs (~800 tokens)
	DETAIL_FULL, // Full signatures + descriptions + docs (~2000+ tokens)
};

DetailLevel parse_detail_level(const String &p_str);

// Format a Variant::Type enum value to a readable type string.
String format_type(Variant::Type p_type, const StringName &p_class_name = StringName());

// Format a MethodInfo into a signature string like "(arg: Type, ...) -> ReturnType".
String format_method_signature(const MethodInfo &p_info);

// Format a single class at the given detail level.
// sections: if non-empty, only include these sections (e.g., ["methods", "signals"]).
Dictionary format_class(const StringName &p_class, DetailLevel p_detail, const Vector<String> &p_sections = Vector<String>());

// Format a search result entry.
Dictionary format_search_result(const StringName &p_class, const String &p_name, const String &p_kind, const MethodInfo *p_method_info = nullptr, const PropertyInfo *p_property_info = nullptr);

} // namespace lspa

#endif // HOMOT
