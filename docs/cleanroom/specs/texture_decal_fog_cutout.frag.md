# Texture with fog, cut-out alpha

Shader: `texture_decal_fog_cutout.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for a texture with fog and cut-out transparency.

Inputs: `v_uv`, `v_fog`.
Uniforms: `uniform sampler2D u_texture`.

- `t = texture(u_texture, v_uv)`.
- rgb = t.rgb, then Fog: `rgb = mix(v_fog.rgb, rgb, v_fog.a)`.
- alpha = t.a, then Cut-out: alpha becomes 1.0 when it is at least 0.95, else 0.0.

No alpha test.
