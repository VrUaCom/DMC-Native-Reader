# Colour only

Shader: `vertex_colour.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for untextured geometry.

Inputs: `v_colour`.

- rgb = v_colour.rgb; alpha = v_colour.a * 2.0.

Alpha test: yes (CONVENTIONS.md), on the final alpha.
