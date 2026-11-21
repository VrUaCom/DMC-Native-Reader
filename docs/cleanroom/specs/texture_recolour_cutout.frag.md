# Recoloured texture × colour, cut-out alpha

Shader: `texture_recolour_cutout.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for the recoloured, tinted texture with cut-out transparency.

Inputs: `v_uv`, `v_colour`.
Uniforms: `uniform sampler2D u_texture`, `u_recolour_low`, `u_recolour_high`.

- `t = texture(u_texture, v_uv)`.
- `uniform vec4 u_recolour_low`, `uniform vec4 u_recolour_high`. Brightness `b = (t.r + t.g + t.b) * 0.66666666`; `rate = mix(u_recolour_low, u_recolour_high, b)`; recoloured `r = mix(t.rgb, rate.rgb, rate.a)`.
- rgb = `r * v_colour.rgb * 2.0`.
- alpha: from t.a (the texture's own, not tinted): Cut-out: alpha becomes 1.0 when it is at least 0.95, else 0.0.

Alpha test: yes (CONVENTIONS.md), on the final alpha.
