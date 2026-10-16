#version 460

// Temporal accumulation: this frame's radiance blended into the history, which is followed along the motion vectors.
// The history is clamped to the colour range of the current 3x3 neighbourhood, which limits ghosting, and dropped
// where it comes from off screen or from a surface that faced another way.

layout(set = 0, binding = 0) uniform sampler2D currentColor;
layout(set = 0, binding = 1) uniform sampler2D historyColor;
layout(set = 0, binding = 2) uniform sampler2D motionVectors; // in pixels, from where a point is now to where it was
layout(set = 0, binding = 3) uniform sampler2D currentNormal;
layout(set = 0, binding = 4) uniform sampler2D historyNormal;

// TemporalAccumulationPushConstant (temporal_accumulation_module.hpp).
layout(push_constant) uniform PushConstants {
    float alpha;     // the current frame's weight where the history is kept
    float threshold; // the least cosine between the current and the history normal that keeps the history
}
pc;

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;

void main() {
    vec2 resolution = vec2(textureSize(currentColor, 0));
    vec2 texel = 1.0 / resolution;

    vec3 color = texture(currentColor, texCoord).rgb;
    vec3 normal = texture(currentNormal, texCoord).xyz;

    vec3 lowest = vec3(10000.0);
    vec3 highest = vec3(-10000.0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec3 neighbour = texture(currentColor, texCoord + vec2(x, y) * texel).rgb;
            lowest = min(lowest, neighbour);
            highest = max(highest, neighbour);
        }
    }

    vec2 historyUv = texCoord + texture(motionVectors, texCoord).xy / resolution;
    bool offScreen = any(lessThan(historyUv, vec2(0.0))) || any(greaterThan(historyUv, vec2(1.0)));

    // Where the history is not kept the result is this frame alone: its own values blended at full weight.
    vec3 pastColor = color;
    vec3 pastNormal = normal;
    bool keepHistory = false;
    if (!offScreen) {
        vec3 storedNormal = texture(historyNormal, historyUv).xyz;
        if (!(dot(normal, storedNormal) < pc.threshold)) {
            keepHistory = true;
            pastNormal = storedNormal;
            pastColor = clamp(texture(historyColor, historyUv).rgb, lowest, highest);
        }
    }

    float weight = keepHistory ? pc.alpha : 1.0;
    outColor = vec4(mix(pastColor, color, weight), 1.0);
    outNormal = vec4(normalize(mix(pastNormal, normal, weight)), 1.0);
}
