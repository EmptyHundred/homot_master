extends Node
# HMScript Test Runner
# Runs all test scripts in the current directory and reports results

class TestResult:
	var name: String
	var passed: bool = false
	var error_message: String = ""
	var duration_ms: float = 0.0
	var expected: String = ""
	var actual: String = ""

func _ready():
	print("=")
	print("HMScript Sandbox Test Runner")
	print("=")
	print()

	var results: Array[TestResult] = []
	var test_dir = "res://scripts"

	# Get all test files
	var test_files = get_test_files(test_dir)

	if test_files.is_empty():
		print("No test files found in: " + test_dir)
		get_tree().quit(1)
		return

	print("Found " + str(test_files.size()) + " test files")
	print()

	# Run each test
	for test_file in test_files:
		var result = run_test(test_file)
		results.append(result)

	# Print summary
	print()
	print("=")
	print_summary(results)
	print("=")

	# Exit with appropriate code
	var failed_count = results.filter(func(r): return not r.passed).size()
	get_tree().quit(0 if failed_count == 0 else 1)

func get_test_files(dir_path: String) -> Array[String]:
	var files: Array[String] = []
	var dir = DirAccess.open(dir_path)

	if dir == null:
		print("Error: Cannot open directory: " + dir_path)
		return files

	dir.list_dir_begin()
	var file_name = dir.get_next()

	while file_name != "":
		# Skip special files and output files
		if not file_name.begins_with(".") and \
		   file_name.ends_with(".hm") and \
		   not file_name.ends_with(".notest.hm"):
			files.append(file_name)
		file_name = dir.get_next()

	dir.list_dir_end()
	files.sort()
	return files

func run_test(test_file: String) -> TestResult:
	var result = TestResult.new()
	result.name = test_file.get_basename()

	var test_path = "res://scripts/" + test_file

	print("-")
	print("Running: " + result.name)
	print("-")

	var start_time = Time.get_ticks_msec()

	# Load the script
	var script = load(test_path)
	if script == null:
		result.passed = false
		result.error_message = "Failed to load script"
		print("ERROR: " + result.error_message)
		result.duration_ms = Time.get_ticks_msec() - start_time
		return result

	# Create instance
	var test_instance = script.new()
	if test_instance == null:
		result.passed = false
		result.error_message = "Failed to instantiate script"
		print("ERROR: " + result.error_message)
		result.duration_ms = Time.get_ticks_msec() - start_time
		return result

	# Check if test() method exists
	if not test_instance.has_method("test"):
		result.passed = false
		result.error_message = "Script does not have a test() method"
		print("ERROR: " + result.error_message)
		if test_instance is Node:
			test_instance.free()
		result.duration_ms = Time.get_ticks_msec() - start_time
		return result

	# Run the test
	var test_result = test_instance.test()

	# Free instance if it's a Node
	if test_instance is Node:
		test_instance.free()

	result.duration_ms = Time.get_ticks_msec() - start_time

	# Process test result
	if test_result is Dictionary:
		result.name = test_result.get("test_name", result.name)
		result.passed = test_result.get("passed", false)
		result.error_message = test_result.get("message", "")
		result.expected = test_result.get("expected", "")
		result.actual = test_result.get("actual", "")
	else:
		# If test doesn't return a Dictionary, assume it passed if no error
		result.passed = true

	# Print result
	if result.passed:
		print("✓ PASSED: " + result.error_message if result.error_message else "✓ PASSED")
	else:
		print("✗ FAILED: " + result.error_message)
		if result.expected:
			print("  Expected: " + result.expected)
		if result.actual:
			print("  Actual: " + result.actual)

	print("Duration: " + str(result.duration_ms) + " ms")
	print()

	return result

func print_summary(results: Array[TestResult]):
	var passed_count = results.filter(func(r): return r.passed).size()
	var failed_count = results.filter(func(r): return not r.passed).size()
	var total_duration = 0.0

	for result in results:
		total_duration += result.duration_ms

	print("Test Summary:")
	print("  Total:  " + str(results.size()))
	print("  Passed: " + str(passed_count) + " ✓")
	print("  Failed: " + str(failed_count) + " ✗")
	print("  Duration: " + str(total_duration) + " ms")
	print()

	if failed_count > 0:
		print("Failed tests:")
		for result in results:
			if not result.passed:
				print("  - " + result.name)
				if not result.error_message.is_empty():
					print("    Message: " + result.error_message)
				if not result.expected.is_empty():
					print("    Expected: " + result.expected)
				if not result.actual.is_empty():
					print("    Actual: " + result.actual)
		print()

	if passed_count == results.size():
		print("All tests passed! 🎉")
	else:
		print(str(failed_count) + " test(s) failed.")
