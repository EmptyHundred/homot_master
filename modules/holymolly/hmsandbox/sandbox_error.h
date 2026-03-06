/**************************************************************************/
/*  sandbox_error.h                                                       */
/**************************************************************************/

#pragma once

#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"

namespace hmsandbox {

// 结构化错误条目，用于为编辑器 / AI 提供可消费的数据。
struct HMSandboxErrorEntry {
	String id;
	String type;
	String severity;
	String message;
	String file;
	int line = 0;
	int column = 0;
	String stack_trace;
	String trigger_context;
	String phase;
	int64_t timestamp = 0;
	int64_t last_occurrence = 0;
	int occurrence_count = 1;

	Dictionary to_dict() const;

	static String compute_id(const String &p_type,
			const String &p_message,
			const String &p_file,
			int p_line);
};

// 简单错误仓库，负责去重与统计。
class HMSandboxErrorRegistry {
public:
	void add_error(const String &p_type,
			const String &p_message,
			const String &p_file = "",
			int p_line = 0,
			int p_column = 0,
			const String &p_stack_trace = "",
			const String &p_severity = "error",
			const String &p_trigger_context = "",
			const String &p_phase = "");

	void clear();

	String get_last_error() const { return last_error; }
	Array get_all_errors() const;
	String get_error_report_markdown() const;

private:
	HashMap<String, HMSandboxErrorEntry> error_map;
	Vector<String> error_order;
	String last_error;
};

} // namespace hmsandbox

