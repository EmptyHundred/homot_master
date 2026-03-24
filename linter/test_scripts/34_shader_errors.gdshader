shader_type spatial;

uniform vec3 color;
uniform vec3 color;
uniform badtype broken_uniform;

void fragment() {
	ALBEDO = color;
	if (true) {
		// missing closing brace
}
