#version 450

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform fx_text_pc {
    vec2 surface_size;
    vec2 pad;
    vec4 color;
} pc;

layout(binding = 0) uniform sampler2D tex;

void main(void)
{
    float a = texture(tex, in_uv).r;
    out_color = pc.color * a;
}
