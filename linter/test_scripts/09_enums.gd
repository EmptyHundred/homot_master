extends RefCounted

enum Direction { UP, DOWN, LEFT, RIGHT }

var current_dir: Direction = Direction.UP

func move(dir: Direction) -> void:
	current_dir = dir

func test_enum() -> void:
	move(Direction.LEFT)
