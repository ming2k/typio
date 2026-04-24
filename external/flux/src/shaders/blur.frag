#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D src;

layout(push_constant) uniform fx_blur_pc {
    vec2 texel_size;
} pc;

void main(void)
{
    vec4 sum = vec4(0.0);
    float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    for (int x = -4; x <= 4; x++) {
        for (int y = -4; y <= 4; y++) {
            float dist = sqrt(float(x*x + y*y));
            int idx = int(round(dist));
            if (idx > 4) idx = 4;
            sum += texture(src, uv + vec2(float(x), float(y)) * pc.texel_size) * w[idx];
        }
    }
    out_color = sum;
}
