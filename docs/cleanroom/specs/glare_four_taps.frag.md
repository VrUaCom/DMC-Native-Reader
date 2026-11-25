# Glare from four taps

Shader: `glare_four_taps.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for a glare: the sum of four nearby reads of a picture.

Inputs: `v_uv`, `v_colour`, `v_param1` (the offsets).
Uniforms: `uniform sampler2D u_texture`.

- `p = v_uv.xyxy + v_param1`.
- `c = texture at p.xy + at p.zw + at p.xw + at p.zy` (a sum, not a mean).
- rgb = `c.rgb * v_colour.rgb`; alpha = 1.0.

No alpha test.
