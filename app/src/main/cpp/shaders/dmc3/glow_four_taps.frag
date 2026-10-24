#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Soft glow built from four nearby reads of a picture.

uniform sampler2D u_texture;

in vec2 v_uv;
in vec4 v_colour;
in vec4 v_param1; // tap offsets: xy first pair, zw second pair

layout(location = 0) out vec4 o_colour;

void main() {
    vec4 p = v_uv.xyxy + v_param1;

    // Blend the four taps in turn, clamping after each step.
    vec4 c = texture(u_texture, p.xy);
    c = clamp(mix(c, texture(u_texture, p.zw), 0.5), 0.0, 1.0);
    c = clamp(mix(c, texture(u_texture, p.xw), 0.375), 0.0, 1.0);
    c = clamp(mix(c, texture(u_texture, p.zy), 0.25), 0.0, 1.0);

    // Tint with the vertex colour (x2 rule); alpha is always 1.
    o_colour = vec4(c.rgb * v_colour.rgb * 2.0, 1.0);
}
