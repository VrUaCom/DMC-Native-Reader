#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Texture with fog and cut-out alpha.

in vec2 v_uv;
in vec4 v_fog;

uniform sampler2D u_texture;

layout(location = 0) out vec4 o_colour;

void main() {
    vec4 t = texture(u_texture, v_uv);

    // Fog: amount 1 keeps the texel colour, 0 gives the fog colour.
    vec3 rgb = mix(v_fog.rgb, t.rgb, v_fog.a);

    // Cut-out: alpha becomes 1 when at least 0.95, else 0.
    float alpha = t.a >= 0.95 ? 1.0 : 0.0;

    o_colour = vec4(rgb, alpha);
}
