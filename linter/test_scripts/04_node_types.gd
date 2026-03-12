extends Node

func _ready() -> void:
	var timer := Timer.new()
	timer.wait_time = 1.0
	timer.one_shot = true
	add_child(timer)
	timer.start()

func _process(delta: float) -> void:
	pass
