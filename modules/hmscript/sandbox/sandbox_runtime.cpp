/**************************************************************************/
/*  sandbox_runtime.cpp                                                   */
/**************************************************************************/

#include "sandbox_runtime.h"

#include "core/io/resource_loader.h"
#include "core/object/script_language.h"
#include "core/os/os.h"

namespace hmsandbox {

void HMSandboxRuntime::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_timeout_ms", "ms"), &HMSandboxRuntime::set_timeout_ms);
	ClassDB::bind_method(D_METHOD("set_memory_limit_mb", "mb"), &HMSandboxRuntime::set_memory_limit_mb);
	ClassDB::bind_method(D_METHOD("set_write_ops_per_frame", "count"), &HMSandboxRuntime::set_write_ops_per_frame);
	ClassDB::bind_method(D_METHOD("set_heavy_ops_per_frame", "count"), &HMSandboxRuntime::set_heavy_ops_per_frame);
	ClassDB::bind_method(D_METHOD("reset_frame_counters"), &HMSandboxRuntime::reset_frame_counters);

	ClassDB::bind_method(D_METHOD("load_sandbox", "directory", "tscn_filename"), &HMSandboxRuntime::load_sandbox);

	ClassDB::bind_method(D_METHOD("get_last_error"), &HMSandboxRuntime::get_last_error);
	ClassDB::bind_method(D_METHOD("get_all_errors"), &HMSandboxRuntime::get_all_errors);
	ClassDB::bind_method(D_METHOD("get_error_report_markdown"), &HMSandboxRuntime::get_error_report_markdown);
}

HMSandboxRuntime::HMSandboxRuntime() {
}

void HMSandboxRuntime::set_timeout_ms(int p_ms) {
	limiter.set_timeout_ms(p_ms);
}

void HMSandboxRuntime::set_memory_limit_mb(int p_mb) {
	limiter.set_memory_limit_mb(p_mb);
}

void HMSandboxRuntime::set_write_ops_per_frame(int p_count) {
	limiter.set_write_ops_per_frame(p_count);
}

void HMSandboxRuntime::set_heavy_ops_per_frame(int p_count) {
	limiter.set_heavy_ops_per_frame(p_count);
}

void HMSandboxRuntime::reset_frame_counters() {
	limiter.reset_frame_counters();
}

Variant HMSandboxRuntime::call_script_function(const Ref<Script> &p_script,
		Object *p_owner,
		const StringName &p_method,
		const Array &p_args,
		String &r_error) {
	r_error = String();

	if (p_script.is_null() || p_owner == nullptr) {
		r_error = "Invalid script or owner.";
		add_error("sandbox", r_error);
		return Variant();
	}

	if (limiter.is_timeout_exceeded()) {
		r_error = "Sandbox timeout exceeded before call.";
		add_error("timeout", r_error);
		return Variant();
	}

	if (limiter.is_memory_limit_exceeded()) {
		r_error = "Sandbox memory limit exceeded before call.";
		add_error("memory", r_error);
		return Variant();
	}

	limiter.begin_execution();

	const int argc = p_args.size();
	Vector<const Variant *> arg_ptrs;
	arg_ptrs.resize(argc);
	for (int i = 0; i < argc; i++) {
		arg_ptrs.write[i] = &p_args[i];
	}

	Callable::CallError ce;
	const Variant **arg_array = (argc > 0) ? const_cast<const Variant **>(arg_ptrs.ptr()) : nullptr;
	Variant ret = p_owner->callp(p_method, arg_array, argc, ce);

	limiter.end_execution();

	if (limiter.is_timeout_exceeded()) {
		r_error = "Sandbox timeout exceeded during call.";
		add_error("timeout", r_error);
		return Variant();
	}

	if (limiter.is_memory_limit_exceeded()) {
		r_error = "Sandbox memory limit exceeded during call.";
		add_error("memory", r_error);
		return Variant();
	}

	if (ce.error != Callable::CallError::CALL_OK) {
		const Variant **err_array = (argc > 0) ? const_cast<const Variant **>(arg_ptrs.ptr()) : nullptr;
		r_error = Variant::get_call_error_text(p_owner, p_method, err_array, argc, ce);
		add_error("gdscript", r_error);
		return Variant();
	}

	return ret;
}

Ref<Resource> HMSandboxRuntime::load_sandbox(const String &p_directory, const String &p_tscn_filename) {
	// Construct the full path by combining directory and filename
	String full_path = p_directory;
	if (!full_path.is_empty() && !full_path.ends_with("/")) {
		full_path += "/";
	}
	full_path += p_tscn_filename;

	// Load the resource using ResourceLoader without cache
	Error err = OK;
	Ref<Resource> resource = ResourceLoader::load(full_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);

	if (err != OK || resource.is_null()) {
		add_error("resource_load", "Failed to load resource: " + full_path);
		return Ref<Resource>();
	}

	return resource;
}

void HMSandboxRuntime::add_error(const String &p_type,
		const String &p_message,
		const String &p_file,
		int p_line,
		int p_column,
		const String &p_stack_trace,
		const String &p_severity,
		const String &p_trigger_context,
		const String &p_phase) {
	errors.add_error(p_type, p_message, p_file, p_line, p_column, p_stack_trace, p_severity, p_trigger_context, p_phase);
}

} // namespace hmsandbox

