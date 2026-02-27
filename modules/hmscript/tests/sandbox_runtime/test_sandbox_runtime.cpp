/**************************************************************************/
/*  test_sandbox_runtime.cpp                                              */
/**************************************************************************/

#include "tests/test_macros.h"

#include "modules/hmscript/sandbox/sandbox_runtime.h"

namespace TestHMSandboxRuntime {

TEST_CASE("[HMSandbox] call_script_function invalid input") {
	hmsandbox::HMSandboxRuntime runtime;

	Ref<Script> script; // null
	Object *owner = nullptr;
	String error;

	Variant ret = runtime.call_script_function(script, owner, "foo", Array(), error);

	CHECK(ret.get_type() == Variant::NIL);
	CHECK(!error.is_empty());

	Array all_errors = runtime.get_all_errors();
	CHECK(all_errors.size() == 1);
}

TEST_CASE("[HMSandbox] call_script_function respects memory limit") {
	hmsandbox::HMSandboxRuntime runtime;

	// 将当前使用量设置为超过上限的值，模拟超限场景。
	runtime.set_memory_limit_mb(1);
	runtime.get_limiter().set_current_memory_usage(2 * 1024 * 1024);

	Ref<Script> script; // null
	Object *owner = nullptr;
	String error;

	Variant ret = runtime.call_script_function(script, owner, "foo", Array(), error);

	CHECK(ret.get_type() == Variant::NIL);
	CHECK(!error.is_empty());

	Array all_errors = runtime.get_all_errors();
	CHECK(all_errors.size() >= 1);
}

} // namespace TestHMSandboxRuntime

