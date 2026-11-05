# Flat geometry, one colour

Shader: `flat_object_colour.vert` (GLSL ES 3.00, CONVENTIONS.md).

A vertex shader for flat geometry (menus, 2D shapes) in one colour.

Inputs: `a_position` (vec3). Outputs: `gl_Position`, `v_colour`.

- `uniform mat4 u_transform`: `gl_Position = u_transform * vec4(a_position, 1.0)` (no screen mapping).
- `uniform vec4 u_colour`: `v_colour = clamp(u_colour)`.
