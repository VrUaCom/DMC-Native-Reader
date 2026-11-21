# Texture × colour + highlight, texture alpha, fog

Shader: `texture_highlight2_fog.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for the same with fog.

Inputs: `v_uv`, `v_colour`, `v_fog`.
Uniforms: `uniform sampler2D u_texture`.

- `t = texture(u_texture, v_uv)`.
- `rgb = t.rgb * v_colour.rgb * 2.0 + v_colour.a`; alpha = t.a (the texture's own).
- On rgb: Fog: `rgb = mix(v_fog.rgb, rgb, v_fog.a)`.

Alpha test: yes (CONVENTIONS.md), on the final alpha.
