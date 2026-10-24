# Procedural fog

Shader: `procedural_fog.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for a moving fog from four octaves of a noise picture through a palette.

Inputs: `v_uv`, `v_param1` (xy: an offset, zw: a scale).
Uniforms: `uniform sampler2D u_texture` (the noise), `uniform sampler2D u_palette` (a table, CONVENTIONS.md).

- Octave factors: the first pair of reads uses 1 and 2, the second pair 3 and 4 times the base offset and scale.
- `o = v_param1.xyxy * vec4(1.0, 1.0, 2.0, 2.0)`, `sc = v_param1.zwzw * vec4(1.0, 1.0, 2.0, 2.0)`, `p = v_uv.xyxy * sc + o`.
- `c = texture at p.xy * 0.5 + texture at p.zw * 0.25`.
- `o *= vec4(3.0, 3.0, 2.0, 2.0)`, `sc *= vec4(3.0, 3.0, 2.0, 2.0)`, `p = v_uv.xyxy * sc + o`.
- `c += texture at p.xy * 0.125 + texture at p.zw * 0.0625`.
- The colour is `u_palette at c.r`, its alpha doubled.

No alpha test.
