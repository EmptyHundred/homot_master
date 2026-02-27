class_name TestResultData
# Shared test result structure for HMScript tests

var test_name: String = ""
var passed: bool = false
var message: String = ""
var expected: String = ""
var actual: String = ""

func _init(p_test_name: String = ""):
	test_name = p_test_name

func set_passed(p_message: String = "Test passed"):
	passed = true
	message = p_message

func set_failed(p_message: String, p_expected: String = "", p_actual: String = ""):
	passed = false
	message = p_message
	expected = p_expected
	actual = p_actual

func to_string() -> String:
	var result = "[" + ("PASS" if passed else "FAIL") + "] " + test_name
	if message:
		result += ": " + message
	if not passed and expected:
		result += "\n  Expected: " + expected
		result += "\n  Actual: " + actual
	return result
