#version 460

// Bloom, the downscaling half of the pyramid. The pass layout follows Bedrock's bloom; every filter shape and weight
// here is a stand-in.
//
//   firstPass != 0: the HDR image to half size. The source is exposed as the tone mapping pass exposes it, so what
//                   blooms is what is seen, and the threshold keeps only the bright part.
//   firstPass == 0: one pyramid level to the next (a 3x3 tent).

layout(set = 0, binding = 0) uniform sampler2D source;

layout(set = 0, binding = 2) readonly buffer ExposureBuffer {
    float exposure;
    float avgLogLum;
    float padding0;
    float padding1;
}
expData;

// ToneMappingBloomPushConstant (tone_mapping_module.hpp).
layout(push_constant) uniform PushConstant {
    vec2 srcTexel; // the size of one source texel in uv
    int firstPass;
    int autoExposure;
    float manualExposure;
    float exposureBias;
    float threshold;
    float scatter;
    float weight;
    float padding0;
}
pc;

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

// The most a single texel may carry into the pyramid: a few texels of the sun's disc would light the whole frame.
const float BLOOM_TEXEL_LIMIT = 64.0;

// The exposure the tone mapping pass applies.
float currentExposure() {
    float exposure = pc.autoExposure != 0 ? expData.exposure : pc.manualExposure;
    if (isnan(exposure) || isinf(exposure) || exposure <= 0.0) { exposure = max(pc.manualExposure, 1e-6); }
    return exposure * exp2(pc.exposureBias);
}

vec3 exposedHalf(vec2 uv) {
    float exposure = currentExposure();
    vec3 sum = vec3(0.0);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            vec3 c = texture(source, uv + (vec2(x, y) - 0.5) * pc.srcTexel).rgb;
            // A value that is not a number would spread through every level as a black blotch.
            if (any(isnan(c))) { c = vec3(0.0); }
            c = clamp(c * exposure, vec3(0.0), vec3(BLOOM_TEXEL_LIMIT));
            float peak = max(c.r, max(c.g, c.b));
            c *= peak > 1e-5 ? max(peak - pc.threshold, 0.0) / peak : 0.0;
            sum += c;
        }
    }
    return sum * 0.25;
}

vec3 tentDown(vec2 uv) {
    vec3 sum = vec3(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float w = (2.0 - abs(float(x))) * (2.0 - abs(float(y)));
            sum += w * texture(source, uv + vec2(x, y) * pc.srcTexel).rgb;
        }
    }
    return sum * (1.0 / 16.0);
}

void main() {
    fragColor = vec4(pc.firstPass != 0 ? exposedHalf(texCoord) : tentDown(texCoord), 1.0);
}
