# Movie frame

Shader: `movie_frame.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for a movie frame whose texture holds blue in the red channel and red in the blue one.

Inputs: `v_uv`.
Uniforms: `uniform sampler2D u_frame`.

- `f = texture(u_frame, v_uv)`; the colour is `f.bgra`.

No alpha test.
