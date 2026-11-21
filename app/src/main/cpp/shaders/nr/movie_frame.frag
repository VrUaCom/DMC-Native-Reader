#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Movie frame: the texture stores blue in red and red in blue.

in vec2 v_uv;

uniform sampler2D u_frame;

layout(location = 0) out vec4 o_colour;

void main()
{
    vec4 f = texture(u_frame, v_uv);
    // Swap red and blue back.
    o_colour = f.bgra;
}
