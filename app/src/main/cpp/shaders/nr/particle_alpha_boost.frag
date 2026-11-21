#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Particle texture with its alpha raised a lot.

in vec2 v_uv;

uniform sampler2D u_texture;

layout(location = 0) out vec4 o_colour;

void main()
{
    vec4 t = texture(u_texture, v_uv);
    // Keep the texel colour; scale alpha by 64 and clamp to 0..1.
    o_colour = vec4(t.rgb, clamp(t.a * 64.0, 0.0, 1.0));
}
