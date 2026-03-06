/**************************************************************************/
/*  sandbox_limiter.h                                                     */
/**************************************************************************/

#pragma once

#include "core/typedefs.h"

namespace hmsandbox {

// GDScript 调用分类，用于配额控制。
enum class HMSandboxApiCategory {
	READ,  // 纯读取操作，不限次。
	WRITE, // 改变场景/对象状态的操作，限次。
	HEAVY, // 实例化、销毁等重操作，更严格限次。
};

// 简单的执行/资源限制器，实现超时、内存与 API 速率限制。
class HMSandboxLimiter {
public:
	HMSandboxLimiter();

	void set_timeout_ms(int64_t p_ms);
	void set_memory_limit_mb(int p_mb);

	void set_write_ops_per_frame(int p_count);
	void set_heavy_ops_per_frame(int p_count);

	void begin_execution();
	void end_execution();

	bool is_executing() const { return executing; }

	bool is_timeout_exceeded() const;
	int64_t get_remaining_time_ms() const;
	int64_t get_timeout_ms() const { return timeout_ms; }

	bool check_api_rate_limit(HMSandboxApiCategory p_category);
	void reset_frame_counters();

	void set_current_memory_usage(size_t p_bytes);
	bool is_memory_limit_exceeded() const;
	size_t get_memory_limit_bytes() const { return memory_limit_bytes; }

	int get_write_ops_this_frame() const { return write_ops_this_frame; }
	int get_heavy_ops_this_frame() const { return heavy_ops_this_frame; }
	int get_max_write_ops_per_frame() const { return max_write_ops_per_frame; }
	int get_max_heavy_ops_per_frame() const { return max_heavy_ops_per_frame; }

	void reset_stats();

private:
	int64_t timeout_ms = 1000;
	int64_t exec_start_time = 0;
	bool executing = false;

	size_t memory_limit_bytes = 64 * 1024 * 1024;
	size_t current_memory_usage = 0;

	int max_write_ops_per_frame = 500;
	int max_heavy_ops_per_frame = 50;
	int write_ops_this_frame = 0;
	int heavy_ops_this_frame = 0;
};

} // namespace hmsandbox

