#ifndef VPT_RAIN_WETNESS_GLSL
#define VPT_RAIN_WETNESS_GLSL

#include "hooks/rain_surface.glsl"

// Wet ground in rain. The Java side keeps
// a texture of the top rain-blocking block of every column around the camera (RainExposure): an upward face that is
// the top of its column, in a biome where it rains rather than snows, gets darker and glossier with the rain. Faces
// next to a wall or overhang dry off towards it, so wet patches end softly instead of at block edges.

ivec4 vptRainColumn(ivec2 column, uint textureId) {
    return ivec4(round(texelFetch(textures[nonuniformEXT(textureId)], column & ivec2(511), 0) * 255.0));
}

bool vptRainColumnBlocks(ivec4 data, int surfaceBlockY) {
    return data.a == 0 || (data.r + 256 * data.g - 32768) > surfaceBlockY;
}

/** How open to the rain an upward face is here, 0..1, whatever the weather: 1 on the top of its column, fading
 *  towards a wall or overhang and at the edge of the texture. */
float vptGroundOpen(vec3 position, vec3 outwardNormal) {
    float rain = 1.0;
    uint textureId = uint(skyUBO.rainWetness.y + 0.5);
    if (textureId == 0u || outwardNormal.y < 0.5) { return 0.0; }
    // The texture covers 256 blocks each way.
    if (max(abs(position.x), abs(position.z)) > 238.0) { return 0.0; }
    ivec3 cameraCell = ivec3(floor(worldUBO.cameraPos.xyz));
    vec3 relativePosition = position + vec3(worldUBO.cameraPos.xyz - dvec3(cameraCell));
    ivec2 column = cameraCell.xz + ivec2(floor(relativePosition.xz));
    int surfaceBlockY = cameraCell.y + int(floor(relativePosition.y - 0.001));
    ivec4 center = vptRainColumn(column, textureId);
    if (center.b == 0 || vptRainColumnBlocks(center, surfaceBlockY)) { return 0.0; }

    vec2 withinColumn = fract(relativePosition.xz);
    ivec2 nearestSide = ivec2(step(vec2(0.5), withinColumn)) * 2 - 1;
    vec2 edge = min(withinColumn, vec2(1.0) - withinColumn);
    float edgeDistanceSquared = 0.25;
    if (vptRainColumnBlocks(vptRainColumn(column + ivec2(nearestSide.x, 0), textureId), surfaceBlockY))
        edgeDistanceSquared = min(edgeDistanceSquared, edge.x * edge.x);
    if (vptRainColumnBlocks(vptRainColumn(column + ivec2(0, nearestSide.y), textureId), surfaceBlockY))
        edgeDistanceSquared = min(edgeDistanceSquared, edge.y * edge.y);
    if (vptRainColumnBlocks(vptRainColumn(column + nearestSide, textureId), surfaceBlockY))
        edgeDistanceSquared = min(edgeDistanceSquared, dot(edge, edge));
    rain *= 1.0 - smoothstep(220.0, 238.0, max(abs(position.x), abs(position.z)));
    return rain * smoothstep(0.0, 0.5, sqrt(edgeDistanceSquared)) * smoothstep(0.5, 0.95, outwardNormal.y);
}

float vptGroundWetness(vec3 position, vec3 outwardNormal) {
    float rain = skyUBO.rainWetness.x;
    if (rain <= 0.0001) { return 0.0; }
    return rain * vptGroundOpen(position, outwardNormal);
}

// How rough wet open ground gets.
const float WET_GROUND_ROUGHNESS = 0.32;
// How rough it is while the rain is still falling on it, and how much soaked ground darkens (rough, non-metal).
const float WET_GROUND_RAINING_ROUGHNESS = 0.14;
const float WET_GROUND_DARKENING = 0.5;

