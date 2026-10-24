#version 300 es
precision highp float;
precision highp int;

// Flat geometry in one colour with a per-vertex fog amount.

layout(location = 0) in vec3 a_position;
layout(location = 6) in float a_fog;

uniform mat4 u_transform;
uniform vec4 u_colour;
uniform vec3 u_fog_colour;

out vec4 v_colour;
out vec4 v_fog;

void main() {
    // Transform to clip space (no screen mapping).
    gl_Position = u_transform * vec4(a_position, 1.0);

    // One colour for the whole object.
    v_colour = clamp(u_colour, 0.0, 1.0);

    // Fog colour in rgb, fog amount in a (1 = no fog, 0 = all fog).
    v_fog = clamp(vec4(u_fog_colour, a_fog), 0.0, 1.0);
}
