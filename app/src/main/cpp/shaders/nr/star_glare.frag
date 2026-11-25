#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Star glare: weighted reads of a picture stepped along a direction.

in vec2 v_uv;
in vec4 v_colour;
in vec4 v_param1; // step: xy the first, zw the further one
in vec4 v_param2; // four weights

uniform sampler2D u_texture;

layout(location = 0) out vec4 o_colour;

void main()
{
    // Centre read.
    vec4 c = texture(u_texture, v_uv) * v_param2.x;

    // Reads one first step and one further step back.
    vec4 p = v_uv.xyxy - v_param1;
    c += texture(u_texture, p.xy) * v_param2.y;
    c += texture(u_texture, p.zw) * v_param2.z;

    // Step back again by the further step.
    p -= v_param1.zwzw;
    c += texture(u_texture, p.xy) * v_param2.w;

    o_colour = vec4(c.rgb * v_colour.rgb * 2.0, 1.0);
}