/** Water fills the pores: rough, non-metal surfaces darken the most, and everything turns glossy. */
void vptApplyGroundWetness(inout LabPBRMat material, float wetness) {
    if (wetness <= 0.0 || material.transmission > 0.0) { return; }
    float absorbency = (1.0 - material.metallic) * clamp(material.roughness * 2.0, 0.0, 1.0);
    material.albedo *= 1.0 - WET_GROUND_DARKENING * wetness * absorbency;
    // Glossy, not a mirror: the standing water of a puddle (vptApplyPuddle) is what mirrors the world. At 0.08 all the
    // open ground mirrored the sky as one sheet of water, and the puddles had nothing to stand out from.
    // Water still running on it while it rains: glossier then, drying back to merely glossy once it stops.
    float wetRoughness = mix(WET_GROUND_ROUGHNESS, WET_GROUND_RAINING_ROUGHNESS, skyUBO.rainGradient);
    material.roughness = mix(material.roughness, min(material.roughness, wetRoughness), wetness);
    material.f0 = mix(material.f0, max(material.f0, vec3(0.02)), wetness);
}

// ---- Puddles and rain ripples

// How much of the open, flat ground puddles cover in steady rain, 0..1 (the Puddles setting).
#ifndef VPT_RAIN_PUDDLES
#    define VPT_RAIN_PUDDLES 0.5
#endif
// How large a puddle patch is, in blocks (the period of the coarser noise), and how soft its rim is.
const float PUDDLE_SCALE = 10.0;
const float PUDDLE_RIM = 0.03;
// How far the noise threshold drops at full wetness and Puddles 100 %.
const float PUDDLE_FILL = 1.1;
// How long puddles stand after the rain eases off, in seconds: full for the first, gone by the second. The ground
// itself dries sooner (its wetness falls with a time constant of 25 s).
const float PUDDLE_LINGER_SECONDS = 45.0;
const float PUDDLE_DRY_SECONDS = 180.0;
// Ripples: rings per block across, how long one ring lives in seconds, and how steep its wave is.
const float RIPPLE_CELLS = 2.5;
const float RIPPLE_LIFETIME = 0.9;
const float RIPPLE_SLOPE = 0.35;
// How much of the rings a puddle shows, against open water.
const float PUDDLE_RIPPLES = 0.5;

// A point's x and z in the world, kept small enough for a float: the camera's position wrapped to 4096 blocks.
vec2 vptRainWorldXZ(vec3 position) {
    return vec2(mod(worldUBO.cameraPos.xz, dvec2(4096.0))) + position.xz;
}

float vptRainHash(vec2 cell) {
    uvec2 q = uvec2(ivec2(cell) + ivec2(1 << 20));
    uint h = q.x * 1597334677u ^ q.y * 3812015801u;
    h ^= h >> 15u;
    h *= 2246822519u;
    h ^= h >> 13u;
    return float(h & 0xFFFFFFu) / 16777215.0;
}

float vptRainValueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(vptRainHash(i), vptRainHash(i + vec2(1.0, 0.0)), f.x),
               mix(vptRainHash(i + vec2(0.0, 1.0)), vptRainHash(i + vec2(1.0, 1.0)), f.x), f.y);
}

/** How much of a puddle stands here, 0..1, on an upward face under the open sky. Patches fixed to the
 *  world, from a noise; they spread as the ground soaks and shrink back as it dries after the rain, well after the
 *  ground itself. open: vptGroundOpen. */
