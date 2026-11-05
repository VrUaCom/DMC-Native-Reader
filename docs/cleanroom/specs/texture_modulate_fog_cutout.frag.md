# Texture × colour, fog, cut-out alpha

Shader: `texture_modulate_fog_cutout.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for a tinted texture with fog and cut-out transparency.

Inputs: `v_uv`, `v_colour`, `v_fog`.
Uniforms: `uniform sampler2D u_texture`.

- `t = texture(u_texture, v_uv)`.
- `c = t * v_colour * 2.0` (all four channels).
- On c.rgb: Fog: `rgb = mix(v_fog.rgb, rgb, v_fog.a)`.
- Then on c.a: Cut-out: alpha becomes 1.0 when it is at least 0.95, else 0.0.

Alpha test: yes (CONVENTIONS.md), on the final alpha.
