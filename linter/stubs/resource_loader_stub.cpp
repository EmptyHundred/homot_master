/**************************************************************************/
/*  resource_loader_stub.cpp                                              */
/**************************************************************************/
/*  Stub for ResourceLoader used by the GDScript analyzer.                */
/*  This file is compiled INSTEAD OF the real ResourceLoader              */
/*  implementation when building the linter target.                       */
/*                                                                        */
/*  The analyzer calls:                                                   */
/*    - ResourceLoader::exists(path)                                      */
/*    - ResourceLoader::load(path, type_hint, cache_mode, error)          */
/*    - ResourceLoader::get_resource_type(path)                           */
/*    - ResourceLoader::ensure_resource_ref_override_for_outer_load(...)  */
/**************************************************************************/

#ifdef HOMOT

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"

bool ResourceLoader::exists(const String &p_path, const String &p_type_hint) {
	return FileAccess::exists(p_path);
}

Ref<Resource> ResourceLoader::load(const String &p_path, const String &p_type_hint, ResourceFormatLoader::CacheMode p_cache_mode, Error *r_error) {
	// The linter cannot load actual resources.
	// Cross-script resolution is handled by the linter cache / sandbox registry.
	if (r_error) {
		*r_error = ERR_UNAVAILABLE;
	}
	return Ref<Resource>();
}

String ResourceLoader::get_resource_type(const String &p_path) {
	String ext = p_path.get_extension().to_lower();
	if (ext == "gd" || ext == "hm" || ext == "hmc") {
		return "GDScript";
	} else if (ext == "tscn") {
		return "PackedScene";
	} else if (ext == "tres") {
		return "Resource";
	} else if (ext == "res") {
		return "Resource";
	} else if (ext == "gdshader" || ext == "shader") {
		return "Shader";
	}
	return "";
}

Ref<Resource> ResourceLoader::ensure_resource_ref_override_for_outer_load(const String &p_path, const String &p_res_type) {
	return Ref<Resource>();
}

String ResourceLoader::path_remap(const String &p_path) {
	// No remapping in the linter — paths are used as-is.
	return p_path;
}

#endif // HOMOT
