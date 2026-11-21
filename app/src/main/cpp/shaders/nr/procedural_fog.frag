#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Moving fog: four octaves of a noise texture mapped through a palette.

in vec2 v_uv;
in vec4 v_param1; // xy: base offset, zw: base scale

uniform sampler2D u_texture; // noise
uniform sampler2D u_palette; // table texture, one row high

layout(location = 0) out vec4 o_colour;

void main() {
    // Octaves 1 and 2.
    vec4 o = v_param1.xyxy * vec4(1.0, 1.0, 2.0, 2.0);
    vec4 sc = v_param1.zwzw * vec4(1.0, 1.0, 2.0, 2.0);
    vec4 p = v_uv.xyxy * sc + o;
    vec4 c = texture(u_texture, p.xy) * 0.5 + texture(u_texture, p.zw) * 0.25;

    // Octaves 3 and 4.
    o *= vec4(3.0, 3.0, 2.0, 2.0);
    sc *= vec4(3.0, 3.0, 2.0, 2.0);
    p = v_uv.xyxy * sc + o;
    c += texture(u_texture, p.xy) * 0.125 + texture(u_texture, p.zw) * 0.0625;

    // Palette lookup by the summed red channel, alpha doubled.
    vec4 colour = texture(u_palette, vec2(c.r, 0.5));
    colour.a *= 2.0;
    o_colour = colour;
}
