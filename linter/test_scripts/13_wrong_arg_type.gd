extends RefCounted

func add(a: int, b: int) -> int:
	return a + b

func test() -> void:
	var result: int = add("hello", "world")
