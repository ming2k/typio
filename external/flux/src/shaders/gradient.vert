#version 450

layout(location = 0) in vec2 in_pos;
layout(location = 0) out vec2 frag_pos;

layout(push_constant) uniform fx_gradient_pc {
    vec2 surface_size;
    uint mode;
    uint stop_count;
    vec2 start;
    vec2 end;
    vec4 colors[4];
    float stops[4];
} pc;

void main(void)
{
    vec2 ndc = vec2(
        in_pos.x / pc.surface_size.x * 2.0 - 1.0,
        in_pos.y / pc.surface_size.y * 2.0 - 1.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    frag_pos = in_pos;
}
