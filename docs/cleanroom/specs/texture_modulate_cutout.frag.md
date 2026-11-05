# Texture × colour, cut-out alpha

Shader: `texture_modulate_cutout.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for a tinted texture with cut-out transparency.

Inputs: `v_uv`, `v_colour`.
Uniforms: `uniform sampler2D u_texture`.

- `t = texture(u_texture, v_uv)`.
- `c = t * v_colour * 2.0` (all four channels).
- Then on c.a: Cut-out: alpha becomes 1.0 when it is at least 0.95, else 0.0.

Alpha test: yes (CONVENTIONS.md), on the final alpha.
