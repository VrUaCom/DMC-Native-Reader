# Object edges

Shader: `object_edge.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for finding where one object meets another: the object ids of a pixel and its neighbour half a pixel away differ.

Inputs: `v_uv`.
Uniforms: `uniform sampler2D u_texture`, `uniform sampler2D u_object_ids` (a table, CONVENTIONS.md, looked up by a pixel's alpha), `uniform vec2 u_texel` (1 / the picture's size); half a pixel `h = u_texel * 0.5`.

- `a = texture at v_uv`, `b = texture at v_uv + h`.
- `ia = u_object_ids at a.a`, `ib = u_object_ids at b.a`; `diff = abs(ia - ib)`; `t = diff.r + diff.b`.
- rgb = 0.0; alpha = `clamp(t * 255.0)`.

No alpha test.