float vptPuddle(vec3 position, float open) {
    float amount = clamp(VPT_RAIN_PUDDLES, 0.0, 1.0);
    // While it rains, as wet as the ground; once it has stopped, standing on for a minute or two (rainStoppedFor is
    // 0 while it rains and huge before any rain).
    float linger = 1.0 - smoothstep(PUDDLE_LINGER_SECONDS, PUDDLE_DRY_SECONDS, skyUBO.rainStoppedFor);
    linger *= 1.0 - smoothstep(0.1, 0.2, skyUBO.rainGradient);
    float wetness = open * max(skyUBO.rainWetness.x, linger);
    if (amount <= 0.0 || wetness <= 0.3) { return 0.0; }
    vec2 p = vptRainWorldXZ(position);
    float n = 0.75 * vptRainValueNoise(p / PUDDLE_SCALE) + 0.25 * vptRainValueNoise(p / (PUDDLE_SCALE * 0.33) + 17.0);
    // At the default (Puddles 50 %) and full rain, about three fifths of the open ground; less as it dries.
    float fill = smoothstep(0.3, 1.0, wetness) * amount;
    float threshold = 1.0 - PUDDLE_FILL * fill;
    return smoothstep(threshold, threshold + PUDDLE_RIM, n);
}

/** Standing water: as dark as soaked ground and smooth as water's surface, so it mirrors the sky and the lamps. */
void vptApplyPuddle(inout LabPBRMat material, float puddle) {
    if (puddle <= 0.0 || material.transmission > 0.0) { return; }
    material.albedo *= 1.0 - 0.45 * puddle * (1.0 - material.metallic);
    material.roughness = mix(material.roughness, 0.01, puddle);
    material.f0 = mix(material.f0, max(material.f0, vec3(0.02)), puddle);
}

/** The slope rain ripples give a flat water surface here (x, z), while it rains: rings that spread from where drops
 *  land and fade, from cells fixed to the world in two offset layers, one ring per cell at a time. */
vec2 vptRainRippleSlope(vec3 position) {
    float rain = skyUBO.rainGradient;
    if (rain <= 0.01) { return vec2(0.0); }
    vec2 p = vptRainWorldXZ(position) * RIPPLE_CELLS;
    float time = worldUBO.windTime / RIPPLE_LIFETIME;
    vec2 slope = vec2(0.0);
    for (int layer = 0; layer < 2; layer++) {
        vec2 q = p + (layer == 0 ? vec2(0.0) : vec2(0.5, 0.37));
        vec2 cell = floor(q) + vec2(float(layer) * 4096.0, 0.0);
        float h = vptRainHash(cell);
        float phase = fract(time + h);
        // Not every cell has a drop in it every cycle: fewer as the rain thins.
        if (fract(h * 13.7 + floor(time + h) * 0.618) > rain) { continue; }
        vec2 centre = floor(q) + 0.3 + 0.4 * vec2(fract(h * 7.3), fract(h * 3.1));
        vec2 offset = q - centre;
        float d = length(offset);
        float x = (d - phase * 0.45) * 22.0;
        float fade = (1.0 - phase) * (1.0 - phase);
        // d/dd of sin(x) exp(-x^2): a ring of one crest and one trough.
        float wave = (cos(x) - 2.0 * x * sin(x)) * exp(-x * x) * fade;
        slope += offset / max(d, 1e-4) * wave;
    }
    return slope * RIPPLE_SLOPE * rain;
}

/** Where the puddle and the rings are looked up for a hit at worldPos: a pack's point (hooks/rain_surface.glsl). */
vec3 vptRainSurfacePosition(vec3 worldPos) {
#ifndef RADIANTE_HOOK_RAIN_SURFACE_DEFAULT
    return hookRainPosition(worldPos);
#else
    return worldPos;
#endif
}

/** The puddle's cover, through a pack's rim (hooks/rain_surface.glsl). */
float vptRainPuddleEdge(float puddle) {
#ifndef RADIANTE_HOOK_RAIN_SURFACE_DEFAULT
    return clamp(hookPuddleEdge(puddle), 0.0, 1.0);
#else
    return puddle;
#endif
}

/** The normal turned by the ripples (slope from vptRainRippleSlope), on an upward face. */
vec3 vptApplyRainRipples(vec3 normal, vec2 slope, float strength) {
    if (strength <= 0.0 || dot(slope, slope) <= 0.0) { return normal; }
    return normalize(normal + vec3(-slope.x, 0.0, -slope.y) * strength);
}

#endif
