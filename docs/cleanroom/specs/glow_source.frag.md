# Glow source

Shader: `glow_source.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for picking out the parts of a picture that glow, by two tables.

Inputs: `v_uv`.
Uniforms: `uniform sampler2D u_texture`, `uniform sampler2D u_glow_scale` (a table, looked up by a pixel's green), `uniform sampler2D u_object_ids` (a table, CONVENTIONS.md, looked up by a pixel's alpha).

- `p = texture(u_texture, v_uv)`.
- `a = (u_glow_scale at p.g).a`; rgb = `clamp(p.rgb * a * 2.0)`.
- `b = (u_object_ids at p.a).a`; rgb = `clamp(rgb * b * 2.0)`.
- alpha = 1.0.

No alpha test.
