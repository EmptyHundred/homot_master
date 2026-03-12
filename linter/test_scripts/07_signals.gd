extends Node

signal health_changed(new_health: int)
signal died

var health: int = 100

func take_damage(amount: int) -> void:
	health -= amount
	health_changed.emit(health)
	if health <= 0:
		died.emit()
