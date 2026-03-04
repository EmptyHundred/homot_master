# Test script to demonstrate sandbox query methods
extends Node

func _ready():
	print("=== Testing Sandbox Query Methods ===")

	# Test 1: Query the script object directly
	var my_script = get_script()
	if my_script:
		print("Script class: ", my_script.get_class())
		print("Script sandbox enabled: ", my_script.is_sandbox_enabled())
		print("Script sandbox profile ID: ", my_script.get_sandbox_profile_id())
	else:
		print("No script attached")

	# Test 2: Query the script instance (runtime)
	print("\nRuntime instance sandbox status:")
	print("Instance sandbox enabled: ", is_script_instance_sandbox_enabled())
	print("Instance sandbox profile ID: ", get_script_instance_sandbox_profile_id())

	# Test 3: Load another script and check its sandbox status
	print("\n=== Testing External Script ===")
	var hm_script = load("res://test.hm")  # Would be an HMScript file
	if hm_script:
		print("Loaded script sandbox enabled: ", hm_script.is_sandbox_enabled())
		print("Loaded script profile ID: ", hm_script.get_sandbox_profile_id())

	print("\n=== Test Complete ===")
