#version 300 es
// Stage geometry: four point lights plus ambient, tinted by vertex colour, no fog.
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

// One light per component.
uniform vec4 u_light_x;
uniform vec4 u_light_y;
uniform vec4 u_light_z;
uniform vec4 u_light_falloff;
uniform vec4 u_light_colour[4];
uniform vec3 u_ambient;

uniform float u_part_alpha;

out vec2 v_uv;
out vec4 v_colour;
out vec4 v_fog;

void main() {
    // Position, then screen mapping.
    vec4 c = u_world_view_projection * vec4(a_position, 1.0);
    c = c * u_screen_scale + u_screen_offset * c.w;
    gl_Position = c;

    // Texture coordinates from fixed point.
    v_uv = (a_uv + u_uv_scroll) / 4096.0 * u_uv_scale;

    // Vectors from the vertex to the four lights (unnormalised).
    vec4 dx = u_light_x - a_position.x;
    vec4 dy = u_light_y - a_position.y;
    vec4 dz = u_light_z - a_position.z;

    vec4 dist2 = dx * dx + dy * dy + dz * dz;
    vec4 atten = clamp(1.0 - dist2 * u_light_falloff, 0.0, 1.0);
    vec4 facing = dx * a_normal.x + dy * a_normal.y + dz * a_normal.z;
    vec4 w = max(facing * atten, 0.0);

    vec4 light = u_light_colour[0] * w.x
               + u_light_colour[1] * w.y
               + u_light_colour[2] * w.z
               + u_light_colour[3] * w.w;
    light.rgb += u_ambient;

    // Tint by vertex colour, alpha from the part.
    v_colour = vec4(light.rgb * a_colour.rgb / 255.0, u_part_alpha);
    v_colour = clamp(v_colour, 0.0, 1.0);

    // No fog.
    v_fog = vec4(0.0, 0.0, 0.0, 1.0);
}
