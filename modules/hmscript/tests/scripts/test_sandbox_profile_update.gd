# Test script to verify that sandbox profile IDs are correctly updated
extends Node

func _ready():
	print("=== Testing Sandbox Profile ID Update ===\n")

	# Test 1: Load a sandbox
	print("Test 1: Loading a sandbox...")
	var sandbox_manager = Engine.get_singleton("HMSandboxManager")
	if not sandbox_manager:
		print("ERROR: HMSandboxManager not found!")
		return

	# This would load a sandbox with .hm files
	# var sandbox = sandbox_manager.load_sandbox("res://test_sandbox", "scene.tscn")
	# if sandbox:
	#     print("Sandbox loaded with profile ID: ", sandbox.get_profile_id())
	#
	#     # Test 2: Check if scripts have the correct profile ID
	#     var deps = sandbox.get_dependencies()
	#     print("\nTest 2: Checking script profile IDs...")
	#     for dep in deps:
	#         var script = load(dep)
	#         if script:
	#             print("  Script: ", dep)
	#             print("    Sandbox enabled: ", script.is_sandbox_enabled())
	#             print("    Profile ID: ", script.get_sandbox_profile_id())
	#
	#             # Verify it matches the sandbox's profile ID
	#             if script.get_sandbox_profile_id() == sandbox.get_profile_id():
	#                 print("    ✓ Profile ID matches sandbox!")
	#             else:
	#                 print("    ✗ Profile ID mismatch! Expected: ", sandbox.get_profile_id())

	# Test 3: Verify runtime instance profile ID
	print("\nTest 3: Checking runtime instance profile ID...")
	var my_profile = get_script_instance_sandbox_profile_id()
	if my_profile == "":
		print("  Not running in a sandbox (normal GDScript)")
	else:
		print("  Running in sandbox with profile: ", my_profile)

	# Test 4: Demonstrate profile synchronization
	print("\nTest 4: Testing profile synchronization...")
	# If we had a sandbox instance:
	# sandbox.set_write_ops_per_frame(1000)
	# sandbox.set_heavy_ops_per_frame(50)
	#
	# This should automatically sync to the GDScriptLanguage profile
	# so that VM-level checks use the same limits

	print("\n=== Test Complete ===")
	print("\nNote: To fully test, create a test sandbox directory with .hm files")
	print("and uncomment the sandbox loading code above.")
