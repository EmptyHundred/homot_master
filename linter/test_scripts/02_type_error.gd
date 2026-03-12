extends RefCounted

var x: int = "this is not an int"

func bad_return() -> int:
	return "string instead of int"
