# Texture with fog

Shader: `texture_decal_fog.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for a texture with fog.

Inputs: `v_uv`, `v_fog`.
Uniforms: `uniform sampler2D u_texture`.

- `t = texture(u_texture, v_uv)`.
- rgb = t.rgb, then Fog: `rgb = mix(v_fog.rgb, rgb, v_fog.a)`.
- alpha = t.a.

No alpha test.
