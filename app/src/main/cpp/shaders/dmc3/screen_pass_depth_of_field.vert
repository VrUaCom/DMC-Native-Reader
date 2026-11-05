#version 300 es
precision highp float;
precision highp int;

// Full-screen vertex shader for the depth-of-field pass.

layout(location = 0) in vec4 a_position;
layout(location = 2) in vec2 a_uv; // units of 1/16

uniform mat4 u_transform;
uniform vec2 u_uv_scale;
uniform vec4 u_dof;    // (far end, far start, near start, near end) as depths
uniform vec4 u_colour;

out vec2 v_uv;
out vec4 v_colour;
out vec4 v_dof_limits;
out vec2 v_screen_uv;

void main()
{
    gl_Position = u_transform * a_position;

    v_uv = a_uv / 16.0 * u_uv_scale;

    // Slopes and offsets so the fragment shader's ramps run 0..1 across
    // the far (start to end) and near (start to end) ranges.
    const float e = 0.0000001;
    float sx = 1.0 / (u_dof.x - u_dof.y + e);
    float sz = 1.0 / (u_dof.z - u_dof.w + e);
    v_dof_limits = vec4(sx, -u_dof.y * sx, -sz, u_dof.z * sz);

    v_colour = clamp(u_colour, 0.0, 1.0);

    // Positions have w = 1, so clip xy maps straight to 0..1.
    v_screen_uv = gl_Position.xy * 0.5 + 0.5;
}
