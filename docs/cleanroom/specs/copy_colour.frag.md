# Copy

Shader: `copy_colour.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for copying a picture.

Inputs: `v_uv`.
Uniforms: `uniform sampler2D u_texture`.

- The colour is `texture(u_texture, v_uv)`.

No alpha test.
