#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Copies a picture and its depth.

in vec2 v_uv;

uniform sampler2D u_texture;
uniform sampler2D u_depth_texture;

layout(location = 0) out vec4 o_colour;

void main()
{
    // Depth comes from the red channel of the depth texture.
    gl_FragDepth = texture(u_depth_texture, v_uv).r;
    o_colour = texture(u_texture, v_uv);
}
