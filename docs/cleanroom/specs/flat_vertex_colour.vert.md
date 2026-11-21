# Flat geometry, vertex colours

Shader: `flat_vertex_colour.vert` (GLSL ES 3.00, CONVENTIONS.md).

A vertex shader for flat geometry coloured by its vertices.

Inputs: `a_position` (vec3), `a_colour` (vec4, 0..1). Outputs: `gl_Position`, `v_uv`, `v_colour`, `v_fog`.

- `uniform mat4 u_transform`: `gl_Position = u_transform * vec4(a_position, 1.0)` (no screen mapping).
- `v_colour = clamp(a_colour)`.
- `v_uv = vec2(0.0)`.
- `uniform vec3 u_fog_colour`: `v_fog = vec4(u_fog_colour, 1.0)`.
