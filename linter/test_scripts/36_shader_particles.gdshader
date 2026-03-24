shader_type particles;

uniform float emission_speed : hint_range(0.0, 20.0) = 5.0;
uniform float spread : hint_range(0.0, 3.14) = 1.0;

void start() {
	float angle = RANDOM_SEED * spread;
	VELOCITY = vec3(sin(angle), 1.0, cos(angle)) * emission_speed;
	TRANSFORM = mat4(1.0);
}

void process() {
	VELOCITY.y -= 9.8 * DELTA;
	if (CUSTOM.y > LIFETIME) {
		ACTIVE = false;
	}
}
