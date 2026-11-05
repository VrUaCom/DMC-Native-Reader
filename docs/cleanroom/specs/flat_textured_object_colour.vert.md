# Flat geometry, textured, one colour

Shader: `flat_textured_object_colour.vert` (GLSL ES 3.00, CONVENTIONS.md).

A vertex shader for flat textured geometry tinted by one colour.

Inputs: `a_position` (vec3), `a_uv` (vec2). Outputs: `gl_Position`, `v_uv`, `v_colour`, `v_fog`.

- `uniform mat4 u_transform`: `gl_Position = u_transform * vec4(a_position, 1.0)` (no screen mapping).
- `a_uv` is in fixed-point units of 1/16; `uniform vec2 u_uv_scale`: `v_uv = a_uv / 16.0 * u_uv_scale`.
- `uniform vec4 u_colour`: `v_colour = clamp(u_colour)`.
- `v_fog = vec4(0.0)`.
