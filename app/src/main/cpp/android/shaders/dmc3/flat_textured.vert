#version 300 es
// Flat textured geometry: white colour, no fog.
precision highp float;
precision highp int;

layout(location = 0) in vec3 a_position;
layout(location = 2) in vec2 a_uv; // fixed point, units of 1/16

uniform mat4 u_transform;
uniform vec2 u_uv_scale;
uniform vec3 u_fog_colour;

out vec2 v_uv;
out vec4 v_colour;
out vec4 v_fog;

void main() {
    gl_Position = u_transform * vec4(a_position, 1.0);
    // Convert 1/16 fixed point to texture coordinates, then scale.
    v_uv = a_uv / 16.0 * u_uv_scale;
    v_colour = vec4(1.0);
    // Fog amount 1 means no fog.
    v_fog = vec4(u_fog_colour, 1.0);
}
