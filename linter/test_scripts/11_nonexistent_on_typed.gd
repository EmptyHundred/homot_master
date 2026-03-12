extends RefCounted

func test() -> void:
	var n: Node = Node.new()
	n.nonexistent_method()
