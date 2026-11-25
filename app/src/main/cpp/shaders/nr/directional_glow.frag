#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Directional glow: mixes the picture with a moved, tinted copy of it.

in vec2 v_uv;
in vec2 v_uv2;
in vec4 v_colour;

uniform sampler2D u_texture;

layout(location = 0) out vec4 o_colour;

void main()
{
    // Offset of the second tap (one texel of a 360x180 image).
    const vec2 uv_step = vec2(1.0 / 360.0, 1.0 / 180.0);

    // Picture: average of two taps.
    vec3 a = (texture(u_texture, v_uv).rgb + texture(u_texture, v_uv + uv_step).rgb) * 0.5;

    // Moved copy: sum of two taps, tinted by the vertex colour.
    vec3 b = (texture(u_texture, v_uv2).rgb + texture(u_texture, v_uv2 + uv_step).rgb) * v_colour.rgb;

    vec3 rgb = mix(a, b, clamp(v_colour.a * 2.0, 0.0, 1.0));
    o_colour = vec4(rgb, 1.0);
}
