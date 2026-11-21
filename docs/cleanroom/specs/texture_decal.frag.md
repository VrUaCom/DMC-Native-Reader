# Texture as it is

Shader: `texture_decal.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for a surface showing its texture unchanged.

Inputs: `v_uv`.
Uniforms: `uniform sampler2D u_texture`.

- `t = texture(u_texture, v_uv)`; the colour is t.

Alpha test: yes (CONVENTIONS.md), on the final alpha.
