#version 300 es
precision highp float;
precision highp int;

// Flat geometry (menus, 2D shapes) drawn in one colour.

layout(location = 0) in vec3 a_position;

uniform mat4 u_transform;
uniform vec4 u_colour;

out vec4 v_colour;

void main() {
    // Transform the position; no screen mapping.
    gl_Position = u_transform * vec4(a_position, 1.0);
    // Pass the uniform colour, clamped to 0..1.
    v_colour = clamp(u_colour, 0.0, 1.0);
}
