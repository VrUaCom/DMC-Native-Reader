#version 300 es
// Flat textured geometry tinted by one colour, with a per-vertex fog amount.
precision highp float;
precision highp int;

layout(location = 0) in vec3 a_position;
layout(location = 2) in vec2 a_uv;
layout(location = 6) in float a_fog;

uniform mat4 u_transform;
uniform vec2 u_uv_scale;
uniform vec4 u_colour;
uniform vec3 u_fog_colour;

out vec2 v_uv;
out vec4 v_colour;
out vec4 v_fog;

void main() {
    // Transform to clip space (no screen mapping).
    gl_Position = u_transform * vec4(a_position, 1.0);

    // Texture coordinates arrive in 1/16 fixed-point units.
    v_uv = a_uv / 16.0 * u_uv_scale;

    // One colour for the whole draw.
    v_colour = clamp(u_colour, 0.0, 1.0);

    // rgb: fog colour; a: fog amount (1 = no fog, 0 = all fog).
    v_fog = clamp(vec4(u_fog_colour, a_fog), 0.0, 1.0);
}
