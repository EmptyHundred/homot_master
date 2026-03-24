extends Node

var score: int = 0
var player: Player = null

func _ready() -> void:
	player = $Player as Player
	player.died.connect(_on_player_died)
	player.health_changed.connect(_on_health_changed)

	for enemy: Node in $Enemies.get_children():
		if enemy is Enemy:
			pass

func _on_player_died() -> void:
	get_tree().reload_current_scene()

func _on_health_changed(new_health: int) -> void:
	print("Health: ", new_health)

func add_score(points: int) -> void:
	score += points
