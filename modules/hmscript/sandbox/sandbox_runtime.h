/**************************************************************************/
/*  sandbox_runtime.h                                                     */
/**************************************************************************/

#pragma once

#include "sandbox_config.h"
#include "sandbox_error.h"
#include "sandbox_limiter.h"

#include "core/string/ustring.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

class Node;
class Script;

namespace hmsandbox {

// 轻量运行时聚合器，将配置、限流和错误仓库组合在一起。
// 不直接修改 GDScript 内部，只作为 HMScript 等上层入口的工具类。
class HMSandboxRuntime {
public:
	HMSandboxRuntime();

	HMSandboxConfig &get_config() { return config; }
	const HMSandboxConfig &get_config() const { return config; }

	HMSandboxLimiter &get_limiter() { return limiter; }
	const HMSandboxLimiter &get_limiter() const { return limiter; }

	HMSandboxErrorRegistry &get_error_registry() { return errors; }
	const HMSandboxErrorRegistry &get_error_registry() const { return errors; }

	void set_timeout_ms(int p_ms);
	void set_memory_limit_mb(int p_mb);
	void set_write_ops_per_frame(int p_count);
	void set_heavy_ops_per_frame(int p_count);
	void reset_frame_counters();

	// 简单入口：在受控环境中调用脚本函数。
	// 注意：这里只负责限流与错误记录，不拦截脚本内部所有调用。
	Variant call_script_function(const Ref<Script> &p_script,
			Object *p_owner,
			const StringName &p_method,
			const Array &p_args,
			String &r_error);

	// 记录来自外部的错误（例如自定义包装层捕获到的异常）。
	void add_error(const String &p_type,
			const String &p_message,
			const String &p_file = "",
			int p_line = 0,
			int p_column = 0,
			const String &p_stack_trace = "",
			const String &p_severity = "error",
			const String &p_trigger_context = "",
			const String &p_phase = "");

	String get_last_error() const { return errors.get_last_error(); }
	Array get_all_errors() const { return errors.get_all_errors(); }
	String get_error_report_markdown() const { return errors.get_error_report_markdown(); }

private:
	HMSandboxConfig config;
	HMSandboxLimiter limiter;
	HMSandboxErrorRegistry errors;
};

} // namespace hmsandbox

