# Edge mask

Shader: `edge_mask.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for a mask that is dark where a pixel differs from its neighbour half a pixel away.

Inputs: `v_uv`.
Uniforms: `uniform sampler2D u_texture`, `uniform vec2 u_texel` (1 / the picture's size); half a pixel `h = u_texel * 0.5`.

- `a = texture at v_uv`, `b = texture at v_uv + h`.
- `diff = abs(a - b)`; rgb = `vec3(1.0 - clamp(diff.r + diff.g + diff.b) * 2.0)`; alpha = 1.0.

No alpha test.
