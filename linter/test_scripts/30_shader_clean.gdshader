shader_type spatial;

render_mode unshaded, cull_disabled;

uniform vec4 albedo_color : source_color = vec4(1.0);
uniform sampler2D albedo_texture : filter_linear;
uniform float roughness : hint_range(0.0, 1.0) = 0.5;

varying vec3 world_normal;

void vertex() {
	world_normal = NORMAL;
	VERTEX.y += sin(TIME + VERTEX.x * 2.0) * 0.1;
}

void fragment() {
	vec4 tex_color = texture(albedo_texture, UV);
	ALBEDO = albedo_color.rgb * tex_color.rgb;
	ROUGHNESS = roughness;
	NORMAL_MAP = vec3(0.5, 0.5, 1.0);
}

void light() {
	DIFFUSE_LIGHT += clamp(dot(NORMAL, LIGHT), 0.0, 1.0) * ATTENUATION * LIGHT_COLOR;
}
