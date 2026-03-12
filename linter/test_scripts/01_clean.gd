extends RefCounted

var count: int = 0
var label: String = "hello"

func add(a: int, b: int) -> int:
	return a + b

func greet(name: String) -> String:
	return "Hello, " + name
