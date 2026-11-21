#version 300 es
precision highp float;
precision highp int;

// Movie frame drawn over the screen.

layout(location = 0) in vec4 a_position; // already a clip position
layout(location = 2) in vec2 a_uv;       // fixed-point, units of 1/16

out vec2 v_uv;

void main()
{
    gl_Position = a_position;
    v_uv = a_uv / 16.0;
}
