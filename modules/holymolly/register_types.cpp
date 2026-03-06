/**************************************************************************/
/*  register_types.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "register_types.h"

#include "hmscript/hmscript_language.h"
#include "hmsandbox/sandbox_manager.h"
#include "hmsandbox/sandbox_runtime.h"

#include "core/config/engine.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/script_language.h"

HMScriptLanguage *script_language_hm = nullptr;
Ref<ResourceFormatLoaderHMScript> resource_loader_hm;
Ref<ResourceFormatSaverHMScript> resource_saver_hm;

namespace hmsandbox {
	HMSandboxManager *hm_sandbox_manager = nullptr;
}

using namespace hmsandbox;

void initialize_holymolly_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
		GDREGISTER_CLASS(HMSandbox);
		GDREGISTER_CLASS(HMSandboxManager);

		// Create and register the HMSandbox manager singleton
		hm_sandbox_manager = memnew(HMSandboxManager);
		Engine::get_singleton()->add_singleton(Engine::Singleton("HMSandboxManager", hm_sandbox_manager));

		script_language_hm = memnew(HMScriptLanguage);
		ScriptServer::register_language(script_language_hm);

		resource_loader_hm.instantiate();
		ResourceLoader::add_resource_format_loader(resource_loader_hm);

		resource_saver_hm.instantiate();
		ResourceSaver::add_resource_format_saver(resource_saver_hm);
	}
}

void uninitialize_holymolly_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
		ScriptServer::unregister_language(script_language_hm);

		if (script_language_hm) {
			memdelete(script_language_hm);
			script_language_hm = nullptr;
		}

		ResourceLoader::remove_resource_format_loader(resource_loader_hm);
		resource_loader_hm.unref();

		ResourceSaver::remove_resource_format_saver(resource_saver_hm);
		resource_saver_hm.unref();

		// Unregister and delete the HMSandbox manager singleton
		if (hm_sandbox_manager) {
			Engine::get_singleton()->remove_singleton("HMSandboxManager");
			memdelete(hm_sandbox_manager);
			hm_sandbox_manager = nullptr;
		}
	}
}

