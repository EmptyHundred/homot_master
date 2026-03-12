class_name MyCustomClass
extends Resource

var data: Dictionary = {}
var id: int = 0

func set_data(key: String, value: Variant) -> void:
	data[key] = value

func get_data(key: String) -> Variant:
	return data.get(key)
