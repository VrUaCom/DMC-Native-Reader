# Colour grading, replace

Shader: `colour_grade_mask.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for colour grading: masked pixels are replaced by a colour table entry chosen by their green.

Inputs: `v_uv`.
Uniforms: `uniform sampler2D u_texture`, `uniform sampler2D u_object_ids` (a table, CONVENTIONS.md, looked up by a pixel's alpha), `uniform sampler2D u_grade` (a table, looked up by a pixel's green).

- `p = texture(u_texture, v_uv)`.
- When `u_object_ids at p.a` has an alpha above 0: the colour is `u_grade at p.g`, its alpha doubled.
- Otherwise the colour is `vec4(0.0)`.

No alpha test.
