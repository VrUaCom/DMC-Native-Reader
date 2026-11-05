# Copy colour and depth

Shader: `copy_colour_and_depth.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader that copies a picture and its depth.

Inputs: `v_uv`. Uniforms: `uniform sampler2D u_texture`, `uniform sampler2D u_depth_texture`.

- `gl_FragDepth = texture(u_depth_texture, v_uv).r`.
- The colour is `texture(u_texture, v_uv)`.

No alpha test.
