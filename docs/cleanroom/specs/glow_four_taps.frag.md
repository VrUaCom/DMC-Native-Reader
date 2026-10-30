# Glow from four taps

Shader: `glow_four_taps.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for building a soft glow from four nearby reads of a picture.

Inputs: `v_uv`, `v_colour`, `v_param1` (the offsets).
Uniforms: `uniform sampler2D u_texture`.

- `p = v_uv.xyxy + v_param1`.
- Start with `c = texture at p.xy`.
- `c = clamp(mix(c, texture at p.zw, 0.5))`; then `c = clamp(mix(c, texture at p.xw, 0.375))`; then `c = clamp(mix(c, texture at p.zy, 0.25))`.
- rgb = `c.rgb * v_colour.rgb * 2.0`; alpha = 1.0.

No alpha test.
