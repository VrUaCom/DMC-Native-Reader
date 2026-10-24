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
- Not wired into the renderer yet; GlesViewBackend still uses its own
  shaders.
