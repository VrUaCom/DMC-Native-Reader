# Texture × colour

Shader: `texture_modulate.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for a texture tinted by the vertex colour (0.5 leaves it as it is).

Inputs: `v_uv`, `v_colour`.
Uniforms: `uniform sampler2D u_texture`.

- `t = texture(u_texture, v_uv)`.
- `c = t * v_colour * 2.0` (all four channels).
- The colour is c.

Alpha test: yes (CONVENTIONS.md), on the final alpha.
