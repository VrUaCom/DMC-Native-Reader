# 8-bit picture through a palette

Shader: `palette_lookup.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for an 8-bit (indexed) picture shown through its palette.

Inputs: `v_uv`.
Uniforms: `uniform sampler2D u_texture` (the indices in red), `uniform sampler2D u_palette` (a table, CONVENTIONS.md).

- `i = texture(u_texture, v_uv).r`; the colour is the palette at i.

No alpha test.
