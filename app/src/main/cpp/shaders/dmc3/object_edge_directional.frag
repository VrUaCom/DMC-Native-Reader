#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Object edges toward three neighbours, one per output channel.

in vec2 v_uv;
in vec4 v_param1; // xy: step to the neighbours

uniform sampler2D u_texture;
uniform sampler2D u_object_ids; // table texture, one row high, indexed by alpha

layout(location = 0) out vec4 o_colour;

// Look up the object-id table at x.
vec4 objectIds(float x)
{
    return texture(u_object_ids, vec2(x, 0.5));
}

void main()
{
    vec2 o = v_param1.xy;

    // Centre and the three neighbours.
    vec4 p  = texture(u_texture, v_uv);
    vec4 pr = texture(u_texture, v_uv - vec2(o.x, 0.0));
    vec4 pb = texture(u_texture, v_uv - o);
    vec4 pg = texture(u_texture, v_uv - vec2(0.0, o.y));

    // Object ids: centre from red, each neighbour from its own channel.
    float i0 = objectIds(p.a).r;
    float ir = objectIds(pr.a).r;
    float ig = objectIds(pg.a).g;
    float ib = objectIds(pb.a).b;

    // Difference of each neighbour against the centre's red.
    float r = fract(ir - i0);
    float g = fract(ig - i0);
    float b = fract(ib - i0);

    float a = clamp((r + g + b) * 255.0, 0.0, 1.0);

    o_colour = vec4(r, g, b, a);
}
