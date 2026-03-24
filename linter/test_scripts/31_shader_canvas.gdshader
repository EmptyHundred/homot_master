shader_type canvas_item;

uniform vec4 modulate_color : source_color = vec4(1.0);
uniform float wave_speed : hint_range(0.0, 10.0) = 2.0;
uniform float wave_amplitude : hint_range(0.0, 0.1) = 0.02;

void vertex() {
	VERTEX.y += sin(TIME * wave_speed + VERTEX.x * 4.0) * wave_amplitude * 100.0;
}

void fragment() {
	vec4 color = texture(TEXTURE, UV);
	COLOR = color * modulate_color;
}
