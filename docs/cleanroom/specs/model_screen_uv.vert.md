# Skinned model, screen-space texture (haze / camouflage)

Shader: `model_screen_uv.vert` (GLSL ES 3.00, CONVENTIONS.md).

A vertex shader for a skinned character model drawn with the screen behind it as its texture. The texture coordinates are not the model's:
    they are where the vertex falls on the screen, shifted by how its normal
    faces a direction, so the texture (a copy of the screen) appears bent
    through the model, like a heat haze or active camouflage. No light.

    Inputs: `a_position`, `a_normal`, `a_uv` (unused), `a_bone_rows`,
    `a_weights_packed`. Outputs: `gl_Position`, `v_uv`, `v_colour`, `v_fog`.

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

    ### Screen texture coordinates

    `uniform vec3 u_refract_dir`, `uniform vec3 u_refract_shift`. Take the
    clip position before the screen mapping, c. Its place on the screen is
    `f = (c.xy / c.w) * 0.5 + 0.5`. Let `k = dot(skinned normal.xyz,
    u_refract_dir)`. Then
    `v_uv.x = clamp(f.x + u_refract_shift.x * k)` and
    `v_uv.y = clamp(f.y + u_refract_shift.y * k - u_refract_shift.z)`.

    ### Colour

    `uniform vec4 u_part_colour`, `uniform float u_part_alpha`.
    `v_colour = clamp(vec4(u_part_colour.rgb, u_part_alpha))`.

### Fog

`uniform vec3 u_fog_colour`, `uniform vec4 u_part_colour` (its w is the
part's fog amount). `v_fog = clamp(vec4(u_fog_colour, u_part_colour.w))`.
