# Depth of field

Shader: `depth_of_field.frag` (GLSL ES 3.00, CONVENTIONS.md).

A fragment shader for depth of field: a blurred copy of the picture whose transparency grows away from the focus.

Inputs: `v_uv`, `v_colour`, `v_dof_limits`, `v_screen_uv`.
Uniforms: `uniform sampler2D u_depth_texture`, `uniform sampler2D u_texture`, `uniform vec2 u_blur_offset`.

- `d = texture(u_depth_texture, v_screen_uv).r`.
- `far = clamp(v_dof_limits.y + d * v_dof_limits.x)`, `near = clamp(v_dof_limits.w + d * v_dof_limits.z)`.
- `s = far + near`; alpha = `clamp(s * s * s)`.
- rgb = `(texture at v_uv - u_blur_offset + texture at v_uv).rgb * 0.5`.
- Finally multiply the colour (rgb and alpha) by v_colour.

No alpha test.
