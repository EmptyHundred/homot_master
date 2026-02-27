/**************************************************************************/
/*  test_sandbox_config.cpp                                              */
/**************************************************************************/

#include "tests/test_macros.h"

#include "modules/hmscript/sandbox/sandbox_config.h"

namespace TestHMSandboxConfig {

TEST_CASE("[HMSandbox] default class blocklist") {
	hmsandbox::HMSandboxConfig config;

	CHECK(config.is_class_or_parent_blocked("OS"));
	CHECK(config.is_class_or_parent_blocked("FileAccess"));
	CHECK(config.is_class_or_parent_blocked("DirAccess"));
	CHECK(config.is_class_or_parent_blocked("Thread"));
}

TEST_CASE("[HMSandbox] default method blocklist") {
	hmsandbox::HMSandboxConfig config;

	CHECK(config.is_method_blocked_with_inheritance("Object", "call"));
	CHECK(config.is_method_blocked_with_inheritance("Object", "callv"));
	CHECK(config.is_method_blocked_with_inheritance("Object", "set"));
	CHECK(config.is_method_blocked_with_inheritance("ClassDB", "instantiate"));
	CHECK(config.is_method_blocked_with_inheritance("Engine", "get_singleton"));
}

TEST_CASE("[HMSandbox] path whitelist and traversal") {
	hmsandbox::HMSandboxConfig config;

	// 默认允许 res:// 与 user://。
	CHECK(config.is_path_allowed("res://levels/level_01.tscn"));
	CHECK(config.is_path_allowed("user://profiles/save_1.save"));

	// 明显的目录穿越应被拒绝。
	CHECK_FALSE(config.is_path_allowed("../res/levels/level_01.tscn"));
	CHECK_FALSE(config.is_path_allowed("res://../levels/level_01.tscn"));
	CHECK_FALSE(config.is_path_allowed("C:\\projects\\..\\secret.txt"));
}

} // namespace TestHMSandboxConfig

