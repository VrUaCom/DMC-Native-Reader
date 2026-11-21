#version 300 es
precision highp float;
precision highp int;

// Full-screen pass: texture coordinates as given and moved/scaled.

layout(location = 0) in vec4 a_position; // already a clip position
layout(location = 2) in vec2 a_uv;       // fixed point, units of 1/16

uniform vec4 u_param;  // xy: offset, zw: scale for the second coordinates
uniform vec4 u_colour;

out vec2 v_uv;
out vec2 v_uv2;
out vec4 v_colour;

void main() {
    gl_Position = a_position;
    v_uv = a_uv / 16.0;
    v_uv2 = v_uv * u_param.zw + u_param.xy;
    v_colour = clamp(u_colour, 0.0, 1.0);
}
