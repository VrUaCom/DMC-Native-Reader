# Fog from depth through a table

Shader: `depth_fog_table.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for fog whose colour and amount come from a 256-entry table indexed by depth.

Inputs: `v_uv`.
Uniforms: `uniform sampler2D u_depth_texture`, `uniform sampler2D u_fog_table` (a table, CONVENTIONS.md).

- `d = texture(u_depth_texture, v_uv).r * 257.0`; `s = 1.0 / 256.0`.
- `left = table at d - s`, `right = table at d + s`, `mid = table at d`.
- `k = d * 256.0`; `wl = k - floor(k)`; `wr = ceil(k) - k`.
- `c = mix(mix(left, mid, wl), mix(right, mid, wr), 0.5)`.
- The colour is c with its alpha doubled.

No alpha test.
