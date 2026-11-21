#version 300 es
precision highp float;
precision highp int;

// Shadow volume: positions are already on the screen.

layout(location = 0) in vec3 a_position;

uniform vec4 u_screen_scale;
uniform vec4 u_screen_offset;
uniform vec4 u_shadow_colour;

out vec2 v_uv;
out vec4 v_colour;
out vec4 v_fog;

void main() {
    // Clip position from the screen-space position, then the screen mapping.
    vec4 c = vec4(a_position, 1.0);
    c = c * u_screen_scale + u_screen_offset * c.w;
    gl_Position = c;

    // Shadow colour as given, not clamped.
    v_colour = u_shadow_colour;
    v_uv = vec2(0.0);
    // No fog.
    v_fog = vec4(0.0, 0.0, 0.0, 1.0);
}
