# Recoloured texture × colour, fog

Shader: `texture_recolour_fog.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for the recoloured, tinted texture with fog.

Inputs: `v_uv`, `v_colour`, `v_fog`.
Uniforms: `uniform sampler2D u_texture`, `u_recolour_low`, `u_recolour_high`.

- `t = texture(u_texture, v_uv)`.
- `uniform vec4 u_recolour_low`, `uniform vec4 u_recolour_high`. Brightness `b = (t.r + t.g + t.b) * 0.66666666`; `rate = mix(u_recolour_low, u_recolour_high, b)`; recoloured `r = mix(t.rgb, rate.rgb, rate.a)`.
- `c = vec4(r, t.a) * v_colour * 2.0`.
- On c.rgb: Fog: `rgb = mix(v_fog.rgb, rgb, clamp(v_fog.a))`.

Alpha test: yes (CONVENTIONS.md), on the final alpha.
