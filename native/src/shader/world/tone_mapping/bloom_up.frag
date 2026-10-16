#version 460

// Bloom, the upscaling half of the pyramid: this level as it was downscaled, plus the coarser level spread back out
// over it. Four of these follow the downscales, as in Bedrock's bloom; the 3x3 tent and the weights are stand-ins.

layout(set = 0, binding = 0) uniform sampler2D coarser; // the level below, already combined
layout(set = 0, binding = 1) uniform sampler2D level;   // this level, as downscaled

// ToneMappingBloomPushConstant (tone_mapping_module.hpp).
layout(push_constant) uniform PushConstant {
    vec2 srcTexel; // the size of one texel of the coarser level in uv
    int firstPass;
    int autoExposure;
    float manualExposure;
    float exposureBias;
    float threshold;
    float scatter; // how much of the coarser level is spread back
    float weight;  // how much of this level is kept
    float padding0;
}
pc;

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

vec3 tentUp(vec2 uv) {
    vec3 sum = vec3(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float w = (2.0 - abs(float(x))) * (2.0 - abs(float(y)));
            sum += w * texture(coarser, uv + vec2(x, y) * pc.srcTexel).rgb;
        }
    }
    return sum * (1.0 / 16.0);
}

void main() {
    vec3 own = texture(level, texCoord).rgb;
    fragColor = vec4(pc.weight * own + pc.scatter * tentUp(texCoord), 1.0);
}
