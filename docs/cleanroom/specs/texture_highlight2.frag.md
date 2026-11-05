# Texture × colour + highlight, texture alpha

Shader: `texture_highlight2.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for a tinted texture brightened by the colour's alpha, keeping the texture's alpha.

Inputs: `v_uv`, `v_colour`.
Uniforms: `uniform sampler2D u_texture`.

- `t = texture(u_texture, v_uv)`.
- `rgb = t.rgb * v_colour.rgb * 2.0 + v_colour.a`; alpha = t.a (the texture's own).

Alpha test: yes (CONVENTIONS.md), on the final alpha.
