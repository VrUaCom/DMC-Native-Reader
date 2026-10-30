# Stage, four point lights, no fog

Shader: `stage_lit.vert` (GLSL ES 3.00, CONVENTIONS.md).

A vertex shader for the stage (the level geometry): lit by up to four point lights and an ambient colour, tinted by its vertex colours.

Inputs: `a_position` (vec3), `a_normal` (vec3), `a_uv` (vec2), `a_colour`
(vec4, in 0..255 units: a stored 255 arrives as 255.0).
Outputs: `gl_Position`, `v_uv`, `v_colour`, `v_fog`.

### Position

`uniform mat4 u_world_view_projection`. Clip position =
`u_world_view_projection * vec4(a_position, 1.0)`, then the screen mapping
(CONVENTIONS.md).

### Texture coordinates

`a_uv` is in fixed-point units of 1/4096. `uniform vec2 u_uv_scroll` (same
units), `uniform vec2 u_uv_scale`:
`v_uv = (a_uv + u_uv_scroll) / 4096.0 * u_uv_scale`.

### Four point lights

`uniform vec4 u_light_x`, `u_light_y`, `u_light_z`: the x, y and z of four
light positions (one light per component). `uniform vec4 u_light_falloff`
(per light). `uniform vec4 u_light_colour[4]`. `uniform vec3 u_ambient`.

For every light i:
- `d = light position - a_position` (not normalised);
- `atten = clamp(1.0 - dot(d, d) * u_light_falloff[i])`;
- `facing = dot(d, a_normal)` (neither vector normalised);
- `w = max(facing * atten, 0.0)`.

(Work on the four lights at once with vec4 arithmetic.)
Light = `u_light_colour[0] * w0 + u_light_colour[1] * w1 +
u_light_colour[2] * w2 + u_light_colour[3] * w3`, plus `u_ambient` on rgb.

### Colour

`uniform float u_part_alpha`. `v_colour.rgb = light.rgb * a_colour.rgb /
255.0`, `v_colour.a = u_part_alpha`; clamp `v_colour`.

### Fog

None: `v_fog = vec4(0.0, 0.0, 0.0, 1.0)`.
