#version 300 es
// Flat geometry coloured by its vertices, with a per-vertex fog amount.
precision highp float;
precision highp int;

layout(location = 0) in vec3 a_position;
layout(location = 3) in vec4 a_colour;
layout(location = 6) in float a_fog;

uniform mat4 u_transform;
uniform vec3 u_fog_colour;

out vec2 v_uv;
out vec4 v_colour;
out vec4 v_fog;

void main() {
    // Transform to clip space (no screen mapping).
    gl_Position = u_transform * vec4(a_position, 1.0);
    // Vertex colour, clamped to 0..1.
    v_colour = clamp(a_colour, 0.0, 1.0);
    // No texture coordinates.
    v_uv = vec2(0.0);
    // Fog colour in rgb, fog amount in a, clamped to 0..1.
    v_fog = clamp(vec4(u_fog_colour, a_fog), 0.0, 1.0);
}
