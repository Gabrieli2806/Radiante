#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require

// Star fragment stage: overworld only, hidden by rain and by any cloud in front of the star.

#include "common/shared.hpp"

#ifndef VPT_POST_STAR_CLOUD_TRANSMITTANCE_BINDING
#define VPT_POST_STAR_CLOUD_TRANSMITTANCE_BINDING 31
#endif

layout(set = 1, binding = 0) uniform WorldUniform {
    WorldUBO worldUBO;
};

layout(set = 1, binding = 1) uniform SkyUniform {
    SkyUBO skyUBO;
};

layout(set = 4, binding = VPT_POST_STAR_CLOUD_TRANSMITTANCE_BINDING) uniform sampler2D postStarCloudTransmittance;

layout(location = 0) in vec3 pos;
layout(location = 1) in vec4 colorLayer;
layout(location = 2) in vec2 screenUv;

layout(location = 0) out vec4 fragColor;

void main() {
    if (worldUBO.skyType != 1) { discard; }

    vec4 star = colorLayer;
    star.a *= 1.0 - skyUBO.rainGradient;

    // Any noticeable cloud in front hides the star completely, rather than leaving a dimmed dot inside the cloud.
    float clouds = clamp(texture(postStarCloudTransmittance, clamp(screenUv, vec2(0.0), vec2(1.0))).r, 0.0, 1.0);
    if (clouds < 0.99) { discard; }

    // fogControls.w is the star clarity: it grows with camera height and with thin biome air, letting faint stars
    // through (and the stars are brighter overall than vanilla's, closer to Bedrock's night sky). A negative value
    // means another mod draws the stars, so drawing them here would double them; zero means default clarity.
    float clarity = skyUBO.fogControls.w;
    if (clarity < 0.0) { discard; }
    if (!(clarity > 0.0)) { clarity = 1.0; }

    // Stars only ever add light. Vanilla encodes star brightness as a grey drawn additively; blending that grey over
    // a bright dusk sky would darken it into a black speck. So the brightness becomes coverage (alpha) and the
    // colour is the star's hue at full strength.
    float brightest = max(star.r, max(star.g, star.b));
    vec3 hue = brightest > 1e-4 ? star.rgb / brightest : vec3(1.0);
    float coverage = min(brightest * 1.6 * clarity, 1.0) * min(star.a * 1.8 * clarity, 1.0);
    fragColor = vec4(hue, coverage);
    gl_FragDepth = 0.999999;
}
