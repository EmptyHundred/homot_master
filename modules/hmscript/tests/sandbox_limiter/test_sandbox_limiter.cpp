/**************************************************************************/
/*  test_sandbox_limiter.cpp                                              */
/**************************************************************************/

#include "tests/test_macros.h"

#include "modules/hmscript/sandbox/sandbox_limiter.h"

namespace TestHMSandboxLimiter {

TEST_CASE("[HMSandbox] write/heavy rate limiting per frame") {
	hmsandbox::HMSandboxLimiter limiter;
	limiter.set_write_ops_per_frame(2);
	limiter.set_heavy_ops_per_frame(1);
	limiter.reset_frame_counters();

	// WRITE：前两次允许，第三次拒绝。
	CHECK(limiter.check_api_rate_limit(hmsandbox::HMSandboxApiCategory::WRITE));
	CHECK(limiter.check_api_rate_limit(hmsandbox::HMSandboxApiCategory::WRITE));
	CHECK_FALSE(limiter.check_api_rate_limit(hmsandbox::HMSandboxApiCategory::WRITE));

	// HEAVY：第一次允许，第二次拒绝。
	CHECK(limiter.check_api_rate_limit(hmsandbox::HMSandboxApiCategory::HEAVY));
	CHECK_FALSE(limiter.check_api_rate_limit(hmsandbox::HMSandboxApiCategory::HEAVY));

	// RESET 后应重新计数。
	limiter.reset_frame_counters();
	CHECK(limiter.check_api_rate_limit(hmsandbox::HMSandboxApiCategory::WRITE));
	CHECK(limiter.check_api_rate_limit(hmsandbox::HMSandboxApiCategory::HEAVY));
}

TEST_CASE("[HMSandbox] memory limit detection") {
	hmsandbox::HMSandboxLimiter limiter;

	// 默认 64MB，当前使用为 0，不应超限。
	limiter.set_current_memory_usage(0);
	CHECK_FALSE(limiter.is_memory_limit_exceeded());

	// 设置为 1MB 上限，使用 2MB，应视为超限。
	limiter.set_memory_limit_mb(1);
	limiter.set_current_memory_usage(2 * 1024 * 1024);
	CHECK(limiter.is_memory_limit_exceeded());
}

} // namespace TestHMSandboxLimiter

