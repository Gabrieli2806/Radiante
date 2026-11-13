#ifndef VPT_RAIN_WETNESS_GLSL
#define VPT_RAIN_WETNESS_GLSL

#include "hooks/rain_surface.glsl"

// Wet ground in rain. The Java side keeps
// a texture of the top rain-blocking block of every column around the camera (RainExposure): an upward face that is
// the top of its column, in a biome where it rains rather than snows, gets darker and glossier with the rain, right up
// to a wall or the step of a higher block beside it, as the rain falls straight down there too.

// The texture's id and the rain clock, both carried in rainWetness.y (RainExposure: id * 256 + the clock in steps).
uint vptRainTextureId() { return uint(floor(skyUBO.rainWetness.y / 256.0)); }
float vptRainClock() { return mod(skyUBO.rainWetness.y, 256.0); }

// How far the top of the column last looked up has soaked, 0..1, against the ground around (set by vptGroundRain):
// a top that changed a moment ago (a block broken off it in the rain) is dry and soaks over the Wet-Up Time, and one
// that changed after the rain stays dry. Its alpha holds the rain clock when it changed; 255 is an old top.
float g_vptColumnSoak = 1.0;
const float RAIN_CLOCK_STEPS = 253.0;
const float RAIN_CLOCK_UNIT = 0.025;
float vptColumnSoak(ivec4 data) {
    if (data.a == 255 || data.a == 0) { return 1.0; }
    float since = mod(vptRainClock() - float(data.a - 1) + RAIN_CLOCK_STEPS, RAIN_CLOCK_STEPS);
    return 1.0 - exp(-since * RAIN_CLOCK_UNIT);
}

ivec4 vptRainColumn(ivec2 column, uint textureId) {
    return ivec4(round(texelFetch(textures[nonuniformEXT(textureId)], column & ivec2(511), 0) * 255.0));
}

// How hot the column's top is, 0..3 (RainExposure: fire, lava, a lit furnace or campfire 3, a torch or lantern on it 2).
int vptColumnHeat(ivec4 data) { return data.b > 0 ? (data.b & 3) : 0; }

// How much the heat of a fire, a lit furnace or campfire, lava or a torch beside keeps a face here dry, 0..1: the
// most of the columns round it, falling off over a block and a half. The ground by a campfire stays drier through a
// storm and dries first after it.
const float HEAT_DRYING = 0.85;
float vptHeatDryness(ivec2 column, uint textureId, vec2 withinColumn) {
    float dry = 0.0;
    for (int dz = -1; dz <= 1; dz++) {
        for (int dx = -1; dx <= 1; dx++) {
            int heat = vptColumnHeat(vptRainColumn(column + ivec2(dx, dz), textureId));
            if (heat == 0) { continue; }
            vec2 away = max(abs(withinColumn - (vec2(dx, dz) + 0.5)) - 0.5, vec2(0.0));
            float reach = heat == 3 ? 1.5 : 0.8;
            dry = max(dry, (float(heat) / 3.0) * (1.0 - smoothstep(0.0, reach, length(away))));
        }
    }
    return dry * HEAT_DRYING;
}

bool vptRainColumnBlocks(ivec4 data, int surfaceBlockY) {
    return data.a == 0 || (data.r + 256 * data.g - 32768) > surfaceBlockY;
}

// How damp sheltered ground gets at most (beside a wall, at the rim of a tree's shade), against open ground, and how
// far in from the open the rain blown under a roof or a canopy reaches, in blocks.
const float SHELTER_DAMP = 0.45;
const float SHELTER_REACH = 2.0;
// Ground under leaves: the rain drips through them, so it is never quite dry there.
const float LEAF_DAMP = 0.3;
// How far apart the columns are that are read to blend the rain across a biome's edge, in blocks: the wetness fades
// over about twice that.
const float BIOME_BLEND_STEP = 4.0;

#ifndef VPT_RAIN_WET_WALLS
#    define VPT_RAIN_WET_WALLS 0.8
#endif

