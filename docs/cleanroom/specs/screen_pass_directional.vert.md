# Full-screen pass, two coordinate sets

Shader: `screen_pass_directional.vert` (GLSL ES 3.00, CONVENTIONS.md).

A vertex shader for a full-screen pass that reads its texture at two places: as it is, and moved and scaled.

Inputs: `a_position` (vec4), `a_uv` (vec2). Outputs: `gl_Position`, `v_uv`, `v_uv2`, `v_colour`.

- `a_position` (vec4) is already a clip position: `gl_Position = a_position`.
- `a_uv` (vec2) is in fixed-point units of 1/16: `v_uv = a_uv / 16.0`.
- `uniform vec4 u_param`: `v_uv2 = v_uv * u_param.zw + u_param.xy`.
- `uniform vec4 u_colour`: `v_colour = clamp(u_colour)`.
