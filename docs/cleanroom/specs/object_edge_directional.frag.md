# Object edges, per direction

Shader: `object_edge_directional.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for finding object edges toward three neighbours, one per channel.

Inputs: `v_uv`, `v_param1` (xy: the step to the neighbours).
Uniforms: `uniform sampler2D u_texture`, `uniform sampler2D u_object_ids` (a table, CONVENTIONS.md, looked up by a pixel's alpha).

- `o = v_param1.xy`. Read `p = texture at v_uv`, `pr = at v_uv - vec2(o.x, 0)`, `pb = at v_uv - o`, `pg = at v_uv - vec2(0, o.y)`.
- `i0 = (u_object_ids at p.a).r`; `ir = (u_object_ids at pr.a).r`, `ig = (u_object_ids at pg.a).g`, `ib = (u_object_ids at pb.a).b`.
- r = `fract(ir - i0)`, g = `fract(ig - i0)`, b = `fract(ib - i0)` (all against the centre's red).
- alpha = `clamp((r + g + b) * 255.0)`.

No alpha test.
