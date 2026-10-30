# Recoloured texture × colour

Shader: `texture_recolour.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for a texture recoloured by its brightness (between a low and a high colour) and tinted.

Inputs: `v_uv`, `v_colour`.
Uniforms: `uniform sampler2D u_texture`, `u_recolour_low`, `u_recolour_high`.

- `t = texture(u_texture, v_uv)`.
- `uniform vec4 u_recolour_low`, `uniform vec4 u_recolour_high`. Brightness `b = (t.r + t.g + t.b) * 0.66666666`; `rate = mix(u_recolour_low, u_recolour_high, b)`; recoloured `r = mix(t.rgb, rate.rgb, rate.a)`.
- `c = vec4(r, t.a) * v_colour * 2.0` (all four channels).
- The colour is c.

Alpha test: yes (CONVENTIONS.md), on the final alpha.
