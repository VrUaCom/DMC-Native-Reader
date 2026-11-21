#version 300 es
// Full-screen pass that only passes texture coordinates.
precision highp float;
precision highp int;

layout(location = 0) in vec4 a_position; // already a clip position
layout(location = 2) in vec2 a_uv;       // fixed point, units of 1/16

out vec2 v_uv;

void main()
{
    gl_Position = a_position;
    // Convert fixed-point 1/16 units to texture coordinates.
    v_uv = a_uv / 16.0;
}
