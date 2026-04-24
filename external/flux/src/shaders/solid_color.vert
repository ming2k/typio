#version 450

layout(location = 0) in vec2 in_pos;

layout(push_constant) uniform fx_solid_color_pc {
    vec2 surface_size;
    vec2 pad;
    vec4 color;
} pc;

void main(void)
{
    vec2 ndc = vec2(
        in_pos.x / pc.surface_size.x * 2.0 - 1.0,
        in_pos.y / pc.surface_size.y * 2.0 - 1.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
}
