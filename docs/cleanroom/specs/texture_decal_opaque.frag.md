# Texture, opaque

Shader: `texture_decal_opaque.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for a texture drawn fully opaque.

Inputs: `v_uv`.
Uniforms: `uniform sampler2D u_texture`.

- `t = texture(u_texture, v_uv)`; the colour is `vec4(t.rgb, 1.0)`.

No alpha test.
