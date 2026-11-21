#version 300 es
// Skinned effect model, lit, tinted by vertex colour, light scaled by a sheen.
precision highp float;
precision highp int;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_colour;
layout(location = 4) in ivec3 a_bone_rows;
layout(location = 5) in float a_weights_packed;

uniform vec4 u_bone_rows[176];
uniform mat4 u_view_projection;
uniform vec4 u_screen_scale;
uniform vec4 u_screen_offset;
uniform mat4 u_light_directions;
uniform mat4 u_light_colours;
uniform vec4 u_sheen;
uniform float u_sheen_power;
uniform vec4 u_part_colour;
uniform float u_part_alpha;
uniform vec2 u_uv_scroll;
uniform vec2 u_uv_scale;
uniform vec3 u_fog_colour;

out vec2 v_uv;
out vec4 v_colour;
out vec4 v_fog;

void main() {
    // Decode the three 5-bit weight fields with float arithmetic.
    float wp = a_weights_packed;
    float f1 = wp - 32.0 * floor(wp / 32.0);
    float hi = floor(wp / 32.0);
    float f2 = hi - 32.0 * floor(hi / 32.0);
    float hi2 = floor(hi / 32.0);
    float f3 = hi2 - 32.0 * floor(hi2 / 32.0);

    int b1 = a_bone_rows.x;
    int b2 = a_bone_rows.y;
    int b3 = a_bone_rows.z;

    vec4 r0 = u_bone_rows[b1];
    vec4 r1 = u_bone_rows[b1 + 1];
    vec4 r2 = u_bone_rows[b1 + 2];
    vec4 r3 = u_bone_rows[b1 + 3];

    // Blend the rows of three bones unless only the first bone is used.
    if (f2 != 0.0 || f3 != 0.0) {
        float w1 = f1 / 31.0;
        float w2 = f2 / 31.0;
        float w3 = f3 / 31.0;
        r0 = w1 * r0 + w2 * u_bone_rows[b2]     + w3 * u_bone_rows[b3];
        r1 = w1 * r1 + w2 * u_bone_rows[b2 + 1] + w3 * u_bone_rows[b3 + 1];
        r2 = w1 * r2 + w2 * u_bone_rows[b2 + 2] + w3 * u_bone_rows[b3 + 2];
        r3 = w1 * r3 + w2 * u_bone_rows[b2 + 3] + w3 * u_bone_rows[b3 + 3];
    }

    vec4 skinned_pos = a_position.x * r0 + a_position.y * r1 + a_position.z * r2 + r3;
    vec3 skinned_nrm = (a_normal.x * r0 + a_normal.y * r1 + a_normal.z * r2).xyz;

    // Clip position with screen mapping.
    vec4 c = u_view_projection * skinned_pos;
    c = c * u_screen_scale + u_screen_offset * c.w;
    gl_Position = c;

    // Light intensities and colour.
    vec4 intensity = clamp(u_light_directions * vec4(skinned_nrm, 1.0), 0.0, 1.0);
    vec3 light = (u_light_colours * intensity).rgb;

    // Sheen factor: s, multiplied by s once per counter step below the power.
    float s = dot(skinned_nrm, u_sheen.xyz) * u_sheen.w;
    float sheen = s;
    for (float i = 0.0; i < u_sheen_power; i += 1.0) {
        sheen *= s;
    }
    light *= sheen;

    // Vertex colour: a stored 128 means 1.
    v_colour = clamp(vec4(light * u_part_colour.rgb * a_colour.rgb * (255.0 / 128.0),
                          u_part_alpha), 0.0, 1.0);

    // Texture coordinates from 1/4096 fixed point.
    v_uv = (a_uv + u_uv_scroll) / 4096.0 * u_uv_scale;

    v_fog = clamp(vec4(u_fog_colour, u_part_colour.w), 0.0, 1.0);
}
