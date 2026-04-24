#version 450

layout(location = 0) out vec4 out_color;

layout(push_constant) uniform fx_solid_color_pc {
    vec2 surface_size;
    vec2 pad;
    vec4 color;
} pc;

void main(void)
{
    out_color = pc.color;
}
