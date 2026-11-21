# Shadow volume

Shader: `shadow_volume.vert` (GLSL ES 3.00, CONVENTIONS.md).

A vertex shader for shadow volumes whose positions the engine has already
put on the screen.

Input: `a_position` (vec3). Outputs: `gl_Position`, `v_uv`, `v_colour`,
`v_fog`.

- `gl_Position`: `vec4(a_position, 1.0)` through the screen mapping
  (CONVENTIONS.md).
- `uniform vec4 u_shadow_colour`: `v_colour = u_shadow_colour` (as it is,
  not clamped).
- `v_uv = vec2(0.0)`, `v_fog = vec4(0.0, 0.0, 0.0, 1.0)`.
