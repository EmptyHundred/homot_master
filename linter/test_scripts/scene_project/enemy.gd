class_name Enemy
extends CharacterBody3D

@export var patrol_speed: float = 3.0
@export var chase_speed: float = 6.0
@export var damage: int = 10

enum State { IDLE, PATROL, CHASE, ATTACK, DEAD }

var current_state: State = State.IDLE
var target: Player = null

@onready var nav_agent: NavigationAgent3D = $NavigationAgent3D
@onready var anim: AnimationPlayer = $AnimationPlayer
@onready var detection_area: Area3D = $DetectionArea

func _ready() -> void:
	detection_area.body_entered.connect(_on_body_entered)
	detection_area.body_exited.connect(_on_body_exited)
	current_state = State.PATROL

func _physics_process(delta: float) -> void:
	match current_state:
		State.IDLE:
			pass
		State.PATROL:
			_patrol(delta)
		State.CHASE:
			_chase(delta)
		State.ATTACK:
			_attack()
		State.DEAD:
			pass

func _patrol(delta: float) -> void:
	if nav_agent.is_navigation_finished():
		return
	var next_pos := nav_agent.get_next_path_position()
	var direction := (next_pos - global_position).normalized()
	velocity = direction * patrol_speed
	move_and_slide()

func _chase(delta: float) -> void:
	if target == null:
		current_state = State.PATROL
		return
	nav_agent.target_position = target.global_position
	var next_pos := nav_agent.get_next_path_position()
	var direction := (next_pos - global_position).normalized()
	velocity = direction * chase_speed
	move_and_slide()

func _attack() -> void:
	if target != null:
		target.take_damage(damage)
		anim.play("attack")

func _on_body_entered(body: Node3D) -> void:
	if body is Player:
		target = body as Player
		current_state = State.CHASE

func _on_body_exited(body: Node3D) -> void:
	if body == target:
		target = null
		current_state = State.PATROL
