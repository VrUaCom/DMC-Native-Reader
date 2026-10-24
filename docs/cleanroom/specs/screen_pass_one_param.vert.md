# Full-screen pass, one parameter

Shader: `screen_pass_one_param.vert` (GLSL ES 3.00, CONVENTIONS.md).

A vertex shader for a full-screen pass that hands one parameter vector to its fragment shader.

Inputs: `a_position` (vec4), `a_uv` (vec2). Outputs: `gl_Position`, `v_uv`, `v_colour`, `v_param1`.

- `a_position` (vec4) is already a clip position: `gl_Position = a_position`.
- `a_uv` (vec2) is in fixed-point units of 1/16: `v_uv = a_uv / 16.0`.
- `uniform vec4 u_param1`: `v_param1 = u_param1`.
- `uniform vec4 u_colour`: `v_colour = clamp(u_colour)`.
