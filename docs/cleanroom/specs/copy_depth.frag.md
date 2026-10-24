# Copy depth

Shader: `copy_depth.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader that copies a depth picture into the depth buffer.

Inputs: `v_uv`. Uniforms: `uniform sampler2D u_depth_texture`.

- `gl_FragDepth = texture(u_depth_texture, v_uv).r`.
- It writes no colour (declare no colour output).

No alpha test.
