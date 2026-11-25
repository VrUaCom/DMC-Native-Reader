#version 300 es
// Fog from depth through a 256-entry colour/amount table.
precision highp float;
precision highp int;
precision highp sampler2D;

in vec2 v_uv;

uniform sampler2D u_depth_texture;
uniform sampler2D u_fog_table;

layout(location = 0) out vec4 o_colour;

// Look up the one-row fog table at x.
vec4 fog_table(float x)
{
    return texture(u_fog_table, vec2(x, 0.5));
}

void main()
{
    float d = texture(u_depth_texture, v_uv).r * 257.0;
    float s = 1.0 / 256.0;

    vec4 left = fog_table(d - s);
    vec4 right = fog_table(d + s);
    vec4 mid = fog_table(d);

    // Weights from the fractional position between table entries.
    float k = d * 256.0;
    float wl = k - floor(k);
    float wr = ceil(k) - k;

    vec4 c = mix(mix(left, mid, wl), mix(right, mid, wr), 0.5);

    // Alpha doubled.
    o_colour = vec4(c.rgb, c.a * 2.0);
}
