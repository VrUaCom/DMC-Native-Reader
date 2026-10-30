# Particle texture, alpha raised

Shader: `particle_alpha_boost.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for particles whose texture alpha is raised a lot.

Inputs: `v_uv`.
Uniforms: `uniform sampler2D u_texture`.

- `t = texture(u_texture, v_uv)`.
- rgb = t.rgb; alpha = clamp(t.a * 64.0).

No alpha test.
