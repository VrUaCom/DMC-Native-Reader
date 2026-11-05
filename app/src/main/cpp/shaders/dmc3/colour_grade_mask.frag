#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Colour grading, replace: masked pixels take a colour table entry chosen by their green.

in vec2 v_uv;

uniform sampler2D u_texture;
uniform sampler2D u_object_ids; // table, looked up by the pixel's alpha
uniform sampler2D u_grade;      // table, looked up by the pixel's green

layout(location = 0) out vec4 o_colour;

void main() {
    vec4 p = texture(u_texture, v_uv);
    vec4 id = texture(u_object_ids, vec2(p.a, 0.5));
    if (id.a > 0.0) {
        // Masked: replace with the grade entry, alpha doubled.
        vec4 g = texture(u_grade, vec2(p.g, 0.5));
        o_colour = vec4(g.rgb, g.a * 2.0);
    } else {
        o_colour = vec4(0.0);
    }
}
