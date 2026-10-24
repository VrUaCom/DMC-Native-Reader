#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Depth of field: a blurred copy of the picture whose alpha grows away from the focus.

in vec2 v_uv;
in vec4 v_colour;
in vec4 v_dof_limits;
in vec2 v_screen_uv;

uniform sampler2D u_depth_texture;
uniform sampler2D u_texture;
uniform vec2 u_blur_offset;

layout(location = 0) out vec4 o_colour;

void main()
{
    // Scene depth at this pixel.
    float d = texture(u_depth_texture, v_screen_uv).r;

    // Blur amounts beyond the far limit and before the near limit.
    float far_amount = clamp(v_dof_limits.y + d * v_dof_limits.x, 0.0, 1.0);
    float near_amount = clamp(v_dof_limits.w + d * v_dof_limits.z, 0.0, 1.0);

    float s = far_amount + near_amount;
    float alpha = clamp(s * s * s, 0.0, 1.0);

    // Average of two samples, one shifted by the blur offset.
    vec3 rgb = (texture(u_texture, v_uv - u_blur_offset) + texture(u_texture, v_uv)).rgb * 0.5;

    o_colour = vec4(rgb, alpha) * v_colour;
}
