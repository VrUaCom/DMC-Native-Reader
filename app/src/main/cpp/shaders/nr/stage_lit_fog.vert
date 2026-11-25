#version 300 es
// Stage geometry: four point lights plus ambient, vertex colour tint, distance fog.
precision highp float;
precision highp int;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;     // fixed point, 1/4096 units
layout(location = 3) in vec4 a_colour; // 0..255 units

uniform mat4 u_world_view_projection;
uniform vec4 u_screen_scale;
uniform vec4 u_screen_offset;

uniform vec2 u_uv_scroll; // 1/4096 units
uniform vec2 u_uv_scale;

uniform vec4 u_light_x;       // x of lights 0..3
uniform vec4 u_light_y;       // y of lights 0..3
uniform vec4 u_light_z;       // z of lights 0..3
uniform vec4 u_light_falloff; // falloff of lights 0..3
uniform vec4 u_light_colour[4];
uniform vec3 u_ambient;

uniform float u_part_alpha;

uniform vec3 u_fog_colour;
uniform vec2 u_fog_coeffs;

out vec2 v_uv;
out vec4 v_colour;
out vec4 v_fog;

void main() {
    // Clip position, then screen mapping.
    vec4 c = u_world_view_projection * vec4(a_position, 1.0);
    c = c * u_screen_scale + u_screen_offset * c.w;
    gl_Position = c;

    // Texture coordinates from fixed point.
    v_uv = (a_uv + u_uv_scroll) / 4096.0 * u_uv_scale;

    // Vectors to the four lights, one light per component (not normalised).
    vec4 dx = u_light_x - a_position.x;
    vec4 dy = u_light_y - a_position.y;
    vec4 dz = u_light_z - a_position.z;

    vec4 dist2 = dx * dx + dy * dy + dz * dz;
    vec4 atten = clamp(1.0 - dist2 * u_light_falloff, 0.0, 1.0);
    vec4 facing = dx * a_normal.x + dy * a_normal.y + dz * a_normal.z;
    vec4 w = max(facing * atten, 0.0);

    vec4 light = u_light_colour[0] * w.x + u_light_colour[1] * w.y +
                 u_light_colour[2] * w.z + u_light_colour[3] * w.w;
    light.rgb += u_ambient;

    // Vertex colour tint, part alpha.
    v_colour = clamp(vec4(light.rgb * a_colour.rgb / 255.0, u_part_alpha), 0.0, 1.0);

    // Distance fog from the mapped clip w.
    v_fog = clamp(vec4(u_fog_colour, u_fog_coeffs.x + u_fog_coeffs.y / abs(c.w)), 0.0, 1.0);
}
