# Texture, cut-out alpha

Shader: `texture_decal_cutout.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for a texture with cut-out (all or nothing) transparency.

Inputs: `v_uv`.
Uniforms: `uniform sampler2D u_texture`.

- `t = texture(u_texture, v_uv)`; the colour is t.
- Cut-out: alpha becomes 1.0 when it is at least 0.95, else 0.0. (on t.a)

Alpha test: yes (CONVENTIONS.md), on the final alpha.
