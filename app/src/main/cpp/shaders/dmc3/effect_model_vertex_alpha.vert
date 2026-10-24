#version 300 es
// Skinned effect model: lit, tinted by vertex colour, alpha from the vertices.
precision highp float;
precision highp int;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_colour;
layout(location = 4) in ivec3 a_bone_rows;
layout(location = 5) in float a_weights_packed;

// Bone matrices, four consecutive rows per bone (44 bones).
uniform vec4 u_bone_rows[176];
uniform mat4 u_view_projection;
uniform vec4 u_screen_scale;
uniform vec4 u_screen_offset;
uniform mat4 u_light_directions;
uniform mat4 u_light_colours;
uniform vec4 u_part_colour;   // rgb: part colour, w: part fog amount
uniform float u_part_alpha;   // part of the interface; not used here
uniform vec2 u_uv_scroll;
uniform vec2 u_uv_scale;
uniform vec3 u_fog_colour;

out vec2 v_uv;
out vec4 v_colour;
out vec4 v_fog;

void main()
{
    // Decode the three 5-bit weight fields with float arithmetic.
    float wp = a_weights_packed;
    float f1 = mod(wp, 32.0);
    float f2 = mod(floor(wp / 32.0), 32.0);
    float f3 = mod(floor(wp / 1024.0), 32.0);

    int b1 = a_bone_rows.x;
    vec4 r0 = u_bone_rows[b1 + 0];
    vec4 r1 = u_bone_rows[b1 + 1];
    vec4 r2 = u_bone_rows[b1 + 2];
    vec4 r3 = u_bone_rows[b1 + 3];

    if (f2 != 0.0 || f3 != 0.0) {
        // Blend the corresponding rows of the three bones.
        float w1 = f1 / 31.0;
        float w2 = f2 / 31.0;
        float w3 = f3 / 31.0;
        int b2 = a_bone_rows.y;
        int b3 = a_bone_rows.z;
        r0 = w1 * r0 + w2 * u_bone_rows[b2 + 0] + w3 * u_bone_rows[b3 + 0];
        r1 = w1 * r1 + w2 * u_bone_rows[b2 + 1] + w3 * u_bone_rows[b3 + 1];
        r2 = w1 * r2 + w2 * u_bone_rows[b2 + 2] + w3 * u_bone_rows[b3 + 2];
        r3 = w1 * r3 + w2 * u_bone_rows[b2 + 3] + w3 * u_bone_rows[b3 + 3];
    }

    vec4 skinned_position = a_position.x * r0 + a_position.y * r1
                          + a_position.z * r2 + r3;
    vec4 skinned_normal = a_normal.x * r0 + a_normal.y * r1 + a_normal.z * r2;

    // Clip position, then the screen mapping.
    vec4 c = u_view_projection * skinned_position;
    c = c * u_screen_scale + u_screen_offset * c.w;
    gl_Position = c;

    // Light intensities and light colour.
    vec4 intensity = u_light_directions * vec4(skinned_normal.xyz, 1.0);
    intensity = clamp(intensity, 0.0, 1.0);
    vec4 light_colour = u_light_colours * intensity;

    // Colour: stored 128 means 1; alpha is the stored byte as it is.
    vec4 colour;
    colour.rgb = light_colour.rgb * u_part_colour.rgb * a_colour.rgb * (255.0 / 128.0);
    colour.a = a_colour.a * 255.0;
    v_colour = clamp(colour, 0.0, 1.0);

    // Texture coordinates from 1/4096 fixed point.
    v_uv = (a_uv + u_uv_scroll) / 4096.0 * u_uv_scale;

    // Fog colour and the part's fog amount.
    v_fog = clamp(vec4(u_fog_colour, u_part_colour.w), 0.0, 1.0);
}
