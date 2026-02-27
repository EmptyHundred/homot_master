#include "hmscript_language.h"

#include "sandbox/sandbox_runtime.h"

using namespace hmsandbox;

// 全局 HMSandbox 运行时实例，目前简单实现为单例。
static HMSandboxRuntime g_hm_sandbox_runtime;

static void _hm_sandbox_frame_callback() {
	// 每帧重置写入/重操作配额等。
	g_hm_sandbox_runtime.reset_frame_counters();
}

HMSandboxRuntime *HMScriptLanguage::get_sandbox_runtime() {
	return &g_hm_sandbox_runtime;
}

String HMScriptLanguage::get_name() const {
	return "HMScript";
}

void HMScriptLanguage::init() {
	// 复用现有 GDScript 运行时时，同时注册沙盒帧回调。
	if (GDScriptLanguage::get_singleton()) {
		GDScriptLanguage::get_singleton()->set_sandbox_frame_callback(_hm_sandbox_frame_callback);
	}
}

String HMScriptLanguage::get_type() const {
	return "HMScript";
}

String HMScriptLanguage::get_extension() const {
	// Primary text extension for HMScript files.
	return "hm";
}

void HMScriptLanguage::finish() {
	// Nothing to clean up, actual runtime is managed by GDScriptLanguage.
}

bool HMScriptLanguage::is_using_templates() {
	return GDScriptLanguage::get_singleton()->is_using_templates();
}

Vector<String> HMScriptLanguage::get_reserved_words() const {
	return GDScriptLanguage::get_singleton()->get_reserved_words();
}

bool HMScriptLanguage::is_control_flow_keyword(const String &p_string) const {
	return GDScriptLanguage::get_singleton()->is_control_flow_keyword(p_string);
}

Vector<String> HMScriptLanguage::get_comment_delimiters() const {
	return GDScriptLanguage::get_singleton()->get_comment_delimiters();
}

Vector<String> HMScriptLanguage::get_doc_comment_delimiters() const {
	return GDScriptLanguage::get_singleton()->get_doc_comment_delimiters();
}

Vector<String> HMScriptLanguage::get_string_delimiters() const {
	return GDScriptLanguage::get_singleton()->get_string_delimiters();
}

bool HMScriptLanguage::validate(const String &p_script, const String &p_path, List<String> *r_functions, List<ScriptError> *r_errors, List<Warning> *r_warnings, HashSet<int> *r_safe_lines) const {
	return GDScriptLanguage::get_singleton()->validate(p_script, p_path, r_functions, r_errors, r_warnings, r_safe_lines);
}

bool HMScriptLanguage::supports_builtin_mode() const {
	return GDScriptLanguage::get_singleton()->supports_builtin_mode();
}

int HMScriptLanguage::find_function(const String &p_function, const String &p_code) const {
	return GDScriptLanguage::get_singleton()->find_function(p_function, p_code);
}

Ref<Script> HMScriptLanguage::make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const {
	return GDScriptLanguage::get_singleton()->make_template(p_template, p_class_name, p_base_class_name);
}

String HMScriptLanguage::make_function(const String &p_class, const String &p_name, const PackedStringArray &p_args) const {
	return GDScriptLanguage::get_singleton()->make_function(p_class, p_name, p_args);
}

Vector<ScriptLanguage::ScriptTemplate> HMScriptLanguage::get_built_in_templates(const StringName &p_object) {
	return GDScriptLanguage::get_singleton()->get_built_in_templates(p_object);
}

void HMScriptLanguage::auto_indent_code(String &p_code, int p_from_line, int p_to_line) const {
	GDScriptLanguage::get_singleton()->auto_indent_code(p_code, p_from_line, p_to_line);
}

void HMScriptLanguage::add_global_constant(const StringName &p_variable, const Variant &p_value) {
	GDScriptLanguage::get_singleton()->add_global_constant(p_variable, p_value);
}

String HMScriptLanguage::debug_get_error() const {
	return GDScriptLanguage::get_singleton()->debug_get_error();
}

int HMScriptLanguage::debug_get_stack_level_count() const {
	return GDScriptLanguage::get_singleton()->debug_get_stack_level_count();
}

int HMScriptLanguage::debug_get_stack_level_line(int p_level) const {
	return GDScriptLanguage::get_singleton()->debug_get_stack_level_line(p_level);
}

String HMScriptLanguage::debug_get_stack_level_function(int p_level) const {
	return GDScriptLanguage::get_singleton()->debug_get_stack_level_function(p_level);
}

String HMScriptLanguage::debug_get_stack_level_source(int p_level) const {
	return GDScriptLanguage::get_singleton()->debug_get_stack_level_source(p_level);
}

