#version 300 es
precision highp float;
precision highp int;

// Particle quad with distance fog; each corner picks colour and uv from a table.

layout(location = 0) in vec4 a_position; // xyz: corner offset, w: corner number 0..3

uniform vec4 u_particle_rows[4];
uniform vec4 u_corner_colour[4];
uniform vec4 u_corner_uv[4];
uniform vec3 u_fog_colour;
uniform vec2 u_fog_coeffs;
uniform vec4 u_screen_scale;
uniform vec4 u_screen_offset;

out vec2 v_uv;
out vec4 v_colour;
out vec4 v_fog;

void main() {
    // Clip position from the particle rows.
    vec4 c = a_position.x * u_particle_rows[0]
           + a_position.y * u_particle_rows[1]
           + a_position.z * u_particle_rows[2]
           + u_particle_rows[3];

    // Screen mapping.
    c = c * u_screen_scale + u_screen_offset * c.w;
    gl_Position = c;

    // Corner number, rounded to the nearest whole number.
    int i = int(a_position.w + 0.5);

    v_colour = clamp(u_corner_colour[i], 0.0, 1.0);
    v_uv = u_corner_uv[i].xy;

    // Distance fog from the mapped clip w.
    v_fog = clamp(vec4(u_fog_colour, u_fog_coeffs.x + u_fog_coeffs.y / abs(c.w)), 0.0, 1.0);
}