// Minecraft's look for the rain's marks (the Pixel Rain setting, on by default): puddles, the rings on them and on
// water, and the wetness of every surface go on in pixels of a sixteenth of a block, the size of a block texture's,
// with hard edges, as a texture painted for the game would show them.
#ifndef VPT_RAIN_PIXELS
#    define VPT_RAIN_PIXELS 1
#endif
const float RAIN_PIXELS_PER_BLOCK = 16.0;

/** How much of the rain falls here, 0..1, from the biomes of the columns around: a wet area fades out over a few
 *  blocks into a desert or a snowy biome instead of stopping at the biome's edge. */
float vptRainBiome(ivec2 column, uint textureId) {
    float rains = 0.0;
    for (int dz = -1; dz <= 1; dz++) {
        for (int dx = -1; dx <= 1; dx++) {
            ivec2 at = column + ivec2(dx, dz) * int(BIOME_BLEND_STEP);
            rains += vptRainColumn(at, textureId).b > 0 ? 1.0 : 0.0;
        }
    }
    return smoothstep(0.0, 1.0, rains / 9.0);
}

/** How the rain reaches a face here, whatever the weather.
 *  x: how open an upward face is to the sky, 0..1, for standing water: 1 on the top of its column, fading towards a
 *  wall or overhang and at the edge of the texture.
 *  y: how damp the face gets, 0..1. Upward faces: as x in the open but never under SHELTER_DAMP there; under cover
 *  the rain blown in from the open columns around, fading over SHELTER_REACH blocks, and under leaves never less
 *  than LEAF_DAMP. Walls and other upright faces: VPT_RAIN_WET_WALLS where the column in front of them is open to
 *  the sky at their height. Undersides stay dry. Both fade over a few blocks at the edge of a biome where it rains
 *  (vptRainBiome). It was cut hard at the edge of the open sky and of the biome before, and only floors got wet. */
vec2 vptGroundRain(vec3 position, vec3 outwardNormal) {
    g_vptColumnSoak = 1.0;
    uint textureId = vptRainTextureId();
    if (textureId == 0u || outwardNormal.y < -0.3) { return vec2(0.0); }
    // The texture covers 256 blocks each way.
    if (max(abs(position.x), abs(position.z)) > 238.0) { return vec2(0.0); }
    ivec3 cameraCell = ivec3(floor(worldUBO.cameraPos.xyz));
    vec3 relativePosition = position + vec3(worldUBO.cameraPos.xyz - dvec3(cameraCell));
    ivec2 column = cameraCell.xz + ivec2(floor(relativePosition.xz));
    float biome = vptRainBiome(column, textureId);
    if (biome <= 0.0) { return vec2(0.0); }
    float fade = (1.0 - smoothstep(220.0, 238.0, max(abs(position.x), abs(position.z)))) * biome;

    if (outwardNormal.y < 0.5) {
        // An upright face: wet where the air in front of it is open to the sky at its height.
        ivec2 front = cameraCell.xz + ivec2(floor(relativePosition.xz + outwardNormal.xz * 0.5));
        int faceBlockY = cameraCell.y + int(floor(relativePosition.y));
        ivec4 frontData = vptRainColumn(front, textureId);
        if (vptRainColumnBlocks(frontData, faceBlockY)) { return vec2(0.0); }
        // A face the column in front of it just opened up (the sides of a hole dug in the rain) soaks as that
        // column's top does.
        g_vptColumnSoak = vptColumnSoak(frontData);
        float heatDry = vptHeatDryness(column, textureId, fract(relativePosition.xz));
        return vec2(0.0, clamp(VPT_RAIN_WET_WALLS, 0.0, 1.0) * fade * (1.0 - heatDry));
    }
    fade *= smoothstep(0.5, 0.95, outwardNormal.y);

    int surfaceBlockY = cameraCell.y + int(floor(relativePosition.y - 0.001));
    ivec4 center = vptRainColumn(column, textureId);
    vec2 withinColumn = fract(relativePosition.xz);

    g_vptColumnSoak = vptColumnSoak(center);
    if (vptRainColumnBlocks(center, surfaceBlockY)) {
        // Under cover: how near the closest open column is.
        float nearest = 1e3;
        for (int dz = -2; dz <= 2; dz++) {
            for (int dx = -2; dx <= 2; dx++) {
                if (dx == 0 && dz == 0) { continue; }
                if (vptRainColumnBlocks(vptRainColumn(column + ivec2(dx, dz), textureId), surfaceBlockY)) { continue; }
                vec2 away = max(abs(withinColumn - (vec2(dx, dz) + 0.5)) - 0.5, vec2(0.0));
                nearest = min(nearest, length(away));
            }
        }
        float blownIn = 1.0 - smoothstep(0.0, SHELTER_REACH, nearest);
        // A column whose top is leaves (RainExposure marks it): the rain drips through.
        float leaves = center.b > 0 && center.b < 200 ? LEAF_DAMP : 0.0;
        return vec2(0.0, max(SHELTER_DAMP * blownIn * blownIn, leaves) * fade);
    }

    // The top of its column: wholly open. It dried off towards a wall or a higher block beside it before, which
    // left a dry band along every wall and step and a dry round patch in every inner corner while the water stood
    // all around. Only heat beside it keeps it drier.
    return vec2(fade * (1.0 - vptHeatDryness(column, textureId, withinColumn)));
}

