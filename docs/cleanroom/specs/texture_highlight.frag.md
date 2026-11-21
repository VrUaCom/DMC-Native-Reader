# Texture × colour + highlight

Shader: `texture_highlight.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for a tinted texture brightened by the colour's alpha.

Inputs: `v_uv`, `v_colour`.
Uniforms: `uniform sampler2D u_texture`.

- `t = texture(u_texture, v_uv)`.
- `c = t * v_colour * 2.0 + vec4(v_colour.a)` (all four channels: the colour's alpha is added to each).
- The colour is c.

Alpha test: yes (CONVENTIONS.md), on the final alpha.
