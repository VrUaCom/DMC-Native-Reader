# Directional glow

Shader: `directional_glow.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for a glow mixed between the picture and a moved, tinted copy of it.

Inputs: `v_uv`, `v_uv2`, `v_colour`.
Uniforms: `uniform sampler2D u_texture`.

- `step = vec2(1.0 / 360.0, 1.0 / 180.0)`.
- `a = (texture at v_uv + texture at v_uv + step).rgb * 0.5`.
- `b = (texture at v_uv2 + texture at v_uv2 + step).rgb * v_colour.rgb` (a sum, not halved).
- rgb = `mix(a, b, clamp(v_colour.a * 2.0))`; alpha = 1.0.

No alpha test.
