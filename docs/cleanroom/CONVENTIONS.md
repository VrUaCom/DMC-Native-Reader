# Clean-room shaders: conventions

These shaders are written from behaviour specifications only. Every shader
has one specification in `specs/`; the implementer reads this file and that
specification, nothing else.

## Language

- OpenGL ES Shading Language 3.00: the first line is `#version 300 es`.
- `precision highp float;` and `precision highp int;` in every shader
  (fragment shaders also `precision highp sampler2D;`).
- No `#include`: a helper a specification asks for is written into the
  shader itself.
- Plain uniforms (`uniform vec4 u_name;`), no uniform blocks.
- Vertex inputs use explicit locations, `layout(location = N) in ...`, with
  the numbers below. Fragment shaders write
  `layout(location = 0) out vec4 o_colour;` unless the specification says
  otherwise.
- A fragment shader declares only the inputs it reads.

## Names

- Vertex inputs `a_*`, values passed from the vertex to the fragment
  shader `v_*`, uniforms and samplers `u_*`.
- Vertex input locations:

| location | name | type | meaning |
| --- | --- | --- | --- |
| 0 | `a_position` | `vec3` (or `vec4` where a specification says) | position |
| 1 | `a_normal` | `vec3` | normal |
| 2 | `a_uv` | `vec2` (or `vec4`) | texture coordinates, in the units the specification gives |
| 3 | `a_colour` | `vec4` | vertex colour, in the units the specification gives |
| 4 | `a_bone_rows` | `ivec3` (an integer input) | first rows of up to three bones |
| 5 | `a_weights_packed` | `float` | three bone weights packed in one value |
| 6 | `a_fog` | `float` | per-vertex fog amount |

- Values passed between shaders (a vertex shader writes those its
  specification lists, a fragment shader reads those it needs):

| name | type | meaning |
| --- | --- | --- |
| `v_uv` | `vec2` | texture coordinates (0..1) |
| `v_uv2` | `vec2` | a second set of texture coordinates |
| `v_colour` | `vec4` | colour |
| `v_fog` | `vec4` | rgb: fog colour; a: fog amount, 1 = no fog, 0 = all fog |
| `v_param1`, `v_param2` | `vec4` | per-pass parameters |
| `v_dof_limits` | `vec4` | depth-of-field limits |
| `v_screen_uv` | `vec2` | the pixel's place in a full-screen texture (0..1) |

## Space and colour

- Matrices are GLSL `mat4`, column vectors: `M * v`.
- Clip space is OpenGL's as the engine builds it: the engine's projection
  already puts image row 0 at window row 0. A shader never negates y.
- "Screen mapping", where a specification asks for it: after the clip
  position `c` is found, `c = c * u_screen_scale + u_screen_offset * c.w`
  (two `vec4` uniforms; the engine passes scale 1 and offset 0 when it has
  nothing to add).
- "Clamp" means `clamp(x, 0.0, 1.0)` per component.
- Colours are 0..1 unless a specification gives other units. In the "×2"
  rules a vertex colour of 0.5 leaves a texel unchanged.
- Texture lookups use `texture()`. A table texture (a palette, a colour
  table) is a 2D texture one row high: look it up at `vec2(x, 0.5)`.

## Fog in fragment shaders

`rgb = mix(v_fog.rgb, rgb, v_fog.a)`: an amount of 1 keeps the colour, 0
gives the fog colour.

## Alpha test

Fragment shaders with an alpha test have three uniforms:

- `int u_alpha_test`: 0 off, anything else on.
- `int u_alpha_func`: 1 never passes; 2 passes when alpha < reference;
  3 when alpha == reference; 4 when alpha <= reference; 5 when alpha >
  reference; 6 when alpha != reference; 7 when alpha >= reference; any other
  value passes.
- `float u_alpha_ref`: the reference.

When the test is on and fails, the fragment is discarded. The test runs on
the final alpha the shader computed, after everything else.

## Delivering a shader

- Write it to the path the task gives.
- Check it compiles:
  `EGL_PLATFORM=surfaceless <checker> vert|frag <file>` (the task gives the
  checker's path). Fix it until the checker prints OK.
- Keep comments short and in English; describe what the code does.
