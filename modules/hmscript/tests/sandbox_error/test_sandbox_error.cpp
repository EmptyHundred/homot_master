/**************************************************************************/
/*  test_sandbox_error.cpp                                                */
/**************************************************************************/

#include "tests/test_macros.h"

#include "modules/hmscript/sandbox/sandbox_error.h"

namespace TestHMSandboxError {

TEST_CASE("[HMSandbox] error deduplication and counts") {
	hmsandbox::HMSandboxErrorRegistry registry;

	registry.add_error("gdscript", "Some error message", "res://test.gd", 10);
	registry.add_error("gdscript", "Some error message", "res://test.gd", 10);

	Array all = registry.get_all_errors();
	REQUIRE(all.size() == 1);

	Dictionary e = all[0];
	CHECK(int(e["occurrence_count"]) == 2);
	CHECK(String(e["type"]) == "gdscript");
	CHECK(String(e["file"]) == "res://test.gd");
	CHECK(int(e["line"]) == 10);
}

TEST_CASE("[HMSandbox] markdown error report basics") {
	hmsandbox::HMSandboxErrorRegistry registry;

	registry.add_error("security", "Blocked class OS", "", 0, 0, "", "error", "load_scene", "load");

	const String report = registry.get_error_report_markdown();
	CHECK(report.find("HMSandbox Error Report") != -1);
	CHECK(report.find("security") != -1);
	CHECK(report.find("Blocked class OS") != -1);
	CHECK(report.find("Occurrences") != -1);
}

} // namespace TestHMSandboxError

