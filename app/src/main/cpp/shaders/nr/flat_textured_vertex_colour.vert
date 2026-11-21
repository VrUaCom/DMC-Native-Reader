#version 300 es
// Flat textured geometry tinted by its vertex colours.
precision highp float;
precision highp int;

layout(location = 0) in vec3 a_position;
layout(location = 2) in vec2 a_uv;     // fixed point, units of 1/16
layout(location = 3) in vec4 a_colour; // 0..1

uniform mat4 u_transform;
uniform vec2 u_uv_scale;
uniform vec3 u_fog_colour;

out vec2 v_uv;
out vec4 v_colour;
out vec4 v_fog;

void main() {
    // Transform to clip space (no screen mapping).
    gl_Position = u_transform * vec4(a_position, 1.0);

    // Convert fixed-point texture coordinates and apply the scale.
    v_uv = a_uv / 16.0 * u_uv_scale;

    // Pass the vertex colour through, clamped to 0..1.
    v_colour = clamp(a_colour, 0.0, 1.0);

    // Fog colour with amount 1 (no fog).
    v_fog = vec4(u_fog_colour, 1.0);
}
