# Particle quad

Shader: `particle.vert` (GLSL ES 3.00, CONVENTIONS.md).

A vertex shader for particle quads: each corner picks its colour and texture coordinates from a four-entry table.

Outputs: `gl_Position`, `v_uv`, `v_colour`, `v_fog`.

Input: `a_position` (vec4): xyz the corner's offset, w the corner's number
0..3 (a whole number stored as a float).

- `uniform vec4 u_particle_rows[4]`: clip position =
  `x * rows[0] + y * rows[1] + z * rows[2] + rows[3]`, then the screen
  mapping (CONVENTIONS.md).
- `uniform vec4 u_corner_colour[4]`, `uniform vec4 u_corner_uv[4]`: with
  i = the corner's number, `v_colour = clamp(u_corner_colour[i])`,
  `v_uv = u_corner_uv[i].xy` (already 0..1).
- `v_fog = vec4(0.0, 0.0, 0.0, 1.0)`.
