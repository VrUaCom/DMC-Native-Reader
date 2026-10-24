#version 300 es
precision highp float;
precision highp int;

// Full-screen pass that hands one parameter vector to its fragment shader.

layout(location = 0) in vec4 a_position; // already a clip position
layout(location = 2) in vec2 a_uv;       // fixed point, 1/16 units

uniform vec4 u_param1;
uniform vec4 u_colour;

out vec2 v_uv;
out vec4 v_colour;
out vec4 v_param1;

void main() {
    gl_Position = a_position;
    v_uv = a_uv / 16.0;
    v_param1 = u_param1;
    v_colour = clamp(u_colour, 0.0, 1.0);
}
