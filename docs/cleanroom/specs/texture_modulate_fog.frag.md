# Texture × colour, fog

Shader: `texture_modulate_fog.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for a tinted texture with fog.

Inputs: `v_uv`, `v_colour`, `v_fog`.
Uniforms: `uniform sampler2D u_texture`.

- `t = texture(u_texture, v_uv)`.
- `c = t * v_colour * 2.0` (all four channels).
- On c.rgb: Fog: `rgb = mix(v_fog.rgb, rgb, v_fog.a)`.

Alpha test: yes (CONVENTIONS.md), on the final alpha.
