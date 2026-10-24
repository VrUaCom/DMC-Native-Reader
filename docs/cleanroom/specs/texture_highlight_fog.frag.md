# Texture × colour + highlight, fog

Shader: `texture_highlight_fog.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for a tinted, brightened texture with fog.

Inputs: `v_uv`, `v_colour`, `v_fog`.
Uniforms: `uniform sampler2D u_texture`.

- `t = texture(u_texture, v_uv)`.
- `c = t * v_colour * 2.0 + vec4(v_colour.a)` (all four channels: the colour's alpha is added to each).
- On c.rgb: Fog: `rgb = mix(v_fog.rgb, rgb, v_fog.a)`.

Alpha test: yes (CONVENTIONS.md), on the final alpha.
