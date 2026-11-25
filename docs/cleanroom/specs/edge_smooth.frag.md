# Edge smoothing

Shader: `edge_smooth.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for smoothing edges: each pixel moves toward its neighbour half a pixel away, the more the more they differ.

Inputs: `v_uv`.
Uniforms: `uniform sampler2D u_texture`, `uniform vec2 u_texel` (1 / the picture's size); half a pixel `h = u_texel * 0.5`.

- `a = texture at v_uv`, `b = texture at v_uv + h`.
- `diff = abs(a - b)`; `k = clamp((diff.r + diff.g + diff.b) * 4.0 + 0.2)`.
- rgb = `mix(a.rgb, b.rgb, k)`; alpha = 1.0.

No alpha test.
