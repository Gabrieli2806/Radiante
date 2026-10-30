#ifndef AETHER_CLOUD_SHAPE_GLSL
#define AETHER_CLOUD_SHAPE_GLSL

// Aether: where the clouds are, shared by the cloud layer the sky shows (hooks/clouds.glsl) and the shadow it casts
// (hooks/cloud_shadow.glsl), so a cloud passing over the sun darkens the ground under it.
//
// Two layers over the camera, drawn on a gently curved shell so the horizon stays a finite distance away (a flat
// plane runs out to infinity there and its noise aliases into shimmer): soft cumulus 700 blocks up, from three
// octaves of the noise image (masses, billows that slowly change shape, ragged edges), and streaks of cirrus 2600
// blocks up, drifting a little faster and on a slightly different heading for parallax. Rain and thunder thicken the
// cumulus. The camera's position and the wind are folded into the noise's period in double precision before they
// become floats, so the clouds stay smooth far from spawn and after hours of play.
#include "aether/aether_noise.glsl"

const float AETHER_CUMULUS_HEIGHT = 700.0;
const float AETHER_CIRRUS_HEIGHT = 2600.0;
// Every scale below divides this, so the offsets can wrap on it without a seam.
const double AETHER_CLOUD_PERIOD = 8192.0;

struct AetherCloudOffsets {
    vec2 cumulus;
    vec2 billow;
    vec2 cirrus;
};

AetherCloudOffsets aetherCloudOffsets() {
    dvec2 camera = worldUBO.cameraPos.xz;
    double seconds = double(aetherSeconds()) * double(clamp(float(AETHER_CLOUD_SPEED), 0.0, 4.0));
    AetherCloudOffsets o;
    // About 7.6 blocks a second at speed 1: a slow drift at 700 blocks up.
    o.cumulus = vec2(mod(camera + dvec2(7.0, 3.0) * seconds, AETHER_CLOUD_PERIOD));
    // The billows drift a little against the masses, so the clouds change shape as they go.
    o.billow = vec2(mod(camera + dvec2(8.5, 2.0) * seconds, AETHER_CLOUD_PERIOD));
    o.cirrus = vec2(mod(camera + dvec2(13.0, 7.0) * seconds, AETHER_CLOUD_PERIOD));
    return o;
}

// Where a direction from the camera meets a layer `height` above it, camera-relative: straight up at `height`, at
// the horizon about eight times as far.
vec2 aetherLayerPoint(vec3 dir, float height) {
    return dir.xz * (height / (max(dir.y, 0.0) + 0.12 * length(dir.xz) + 1e-3));
}

// How much more cloud the weather brings: rain covers the sky, thunder darkens it further.
float aetherCloudCover() {
    return clamp(float(AETHER_CLOUD_COVER) + radianteRain() * 0.35 + radianteThunder() * 0.15, 0.0, 1.0);
}

// The cumulus' density, 0 to 1, at a camera-relative point of its layer. detail adds the ragged small scale, for
// the camera's view and the shadows; rays that gather light leave it out.
float aetherCumulusDensity(vec2 p, AetherCloudOffsets o, bool detail) {
    float masses = aetherNoiseAt((p + o.cumulus) / 8192.0).r;
    float billows = aetherNoiseAt((p + o.billow) / 2048.0 + vec2(0.37, 0.11)).g;
    float n = masses * 0.66 + billows * 0.34;
    if (detail) { n += (aetherNoiseAt((p + o.billow) / 512.0 + vec2(0.71, 0.29)).b - 0.5) * 0.12; }
    float threshold = mix(0.80, 0.30, aetherCloudCover());
    return smoothstep(threshold, threshold + 0.17, n);
}

// The cirrus' density, 0 to 1: streaks of the noise's fourth channel laid along the wind, where the large masses
// allow them.
float aetherCirrusDensity(vec2 p, AetherCloudOffsets o) {
    vec2 q = p + o.cirrus;
    float streaks = aetherNoiseAt(vec2(q.y, q.x) / 4096.0).a;
    float where = aetherNoiseAt(q / 8192.0 + vec2(0.5, 0.25)).r;
    return smoothstep(0.55, 0.85, streaks) * smoothstep(0.35, 0.65, where);
}

// How much of the sky behind the cumulus shows through at a density, before the horizon fade.
float aetherCumulusAlpha(float density) {
    return 1.0 - exp(-density * 3.2);
}

// The clouds fade into the horizon's haze.
float aetherCumulusHorizon(vec3 dir) {
    return smoothstep(0.0, 0.10, dir.y);
}

// The cumulus' shadow on a camera-relative point, towards the sun or moon: the light ray is followed up to the
// layer's height, and the cloud found where the camera sees that point of the sky, so the shadow is the cloud the
// sky shows. Thin cloud lets some light through. 1 where nothing is in the way.
float aetherCloudShadow(vec3 worldPos, vec3 lightDir) {
    if (lightDir.y <= 0.03) { return 1.0; }
    float toLayer = (AETHER_CUMULUS_HEIGHT - worldPos.y) / lightDir.y;
    if (toLayer <= 0.0) { return 1.0; }
    vec3 seen = normalize(worldPos + lightDir * toLayer);
    float density = aetherCumulusDensity(aetherLayerPoint(seen, AETHER_CUMULUS_HEIGHT), aetherCloudOffsets(), true);
    if (density <= 0.002) { return 1.0; }
    return 1.0 - 0.92 * aetherCumulusAlpha(density) * aetherCumulusHorizon(seen);
}

#endif
