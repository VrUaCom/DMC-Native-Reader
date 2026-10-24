# Skinned model, lit

Shader: `model_lit.vert` (GLSL ES 3.00, CONVENTIONS.md).

A vertex shader for a character model: skinned by up to three bones, lit by the scene lights.

Inputs: `a_position` (vec3), `a_normal` (vec3), `a_uv` (vec2),
`a_bone_rows`, `a_weights_packed`.
Outputs: `gl_Position`, `v_uv`, `v_colour`, `v_fog`.

### Skinning

Uniform `vec4 u_bone_rows[176]`: bone matrices stored as four consecutive
`vec4` rows each (44 bones). `a_bone_rows` holds, for up to three bones, the
index of each bone's first row in that array (x: first bone, y: second,
z: third).

A bone's rows r0, r1, r2, r3 transform a point p as
`p.x * r0 + p.y * r1 + p.z * r2 + r3` (a `vec4`), and a normal n as
`n.x * r0 + n.y * r1 + n.z * r2`.

`a_weights_packed` is a non-negative whole number holding three 5-bit
fields: bits 0-4 the first bone's weight, bits 5-9 the second's, bits 10-14
the third's; a field f stands for the weight f / 31. Decode it with float
arithmetic (the value is exact in a float).

- When both the second and the third fields are 0, the vertex follows the
  first bone alone, with its rows as they are (its own field is ignored).
- Otherwise each of the four rows used is the weighted sum of the three
  bones' corresponding rows: `row_k = w1 * first_k + w2 * second_k +
  w3 * third_k`.

Apply the resulting rows to `a_position` (as a point) and `a_normal` (as a
direction). Call the results the skinned position (a `vec4`) and the
skinned normal (its xyz is used; it is not normalised).

### Position

`uniform mat4 u_view_projection`. Clip position = `u_view_projection *
skinned position`, then the screen mapping (CONVENTIONS.md), and that is
`gl_Position`.

### Light intensities

`uniform mat4 u_light_directions`, `uniform mat4 u_light_colours`. The four
light intensities are `I = u_light_directions * vec4(skinned normal.xyz,
1.0)` (three directional lights and a constant term, as the engine fills
the matrix).

### Light colour

Clamp each intensity below at 0 (`max(I, 0.0)`). Light colour = `(u_light_colours * I) / 128.0` (a vec4; its rgb is used).

### Colour

`uniform vec4 u_part_colour` (rgb: the part's colour),
`uniform float u_part_alpha`.
`v_colour.rgb = light colour.rgb * u_part_colour.rgb`.
`v_colour.a = u_part_alpha`.
Clamp `v_colour`.

### Texture coordinates

`a_uv` is in fixed-point units of 1/4096. `uniform vec2 u_uv_scroll` (in the
same units) and `uniform vec2 u_uv_scale`:
`v_uv = (a_uv + u_uv_scroll) / 4096.0 * u_uv_scale`.

### Fog

`uniform vec3 u_fog_colour`, `uniform vec4 u_part_colour` (its w is the
part's fog amount). `v_fog = clamp(vec4(u_fog_colour, u_part_colour.w))`.
