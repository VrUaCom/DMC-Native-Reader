# Full-screen pass, coordinates only

Shader: `screen_pass_plain.vert` (GLSL ES 3.00, CONVENTIONS.md).

A vertex shader for a full-screen pass that only needs texture coordinates.

Inputs: `a_position` (vec4), `a_uv` (vec2). Outputs: `gl_Position`, `v_uv`.

- `a_position` (vec4) is already a clip position: `gl_Position = a_position`.
- `a_uv` (vec2) is in fixed-point units of 1/16: `v_uv = a_uv / 16.0`.
