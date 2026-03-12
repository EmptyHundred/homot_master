extends Node

func _ready() -> void:
	self.this_method_does_not_exist()
	var n: Node = Node.new()
	n.nonexistent_method()
