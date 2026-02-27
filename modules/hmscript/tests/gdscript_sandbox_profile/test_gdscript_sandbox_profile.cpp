/**************************************************************************/
/*  test_gdscript_sandbox_profile.cpp                                     */
/**************************************************************************/

#include "tests/test_macros.h"

#include "modules/gdscript/gdscript.h"

namespace TestGDScriptSandboxProfile {

TEST_CASE("[GDScript] sandbox profile basic lifecycle") {
	GDScriptLanguage *lang = GDScriptLanguage::get_singleton();
	REQUIRE_MESSAGE(lang != nullptr, "GDScriptLanguage singleton must exist for sandbox profile tests.");

	// 创建或获取一个名为 "hm_default" 的 profile。
	GDScriptLanguage::SandboxProfile *profile_a = lang->ensure_sandbox_profile("hm_default");
	REQUIRE(profile_a != nullptr);

	// 再次获取应返回同一个 profile。
	GDScriptLanguage::SandboxProfile *profile_b = lang->get_sandbox_profile("hm_default");
	CHECK(profile_b != nullptr);
	CHECK(profile_a == profile_b);

	// 基本限流行为：每帧写操作配额 1。
	profile_a->limiter.set_write_ops_per_frame(1);

	// 首次 WRITE 调用应通过。
	CHECK(profile_a->limiter.check_api_rate_limit(hmsandbox::HMSandboxApiCategory::WRITE));
	// 第二次同帧 WRITE 应被拒绝。
	CHECK_FALSE(profile_a->limiter.check_api_rate_limit(hmsandbox::HMSandboxApiCategory::WRITE));

	// 调用语言级的每帧重置接口后，配额应被重置。
	lang->reset_sandbox_profiles_per_frame();
	CHECK(profile_a->limiter.check_api_rate_limit(hmsandbox::HMSandboxApiCategory::WRITE));
}

TEST_CASE("[GDScript] sandbox profile error aggregation API") {
	GDScriptLanguage *lang = GDScriptLanguage::get_singleton();
	REQUIRE(lang != nullptr);

	GDScriptLanguage::SandboxProfile *profile = lang->ensure_sandbox_profile("hm_errors");
	REQUIRE(profile != nullptr);

	// 记录一条错误，随后通过 GDScriptLanguage 的查询接口读取。
	profile->errors.add_error(
			"sandbox",
			"test error from GDScript sandbox profile",
			"res://fake.gd",
			42,
			0,
			"",
			"error",
			"unit_test",
			"runtime");

	Dictionary info = lang->get_sandbox_errors("hm_errors");
	CHECK(info.has("errors"));
	CHECK(info.has("last_error"));

	Array errors = info["errors"];
	CHECK(errors.size() >= 1);

	String last_error = info["last_error"];
	CHECK(last_error.find("test error from GDScript sandbox profile") != -1);

	String report = lang->get_sandbox_error_report("hm_errors");
	CHECK(report.find("HMSandbox Error Report") != -1);
}

} // namespace TestGDScriptSandboxProfile

