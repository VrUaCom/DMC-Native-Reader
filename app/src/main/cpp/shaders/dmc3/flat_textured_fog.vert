#version 300 es
precision highp float;
precision highp int;

// Flat textured geometry (white colour) with a per-vertex fog amount.

layout(location = 0) in vec3 a_position;
layout(location = 2) in vec2 a_uv;   // fixed-point, units of 1/16
layout(location = 6) in float a_fog;

uniform mat4 u_transform;
uniform vec2 u_uv_scale;
uniform vec3 u_fog_colour;

out vec2 v_uv;
out vec4 v_colour;
out vec4 v_fog;

void main()
{
    // Transform to clip space (no screen mapping).
    gl_Position = u_transform * vec4(a_position, 1.0);

    // Convert fixed-point texture coordinates and apply the scale.
    v_uv = a_uv / 16.0 * u_uv_scale;

    // Flat white colour.
    v_colour = vec4(1.0);

    // Fog colour in rgb, fog amount in a, clamped to 0..1.
    v_fog = clamp(vec4(u_fog_colour, a_fog), 0.0, 1.0);
}
