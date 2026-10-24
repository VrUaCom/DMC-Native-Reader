#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Glare: the sum of four nearby reads of a picture, tinted by the colour.

uniform sampler2D u_texture;

in vec2 v_uv;
in vec4 v_colour;
in vec4 v_param1; // tap offsets

layout(location = 0) out vec4 o_colour;

void main()
{
    vec4 p = v_uv.xyxy + v_param1;

    // Sum of the four corner taps (not a mean).
    vec4 c = texture(u_texture, p.xy)
           + texture(u_texture, p.zw)
           + texture(u_texture, p.xw)
           + texture(u_texture, p.zy);

    o_colour = vec4(c.rgb * v_colour.rgb, 1.0);
}
