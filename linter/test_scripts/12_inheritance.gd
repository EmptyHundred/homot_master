extends Node

func test() -> void:
	var t: Timer = Timer.new()
	t.start()
	t.stop()
	var remaining: float = t.time_left
	t.timeout.connect(test)
