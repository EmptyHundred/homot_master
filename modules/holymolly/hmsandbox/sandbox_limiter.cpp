/**************************************************************************/
/*  sandbox_limiter.cpp                                                   */
/**************************************************************************/

#include "sandbox_limiter.h"

#include "core/os/os.h"

namespace hmsandbox {

HMSandboxLimiter::HMSandboxLimiter() {
}

void HMSandboxLimiter::set_timeout_ms(int64_t p_ms) {
	timeout_ms = p_ms;
}

void HMSandboxLimiter::set_memory_limit_mb(int p_mb) {
	if (p_mb <= 0) {
		memory_limit_bytes = 0;
	} else {
		memory_limit_bytes = static_cast<size_t>(p_mb) * 1024 * 1024;
	}
}

void HMSandboxLimiter::set_write_ops_per_frame(int p_count) {
	max_write_ops_per_frame = p_count;
}

void HMSandboxLimiter::set_heavy_ops_per_frame(int p_count) {
	max_heavy_ops_per_frame = p_count;
}

void HMSandboxLimiter::begin_execution() {
	if (executing) {
		return;
	}
	executing = true;
	exec_start_time = OS::get_singleton()->get_ticks_msec();
}

void HMSandboxLimiter::end_execution() {
	executing = false;
}

bool HMSandboxLimiter::is_timeout_exceeded() const {
	if (!executing || timeout_ms <= 0) {
		return false;
	}
	const int64_t now = OS::get_singleton()->get_ticks_msec();
	return now - exec_start_time > timeout_ms;
}

int64_t HMSandboxLimiter::get_remaining_time_ms() const {
	if (!executing || timeout_ms <= 0) {
		return timeout_ms;
	}
	const int64_t now = OS::get_singleton()->get_ticks_msec();
	const int64_t elapsed = now - exec_start_time;
	const int64_t remaining = timeout_ms - elapsed;
	return remaining > 0 ? remaining : 0;
}

bool HMSandboxLimiter::check_api_rate_limit(HMSandboxApiCategory p_category) {
	switch (p_category) {
		case HMSandboxApiCategory::READ:
			return true;
		case HMSandboxApiCategory::WRITE:
			if (max_write_ops_per_frame <= 0) {
				return false;
			}
			if (write_ops_this_frame >= max_write_ops_per_frame) {
				return false;
			}
			write_ops_this_frame++;
			return true;
		case HMSandboxApiCategory::HEAVY:
			if (max_heavy_ops_per_frame <= 0) {
				return false;
			}
			if (heavy_ops_this_frame >= max_heavy_ops_per_frame) {
				return false;
			}
			heavy_ops_this_frame++;
			return true;
	}
	return false;
}

void HMSandboxLimiter::reset_frame_counters() {
	write_ops_this_frame = 0;
	heavy_ops_this_frame = 0;
}

void HMSandboxLimiter::set_current_memory_usage(size_t p_bytes) {
	current_memory_usage = p_bytes;
}

bool HMSandboxLimiter::is_memory_limit_exceeded() const {
	if (memory_limit_bytes == 0) {
		return false;
	}
	return current_memory_usage > memory_limit_bytes;
}

void HMSandboxLimiter::reset_stats() {
	executing = false;
	exec_start_time = 0;
	write_ops_this_frame = 0;
	heavy_ops_this_frame = 0;
	current_memory_usage = 0;
}

} // namespace hmsandbox

