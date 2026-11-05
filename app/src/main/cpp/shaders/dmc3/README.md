# DMC3 shaders (clean room)

Our own GLSL ES 3.00 shaders that behave like the HD port's: 29 vertex and
38 fragment shaders. Each was written by an implementer who saw only
`docs/cleanroom/CONVENTIONS.md` and its own specification in
`docs/cleanroom/specs/`, never the game's shaders. Every file compiles
with OpenGL ES 3.0 (Mesa).

- Families: skinned models and effect models, the stage with its point
  lights, shadow volumes, particles, flat (2D) geometry, the PS2 texture
  functions (decal, modulate, highlight, highlight 2, recolour, with fog and
  cut-out variants), and the full-screen passes (glow, glare, star glare,
  directional glow, depth of field, colour grading, depth and procedural
  fog, edges, copies, the movie frame).
- Attribute locations, varyings, the alpha test and the fog rule are in
  CONVENTIONS.md.
- Built into the core as text (CMake embeds every file here);
  `dmcresource::dmc3` names them, and the GPU backend compiles all of them
  and draws with them where the core's `dmc3_draw()` picks one. The map of
  our names to the game's, and what the Reader draws with which, is
  docs/SHADERS.md.
