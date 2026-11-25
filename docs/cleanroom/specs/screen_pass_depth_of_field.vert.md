# Full-screen pass for depth of field

Shader: `screen_pass_depth_of_field.vert` (GLSL ES 3.00, CONVENTIONS.md).

A vertex shader for the depth-of-field pass.

Inputs: `a_position` (vec4), `a_uv` (vec2). Outputs: `gl_Position`, `v_uv`,
`v_colour`, `v_dof_limits`, `v_screen_uv`.

- `uniform mat4 u_transform`: `gl_Position = u_transform * a_position`.
- `a_uv` is in units of 1/16; `uniform vec2 u_uv_scale`:
  `v_uv = a_uv / 16.0 * u_uv_scale`.
- `uniform vec4 u_dof` = (far end, far start, near start, near end) as
  depth values. With `e = 0.0000001`:
  `sx = 1.0 / (u_dof.x - u_dof.y + e)`, `sz = 1.0 / (u_dof.z - u_dof.w + e)`;
  `v_dof_limits = vec4(sx, -u_dof.y * sx, -sz, u_dof.z * sz)`.
  (So the fragment shader's `clamp(limits.y + d * limits.x)` grows from 0 at
  the far start to 1 at the far end, and `clamp(limits.w + d * limits.z)`
  from 0 at the near start to 1 at the near end.)
- `uniform vec4 u_colour`: `v_colour = clamp(u_colour)`.
- `v_screen_uv = gl_Position.xy * 0.5 + 0.5` (the pass's positions have
  w = 1).
