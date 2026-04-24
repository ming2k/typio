#version 450

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(binding = 0) uniform sampler2D tex;

layout(push_constant) uniform fx_image_pc {
    vec2 surface_size;
    vec2 pad;
} pc;

void main(void)
{
    out_color = texture(tex, in_uv);
}
