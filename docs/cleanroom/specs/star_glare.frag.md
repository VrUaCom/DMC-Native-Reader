# Star glare along a direction

Shader: `star_glare.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for a star-shaped glare: reads of a picture stepped along a direction, each with its own weight.

Inputs: `v_uv`, `v_colour`, `v_param1` (the step: xy the first, zw the further one), `v_param2` (four weights).
Uniforms: `uniform sampler2D u_texture`.

- `c = texture at v_uv * v_param2.x`.
- `p = v_uv.xyxy - v_param1`; `c += texture at p.xy * v_param2.y`; `c += texture at p.zw * v_param2.z`.
- `p -= v_param1.zwzw`; `c += texture at p.xy * v_param2.w`.
- rgb = `c.rgb * v_colour.rgb * 2.0`; alpha = 1.0.

No alpha test.
