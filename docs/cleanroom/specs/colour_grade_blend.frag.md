# Colour grading, blend

Shader: `colour_grade_blend.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for colour grading: masked pixels move toward a colour table entry chosen by their green.

Inputs: `v_uv`.
Uniforms: `uniform sampler2D u_texture`, `uniform sampler2D u_object_ids` (a table, CONVENTIONS.md, looked up by a pixel's alpha), `uniform sampler2D u_grade` (a table, looked up by a pixel's green).

- `p = texture(u_texture, v_uv)`.
- When `u_object_ids at p.a` has an alpha above 0: `g = u_grade at p.g`; rgb = `p.rgb + (g.rgb - p.rgb) * (g.a * 2.0)`.
- Otherwise rgb = p.rgb.
- alpha = 1.0.

No alpha test.
