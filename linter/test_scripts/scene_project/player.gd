class_name Player
extends CharacterBody3D

signal health_changed(new_health: int)
signal died

@export var max_health: int = 100
@export var speed: float = 5.0

var health: int = 100

@onready var animation_player: AnimationPlayer = $AnimationPlayer
@onready var collision_shape: CollisionShape3D = $CollisionShape3D
@onready var health_bar: ProgressBar = $UI/HealthBar
@onready var label: Label = $UI/NameLabel

func _ready() -> void:
	health = max_health
	health_bar.max_value = max_health
	health_bar.value = health
	label.text = "Player"

func _physics_process(delta: float) -> void:
	var input_dir := Input.get_vector("move_left", "move_right", "move_forward", "move_back")
	var direction := Vector3(input_dir.x, 0, input_dir.y)
	velocity = direction * speed
	move_and_slide()

func take_damage(amount: int) -> void:
	health -= amount
	health = maxi(health, 0)
	health_bar.value = health
	health_changed.emit(health)
	animation_player.play("hit")
	if health <= 0:
		die()

func die() -> void:
	died.emit()
	animation_player.play("death")

func heal(amount: int) -> void:
	health = mini(health + amount, max_health)
	health_bar.value = health
	health_changed.emit(health)
