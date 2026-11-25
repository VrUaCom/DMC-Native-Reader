#version 300 es
precision highp float;
precision highp int;

// Flat geometry coloured by its vertices.

layout(location = 0) in vec3 a_position;
layout(location = 3) in vec4 a_colour; // 0..1

uniform mat4 u_transform;
uniform vec3 u_fog_colour;

out vec2 v_uv;
out vec4 v_colour;
out vec4 v_fog;

void main() {
    // Transform to clip space (no screen mapping).
    gl_Position = u_transform * vec4(a_position, 1.0);

    v_colour = clamp(a_colour, 0.0, 1.0);
    v_uv = vec2(0.0);

    // Fog colour with amount 1: no fog.
    v_fog = vec4(u_fog_colour, 1.0);
}
