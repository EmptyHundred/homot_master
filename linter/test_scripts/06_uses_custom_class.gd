extends RefCounted

var my_obj: MyCustomClass

func _init() -> void:
	my_obj = MyCustomClass.new()
	my_obj.id = 42
	my_obj.set_data("key", "value")