float vptGroundOpen(vec3 position, vec3 outwardNormal) {
    return vptGroundRain(position, outwardNormal).x;
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
// The Wetness setting: how strongly wet surfaces darken and shine, 1 at 100 %.
#ifndef VPT_RAIN_WETNESS_STRENGTH
#    define VPT_RAIN_WETNESS_STRENGTH 1.0
#endif

// Walls: how much more soaked stone darkens, and how rough the film of water running down it is.
const float WET_WALL_DARKENING = 0.85;
const float WET_WALL_ROUGHNESS = 0.38;

/** Water fills the pores: rough, non-metal surfaces darken the most, and everything turns glossy. upright: a wall or
 *  any other upright face, which darkens more and shines less. Walls came out as dry as before or lighter: glossy
 *  like the ground, a wall faces the bright grey sky of the storm across the street and mirrored it, which undid the
 *  darkening. */
void vptApplyGroundWetness(inout LabPBRMat material, float wetness, bool upright) {
    if (wetness <= 0.0 || material.transmission > 0.0) { return; }
    float strength = max(VPT_RAIN_WETNESS_STRENGTH, 0.0);
    float absorbency = (1.0 - material.metallic) * clamp(material.roughness * 2.0, 0.0, 1.0);
    // Brick, stone and wood soak up the water running down them however smooth their pack paints them: at the
    // ground's measure a wet wall of Vanilla RTX's bricks came out 8 % darker, too little to see.
    if (upright) { absorbency = max(absorbency, 0.8 * (1.0 - material.metallic)); }
    float darkening = upright ? WET_WALL_DARKENING : WET_GROUND_DARKENING;
    material.albedo *= 1.0 - min(darkening * strength, 0.9) * wetness * absorbency;
    wetness = min(wetness * strength, 1.0);
    // Glossy, not a mirror: the standing water of a puddle (vptApplyPuddle) is what mirrors the world. At 0.08 all the
    // open ground mirrored the sky as one sheet of water, and the puddles had nothing to stand out from.
    // Water still running on it while it rains: glossier then, drying back to merely glossy once it stops.
    float wetRoughness = upright ? WET_WALL_ROUGHNESS :
                                   mix(WET_GROUND_ROUGHNESS, WET_GROUND_RAINING_ROUGHNESS, skyUBO.rainGradient);
    material.roughness = mix(material.roughness, min(material.roughness, wetRoughness), wetness);
    material.f0 = mix(material.f0, max(material.f0, vec3(0.02)), wetness);
}

// ---- Puddles and rain ripples

// How much standing water rain leaves on open, flat ground, 0..1 (the Puddles setting): full from 50 %.
#ifndef VPT_RAIN_PUDDLES
#    define VPT_RAIN_PUDDLES 0.5
#endif
// How large a drying patch is, in blocks (the period of the coarser noise), and how soft its rim is.
const float PUDDLE_SCALE = 10.0;
const float PUDDLE_RIM = 0.03;
// How long the film of water stands whole after the rain eases off, in seconds, and by when it has dried up patch
// by patch. The ground under it dries sooner (its wetness falls with a time constant of 25 s).
// The Drying Time setting, in seconds: when the last puddle has dried up after the rain. The film stands whole for
// the first PUDDLE_LINGER_SHARE of it.
#ifndef VPT_RAIN_DRYING_TIME
#    define VPT_RAIN_DRYING_TIME 180.0
#endif
const float PUDDLE_LINGER_SHARE = 0.055;
// The ground's wetness (RainExposure, rising with a time constant of 50 s in rain) at which the first water stands,
// and at which it has spread into one film.
const float PUDDLE_WET_START = 0.15;
const float PUDDLE_WET_FULL = 0.95;
// On grass and earth (a surface tinted by the biome): how much less of it holds standing water, and how rough that
// water is.
const float PUDDLE_POROUS_BIAS = 0.45;
const float PUDDLE_POROUS_ROUGHNESS = 0.18;
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

/** How much standing water there is here, 0..1, on the flat top of a full block under the open sky (open:
 *  vptGroundOpen; porous: 1 on grass and earth). As the rain goes on it gathers in patches, by a noise fixed to the
 *  world, that grow and merge into one film; once the rain stops it dries up in patches the other way round, the
 *  last ones gone by VPT_RAIN_DRYING_TIME. */
float vptPuddle(vec3 position, float open, float porous) {
    float amount = clamp(VPT_RAIN_PUDDLES * 2.0, 0.0, 1.0);
    if (amount <= 0.0 || open <= 0.0) { return 0.0; }
    bool raining = skyUBO.rainGradient > 0.2;
    // As far as this top has soaked against the ground around (vptColumnSoak): a block broken off it lately leaves
    // dry ground that gathers water as the rest did.
    float ground = max(skyUBO.rainWetness.x, 1e-4);
    float soaked = min(ground, g_vptColumnSoak);
    // rainStoppedFor is 0 while it rains and huge before any rain.
    float dryingTime = max(VPT_RAIN_DRYING_TIME, 1.0);
    if (!raining && skyUBO.rainStoppedFor > dryingTime) { return 0.0; }
    // How far the water has spread, 0..1: while it rains, with the ground's soaking (a couple of minutes to the
    // whole film); after, back down as it dries.
    float spread = raining ? smoothstep(PUDDLE_WET_START, PUDDLE_WET_FULL, soaked) :
                             (1.0 - smoothstep(dryingTime * PUDDLE_LINGER_SHARE, dryingTime, skyUBO.rainStoppedFor)) *
                                 smoothstep(0.5, 0.95, soaked / ground);
    if (spread <= 0.0) { return 0.0; }
    vec2 p = vptRainWorldXZ(position);
    // High noise is where water gathers: it pools there first and dries there last.
    float n = 0.75 * vptRainValueNoise(p / PUDDLE_SCALE) + 0.25 * vptRainValueNoise(p / (PUDDLE_SCALE * 0.33) + 17.0);
    // The noise runs about 0.15..0.85: the threshold sweeps across all of it. Grass and earth soak water up, so less
    // of it stands there.
    float threshold = mix(0.9, 0.1, spread) + PUDDLE_POROUS_BIAS * porous;
    return open * smoothstep(threshold, threshold + PUDDLE_RIM, n) * amount;
}

/** Standing water: as dark as soaked ground and smooth as water's surface, so it mirrors the sky and the lamps. */
void vptApplyPuddle(inout LabPBRMat material, float puddle, float porous) {
    if (puddle <= 0.0 || material.transmission > 0.0) { return; }
    material.albedo *= 1.0 - 0.45 * puddle * (1.0 - material.metallic);
    // Water among grass blades is broken up: glossy there, a mirror on stone.
    material.roughness = mix(material.roughness, mix(0.01, PUDDLE_POROUS_ROUGHNESS, porous), puddle);
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

/** Where the puddle and the rings are looked up for a hit at worldPos: a pack's point (hooks/rain_surface.glsl), or
 *  with Pixel Rain the centre of the sixteenth of a block it falls in, so each pixel is wholly puddle or dry ground
 *  and the rings step from pixel to pixel. */
vec3 vptRainSurfacePosition(vec3 worldPos) {
#ifndef RADIANTE_HOOK_RAIN_SURFACE_DEFAULT
    return hookRainPosition(worldPos);
#else
    if (VPT_RAIN_PIXELS != 0) {
        // worldPos is camera-relative: snapped in the world's grid, then handed back relative to the camera.
        vec2 cameraFraction = vec2(mod(worldUBO.cameraPos.xz, dvec2(1.0)));
        vec2 world = worldPos.xz + cameraFraction;
        worldPos.xz = (floor(world * RAIN_PIXELS_PER_BLOCK) + 0.5) / RAIN_PIXELS_PER_BLOCK - cameraFraction;
    }
    return worldPos;
#endif
}

/** The puddle's cover, through a pack's rim (hooks/rain_surface.glsl), or with Pixel Rain cut to a hard edge. */
float vptRainPuddleEdge(float puddle) {
#ifndef RADIANTE_HOOK_RAIN_SURFACE_DEFAULT
    return clamp(hookPuddleEdge(puddle), 0.0, 1.0);
#else
    return VPT_RAIN_PIXELS != 0 ? step(0.5, puddle) : puddle;
#endif
}

/** Pixel Rain's wetness of one texel of the texture hit: the surface's wetness where the texel is wet, and less where
 *  it is still drying (WET_PIXEL_DAMP of it). texel: the texel in the texture; row: how far down the face it is, in
 *  texels; seed: what tells one block from the next. On an upward face texels get wet at random, more of them as the
 *  rain soaks in (soak, 0..1). On an upright face the water runs down: in each column of texels the wet run starts at
 *  the top and reaches further down the longer it rains, a different length in each column. */
const float WET_PIXEL_DAMP = 0.35;
float vptRainPixelWetness(float wetness, ivec2 texel, float row, uint seed, bool upright, float soak) {
    if (wetness <= 0.0) { return 0.0; }
    if (upright) {
        float column = vptRainHash(vec2(float(texel.x) + float(seed & 0xFFFu) * 31.0, float(seed >> 12u)));
        float reach = (0.25 + 0.75 * column) * RAIN_PIXELS_PER_BLOCK * (0.3 + 0.7 * soak);
        // A drop or two caught lower down the column.
        float speck = vptRainHash(vec2(texel) + vec2(float(seed & 0xFFFu), 911.0)) < 0.12 * soak ? 1.0 : 0.0;
        return wetness * max(row < reach ? 1.0 : WET_PIXEL_DAMP, speck);
    }
    float h = vptRainHash(vec2(texel) + vec2(float(seed & 0xFFFu) * 17.0, float(seed >> 12u) * 5.0));
    return wetness * (h < 0.35 + 0.65 * soak ? 1.0 : WET_PIXEL_DAMP);
}

/** The normal turned by the ripples (slope from vptRainRippleSlope), on an upward face. */
vec3 vptApplyRainRipples(vec3 normal, vec2 slope, float strength) {
    if (strength <= 0.0 || dot(slope, slope) <= 0.0) { return normal; }
    return normalize(normal + vec3(-slope.x, 0.0, -slope.y) * strength);
}

#endif
