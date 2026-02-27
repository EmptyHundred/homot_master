/**************************************************************************/
/*  sandbox_error.cpp                                                     */
/**************************************************************************/

#include "sandbox_error.h"

#include "core/os/os.h"

namespace hmsandbox {

Dictionary HMSandboxErrorEntry::to_dict() const {
	Dictionary d;
	d["id"] = id;
	d["type"] = type;
	d["severity"] = severity;
	d["message"] = message;
	d["file"] = file;
	d["line"] = line;
	d["column"] = column;
	d["stack_trace"] = stack_trace;
	d["trigger_context"] = trigger_context;
	d["phase"] = phase;
	d["timestamp"] = timestamp;
	d["last_occurrence"] = last_occurrence;
	d["occurrence_count"] = occurrence_count;
	return d;
}

String HMSandboxErrorEntry::compute_id(const String &p_type,
		const String &p_message,
		const String &p_file,
		int p_line) {
	String normalized = p_message;
	if (normalized.length() > 100) {
		normalized = normalized.substr(0, 100);
	}
	return vformat("%s|%s|%s|%d", p_type, normalized, p_file, p_line);
}

void HMSandboxErrorRegistry::add_error(const String &p_type,
		const String &p_message,
		const String &p_file,
		int p_line,
		int p_column,
		const String &p_stack_trace,
		const String &p_severity,
		const String &p_trigger_context,
		const String &p_phase) {
	const String error_id = HMSandboxErrorEntry::compute_id(p_type, p_message, p_file, p_line);
	const int64_t now = OS::get_singleton()->get_ticks_msec();

	if (error_map.has(error_id)) {
		HMSandboxErrorEntry &entry = error_map[error_id];
		entry.occurrence_count++;
		entry.last_occurrence = now;
	} else {
		HMSandboxErrorEntry entry;
		entry.id = error_id;
		entry.type = p_type;
		entry.severity = p_severity;
		entry.message = p_message;
		entry.file = p_file;
		entry.line = p_line;
		entry.column = p_column;
		entry.stack_trace = p_stack_trace;
		entry.trigger_context = p_trigger_context;
		entry.phase = p_phase;
		entry.timestamp = now;
		entry.last_occurrence = now;
		entry.occurrence_count = 1;

		error_map.insert(error_id, entry);
		error_order.push_back(error_id);
	}

	last_error = p_message;
}

void HMSandboxErrorRegistry::clear() {
	error_map.clear();
	error_order.clear();
	last_error = String();
}

Array HMSandboxErrorRegistry::get_all_errors() const {
	Array arr;
	for (int i = 0; i < error_order.size(); i++) {
		const String &id = error_order[i];
		if (error_map.has(id)) {
			arr.push_back(error_map[id].to_dict());
		}
	}
	return arr;
}

String HMSandboxErrorRegistry::get_error_report_markdown() const {
	String report;
	report += "# HMSandbox Error Report\n\n";

	for (int i = 0; i < error_order.size(); i++) {
		const String &id = error_order[i];
		if (!error_map.has(id)) {
			continue;
		}
		const HMSandboxErrorEntry &e = error_map[id];

		report += vformat("## %s (%s)\n", e.type, e.severity);
		report += vformat("- Message: %s\n", e.message);
		if (!e.file.is_empty()) {
			report += vformat("- Location: `%s`:%d\n", e.file, e.line);
		}
		if (!e.trigger_context.is_empty()) {
			report += vformat("- Context: %s\n", e.trigger_context);
		}
		if (!e.phase.is_empty()) {
			report += vformat("- Phase: %s\n", e.phase);
		}
		report += vformat("- Occurrences: %d\n", e.occurrence_count);
		if (!e.stack_trace.is_empty()) {
			report += "\n```text\n";
			report += e.stack_trace;
			report += "\n```\n";
		}
		report += "\n";
	}

	return report;
}

} // namespace hmsandbox

