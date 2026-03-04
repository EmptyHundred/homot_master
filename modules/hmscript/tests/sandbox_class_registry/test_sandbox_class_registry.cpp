/**************************************************************************/
/*  test_sandbox_class_registry.cpp                                       */
/**************************************************************************/

#include "tests/test_macros.h"

#include "modules/hmscript/sandbox/sandbox_class_registry.h"

namespace TestSandboxClassRegistry {

TEST_CASE("[SandboxClassRegistry] Register and lookup classes") {
	hmsandbox::SandboxClassRegistry registry;

	// Create a test class info
	hmsandbox::SandboxClassRegistry::ClassInfo info;
	info.class_name = "TestClass";
	info.script_path = "user://test/test.gd";
	info.base_type = "Node";

	// Register the class
	bool registered = registry.register_class(info);
	CHECK(registered);

	// Verify it was registered
	CHECK(registry.has_class("TestClass"));
	CHECK(registry.get_class_count() == 1);

	// Verify lookup by name
	auto retrieved = registry.get_class_info("TestClass");
	CHECK(retrieved.class_name == "TestClass");
	CHECK(retrieved.script_path == "user://test/test.gd");
	CHECK(retrieved.base_type == "Node");

	// Verify lookup by path
	CHECK(registry.has_script_path("user://test/test.gd"));
	CHECK(registry.get_class_name_for_path("user://test/test.gd") == "TestClass");
}

TEST_CASE("[SandboxClassRegistry] Unregister classes") {
	hmsandbox::SandboxClassRegistry registry;

	hmsandbox::SandboxClassRegistry::ClassInfo info;
	info.class_name = "TestClass";
	info.script_path = "user://test/test.gd";
	info.base_type = "Node";

	registry.register_class(info);
	CHECK(registry.has_class("TestClass"));

	// Unregister
	registry.unregister_class("TestClass");
	CHECK_FALSE(registry.has_class("TestClass"));
	CHECK(registry.get_class_count() == 0);
}

TEST_CASE("[SandboxClassRegistry] Clear all registrations") {
	hmsandbox::SandboxClassRegistry registry;

	// Register multiple classes
	for (int i = 0; i < 5; i++) {
		hmsandbox::SandboxClassRegistry::ClassInfo info;
		info.class_name = "TestClass" + String::num(i);
		info.script_path = "user://test/test" + String::num(i) + ".gd";
		info.base_type = "Node";
		registry.register_class(info);
	}

	CHECK(registry.get_class_count() == 5);

	// Clear all
	registry.clear();
	CHECK(registry.get_class_count() == 0);
}

TEST_CASE("[SandboxClassRegistry] Circular dependency detection") {
	hmsandbox::SandboxClassRegistry registry;

	// Register ClassA -> Node
	hmsandbox::SandboxClassRegistry::ClassInfo info_a;
	info_a.class_name = "ClassA";
	info_a.script_path = "user://test/a.gd";
	info_a.base_type = "Node";
	CHECK(registry.register_class(info_a));

	// Register ClassB -> ClassA (should succeed)
	hmsandbox::SandboxClassRegistry::ClassInfo info_b;
	info_b.class_name = "ClassB";
	info_b.script_path = "user://test/b.gd";
	info_b.base_type = "ClassA";
	CHECK(registry.register_class(info_b));

	// Try to register ClassC -> ClassC (self-referencing, should fail)
	hmsandbox::SandboxClassRegistry::ClassInfo info_c_self;
	info_c_self.class_name = "ClassC";
	info_c_self.script_path = "user://test/c.gd";
	info_c_self.base_type = "ClassC";
	CHECK_FALSE(registry.register_class(info_c_self));

	// Try to create a cycle: ClassC -> ClassB, then update ClassA -> ClassC
	// First register ClassC -> ClassB (should succeed)
	hmsandbox::SandboxClassRegistry::ClassInfo info_c;
	info_c.class_name = "ClassC";
	info_c.script_path = "user://test/c.gd";
	info_c.base_type = "ClassB";
	CHECK(registry.register_class(info_c));

	// Now try to update ClassA to extend ClassC (would create cycle: ClassA -> ClassC -> ClassB -> ClassA)
	hmsandbox::SandboxClassRegistry::ClassInfo info_a_cycle;
	info_a_cycle.class_name = "ClassA";
	info_a_cycle.script_path = "user://test/a_updated.gd";
	info_a_cycle.base_type = "ClassC";
	CHECK_FALSE(registry.register_class(info_a_cycle));

	// Verify ClassA is still registered with original base
	CHECK(registry.has_class("ClassA"));
	CHECK(registry.get_class_info("ClassA").base_type == "Node");
}

TEST_CASE("[SandboxClassRegistry] Get all class names") {
	hmsandbox::SandboxClassRegistry registry;

	// Register multiple classes
	PackedStringArray expected_names;
	for (int i = 0; i < 3; i++) {
		hmsandbox::SandboxClassRegistry::ClassInfo info;
		info.class_name = "TestClass" + String::num(i);
		info.script_path = "user://test/test" + String::num(i) + ".gd";
		info.base_type = "Node";
		registry.register_class(info);
		expected_names.push_back(info.class_name);
	}

	PackedStringArray all_names = registry.get_all_class_names();
	CHECK(all_names.size() == 3);

	// Verify all expected names are present
	for (int i = 0; i < expected_names.size(); i++) {
		bool found = false;
		for (int j = 0; j < all_names.size(); j++) {
			if (all_names[j] == expected_names[i]) {
				found = true;
				break;
			}
		}
		CHECK(found);
	}
}

TEST_CASE("[SandboxClassRegistry] Re-registration with same path") {
	hmsandbox::SandboxClassRegistry registry;

	// Register a class
	hmsandbox::SandboxClassRegistry::ClassInfo info1;
	info1.class_name = "OldName";
	info1.script_path = "user://test/script.gd";
	info1.base_type = "Node";
	registry.register_class(info1);

	CHECK(registry.has_class("OldName"));

	// Re-register same path with different name
	hmsandbox::SandboxClassRegistry::ClassInfo info2;
	info2.class_name = "NewName";
	info2.script_path = "user://test/script.gd"; // Same path
	info2.base_type = "Resource";
	registry.register_class(info2);

	// Old name should be removed
	CHECK_FALSE(registry.has_class("OldName"));
	// New name should be present
	CHECK(registry.has_class("NewName"));
	// Only one class should be registered
	CHECK(registry.get_class_count() == 1);

	// Verify the path maps to the new name
	CHECK(registry.get_class_name_for_path("user://test/script.gd") == "NewName");
}

} // namespace TestSandboxClassRegistry
