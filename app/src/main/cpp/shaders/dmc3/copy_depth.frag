#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Copies a depth texture into the depth buffer; writes no colour.

in vec2 v_uv;

uniform sampler2D u_depth_texture;

void main() {
    gl_FragDepth = texture(u_depth_texture, v_uv).r;
}
