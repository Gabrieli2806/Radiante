#version 460
#extension GL_GOOGLE_include_directive : require

// Per-pixel motion blur along the pixel's own motion vector. Samples are taken symmetrically on both sides, packed
// towards the centre, and weighted down when they belong to a different depth layer, move differently, or differ a
// lot in colour, which keeps sharp objects from smearing over a blurred background (and the reverse).

#include "common/shared.hpp"

#ifndef VPT_POST_ENABLE_MOTION_BLUR
#define VPT_POST_ENABLE_MOTION_BLUR 0
#endif

#ifndef VPT_POST_MOTION_BLUR_STRENGTH
#define VPT_POST_MOTION_BLUR_STRENGTH 1.35
#endif

#ifndef VPT_POST_MOTION_BLUR_SAMPLE_COUNT
#define VPT_POST_MOTION_BLUR_SAMPLE_COUNT 10
#endif

// Strength and sample count are live settings: the native side defines both macros as reads of
// worldUBO.packParams, which is why the world uniform is bound here.
layout(set = 1, binding = 0) uniform WorldUniform {
    WorldUBO worldUBO;
};

layout(set = 3, binding = 0) uniform sampler2D postInputLdr;
layout(set = 3, binding = 1) uniform sampler2D postFirstHitDepth;
layout(set = 3, binding = 3) uniform sampler2D postMotionVector;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// Keeps a UV at least half a texel inside the image so bilinear taps never read past the border.
vec2 clampUv(vec2 uv, vec2 texelSize) {
    vec2 margin = texelSize * 0.5;
    return clamp(uv, margin, vec2(1.0) - margin);
}

float depthAt(vec2 uv) {
    // First-hit depth is view-space z (negative in front of the camera); only its magnitude matters here.
    return abs(texture(postFirstHitDepth, uv).r);
}

// Nearest depth over a plus-shaped 5-tap footprint, so thin foreground edges count as foreground.
float sampleConservativeDepth(vec2 uv, vec2 texelSize) {
    vec2 centre = clampUv(uv, texelSize);
    vec2 stepX = vec2(texelSize.x, 0.0);
    vec2 stepY = vec2(0.0, texelSize.y);
    float nearest = depthAt(centre);
    nearest = min(nearest, depthAt(clampUv(centre + stepX, texelSize)));
    nearest = min(nearest, depthAt(clampUv(centre - stepX, texelSize)));
    nearest = min(nearest, depthAt(clampUv(centre + stepY, texelSize)));
    nearest = min(nearest, depthAt(clampUv(centre - stepY, texelSize)));
    return nearest;
}

void main() {
    vec2 resolution = vec2(textureSize(postInputLdr, 0));
    vec2 texelSize = 1.0 / resolution;
    vec3 centreColour = texture(postInputLdr, fragTexCoord).rgb;

#if VPT_POST_ENABLE_MOTION_BLUR == 0
    outColor = vec4(centreColour, 1.0);
    return;
#else
    // Motion is in pixels. The blur reaches a bit further than the motion and never beyond 20 px.
    vec2 motion = texture(postMotionVector, fragTexCoord).xy;
    float motionPixels = length(motion);
    float radius = min(motionPixels * VPT_POST_MOTION_BLUR_STRENGTH * 1.15, 20.0);
    if (radius < 0.25) {
        outColor = vec4(centreColour, 1.0);
        return;
    }

    vec2 reachUv = motion / max(motionPixels, 1e-4) * (radius / resolution);
    float centreDepth = sampleConservativeDepth(fragTexCoord, texelSize);

    // The centre keeps more weight for short blurs so small motions stay crisp.
    float centreWeight = mix(2.3, 1.2, clamp(radius / 20.0, 0.0, 1.0));
    vec3 sum = centreColour * centreWeight;
    float weightSum = centreWeight;

    int sampleCount = VPT_POST_MOTION_BLUR_SAMPLE_COUNT;
    for (int index = 1; index <= sampleCount; ++index) {
        float linearOffset = float(index) / float(sampleCount + 1);
        // Bias the offsets towards the centre (part-way to a quadratic) and fade with distance.
        float offset = mix(linearOffset, linearOffset * linearOffset, 0.35);
        float falloff = exp2(-2.3 * offset);

        // Behind the pixel first, then ahead of it.
        for (int side = -1; side <= 1; side += 2) {
            vec2 uv = clampUv(fragTexCoord + reachUv * offset * float(side), texelSize);
            vec3 colour = texture(postInputLdr, uv).rgb;
            float depth = sampleConservativeDepth(uv, texelSize);
            vec2 otherMotion = texture(postMotionVector, uv).xy;

            float depthGap = abs(depth - centreDepth) / max(1.0, min(centreDepth, depth));
            float sameLayer = 1.0 - smoothstep(0.02, 0.1, depthGap);
            float sameMotion = 1.0 - smoothstep(1.5, radius * 1.8 + 1.5, length(otherMotion - motion));
            float similarColour = 1.0 - smoothstep(0.2, 0.9, length(colour - centreColour));
            float weight = falloff * sameLayer * mix(0.65, 1.0, sameMotion) * mix(0.6, 1.0, similarColour);

            sum += colour * weight;
            weightSum += weight;
        }
    }

    outColor = vec4(sum / max(weightSum, 1e-4), 1.0);
#endif
}
