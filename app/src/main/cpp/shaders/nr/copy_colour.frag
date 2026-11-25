#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Copies a picture: outputs the texel at v_uv unchanged.

in vec2 v_uv;

uniform sampler2D u_texture;

layout(location = 0) out vec4 o_colour;

void main()
{
    o_colour = texture(u_texture, v_uv);
}
