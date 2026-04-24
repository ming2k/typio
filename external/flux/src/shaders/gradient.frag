#version 450

layout(location = 0) in vec2 frag_pos;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform fx_gradient_pc {
    vec2 surface_size;
    uint mode;
    uint stop_count;
    vec2 start;
    vec2 end;
    vec4 colors[4];
    float stops[4];
} pc;

vec4 sample_gradient(float t)
{
    if (t <= pc.stops[0]) return pc.colors[0];
    if (t >= pc.stops[pc.stop_count - 1]) return pc.colors[pc.stop_count - 1];

    for (uint i = 1; i < pc.stop_count; i++) {
        if (t < pc.stops[i]) {
            float s = (t - pc.stops[i - 1]) / (pc.stops[i] - pc.stops[i - 1]);
            return mix(pc.colors[i - 1], pc.colors[i], s);
        }
    }
    return pc.colors[pc.stop_count - 1];
}

void main(void)
{
    float t;
    if (pc.mode == 0u) {
        vec2 dir = pc.end - pc.start;
        float len2 = dot(dir, dir);
        t = dot(frag_pos - pc.start, dir) / len2;
    } else {
        float d = length(frag_pos - pc.start);
        t = d / pc.end.x;
    }
    out_color = sample_gradient(clamp(t, 0.0, 1.0));
}