void HMScriptLanguage::debug_get_stack_level_locals(int p_level, List<String> *p_locals, List<Variant> *p_values, int p_max_subitems, int p_max_depth) {
	GDScriptLanguage::get_singleton()->debug_get_stack_level_locals(p_level, p_locals, p_values, p_max_subitems, p_max_depth);
}

void HMScriptLanguage::debug_get_stack_level_members(int p_level, List<String> *p_members, List<Variant> *p_values, int p_max_subitems, int p_max_depth) {
	GDScriptLanguage::get_singleton()->debug_get_stack_level_members(p_level, p_members, p_values, p_max_subitems, p_max_depth);
}

void HMScriptLanguage::debug_get_globals(List<String> *p_globals, List<Variant> *p_values, int p_max_subitems, int p_max_depth) {
	GDScriptLanguage::get_singleton()->debug_get_globals(p_globals, p_values, p_max_subitems, p_max_depth);
}

String HMScriptLanguage::debug_parse_stack_level_expression(int p_level, const String &p_expression, int p_max_subitems, int p_max_depth) {
	return GDScriptLanguage::get_singleton()->debug_parse_stack_level_expression(p_level, p_expression, p_max_subitems, p_max_depth);
}

void HMScriptLanguage::reload_all_scripts() {
	GDScriptLanguage::get_singleton()->reload_all_scripts();
}

void HMScriptLanguage::reload_scripts(const Array &p_scripts, bool p_soft_reload) {
	GDScriptLanguage::get_singleton()->reload_scripts(p_scripts, p_soft_reload);
}

void HMScriptLanguage::reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) {
	GDScriptLanguage::get_singleton()->reload_tool_script(p_script, p_soft_reload);
}

void HMScriptLanguage::get_recognized_extensions(List<String> *p_extensions) const {
	// HMScript source and compiled token formats.
	p_extensions->push_back("hm");
	p_extensions->push_back("hmc");
}

void HMScriptLanguage::get_public_functions(List<MethodInfo> *p_functions) const {
	GDScriptLanguage::get_singleton()->get_public_functions(p_functions);
}

void HMScriptLanguage::get_public_constants(List<Pair<String, Variant>> *p_constants) const {
	GDScriptLanguage::get_singleton()->get_public_constants(p_constants);
}

void HMScriptLanguage::get_public_annotations(List<MethodInfo> *p_annotations) const {
	GDScriptLanguage::get_singleton()->get_public_annotations(p_annotations);
}

void HMScriptLanguage::profiling_start() {
	GDScriptLanguage::get_singleton()->profiling_start();
}

void HMScriptLanguage::profiling_stop() {
	GDScriptLanguage::get_singleton()->profiling_stop();
}

void HMScriptLanguage::profiling_set_save_native_calls(bool p_enable) {
	GDScriptLanguage::get_singleton()->profiling_set_save_native_calls(p_enable);
}

int HMScriptLanguage::profiling_get_accumulated_data(ProfilingInfo *p_info_arr, int p_info_max) {
	return GDScriptLanguage::get_singleton()->profiling_get_accumulated_data(p_info_arr, p_info_max);
}

int HMScriptLanguage::profiling_get_frame_data(ProfilingInfo *p_info_arr, int p_info_max) {
	return GDScriptLanguage::get_singleton()->profiling_get_frame_data(p_info_arr, p_info_max);
}

Ref<Resource> ResourceFormatLoaderHMScript::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *p_progress, CacheMode p_cache_mode) {
	// 通过基础的 GDScript 加载器加载 `.hm` / `.hmc`，然后为得到的 GDScript 资源启用沙盒。
	Ref<Resource> res = ResourceFormatLoaderGDScript::load(p_path, p_original_path, r_error, p_use_sub_threads, p_progress, p_cache_mode);

	Ref<GDScript> gds = res;
	if (gds.is_valid()) {
		// 对所有 HMScript 脚本统一使用一个默认的 GDScript 沙盒 profile。
		// 后续如需支持多 profile，可在此根据路径或上层配置选择不同 ID。
		gds->set_sandbox_enabled(true, "hm_default");
	}

	return res;
}

void ResourceFormatLoaderHMScript::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("hm");
	p_extensions->push_back("hmc");
}

bool ResourceFormatLoaderHMScript::handles_type(const String &p_type) const {
	// Reuse the same underlying script resource type.
	return (p_type == "Script" || p_type == "GDScript" || p_type == "HMScript");
}

String ResourceFormatLoaderHMScript::get_resource_type(const String &p_path) const {
	const String el = p_path.get_extension().to_lower();
	if (el == "hm" || el == "hmc") {
		// Underlying resource is still a GDScript object.
		return "GDScript";
	}
	return String();
}

void ResourceFormatSaverHMScript::get_recognized_extensions(const Ref<Resource> &p_resource, List<String> *p_extensions) const {
	if (Object::cast_to<GDScript>(*p_resource)) {
		p_extensions->push_back("hm");
	}
}

