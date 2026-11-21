#ifndef VPT_RAIN_WETNESS_GLSL
#define VPT_RAIN_WETNESS_GLSL

#include "hooks/rain_surface.glsl"

// Wet ground in rain. The Java side keeps
// a texture of the top rain-blocking block of every column around the camera (RainExposure): an upward face that is
// the top of its column, in a biome where it rains rather than snows, gets darker and glossier with the rain, right up
// to a wall or the step of a higher block beside it, as the rain falls straight down there too.

// The texture's id and the rain clock, both carried in rainWetness.y (RainExposure: id * 256 + the clock in steps),
// negative with Snow on Paths & Farmland on.
uint vptRainTextureId() { return uint(floor(abs(skyUBO.rainWetness.y) / 256.0)); }
float vptRainClock() { return mod(abs(skyUBO.rainWetness.y), 256.0); }
bool vptSnowOnPaths() { return skyUBO.rainWetness.y < 0.0; }

// How far the top of the column last looked up has soaked, 0..1, against the ground around (set by vptGroundRain):
// a top that a block broken off it laid bare a moment ago is dry and soaks over the Wet-Up Time; a block put down
// is as wet as the ground round it. Its alpha holds the rain clock when it changed; 255 is an old top.
float g_vptColumnSoak = 1.0;
const float RAIN_CLOCK_STEPS = 253.0;
const float RAIN_CLOCK_UNIT = 0.025;
// Whether the column's stamp (alpha) is a block's under its top, swapped lately (green's bit 3, RainExposure's
// underStamp), rather than the top's own.
bool vptColumnUnderStamp(ivec4 data) { return (data.g & 8) != 0; }
// How long ago, in time constants of soaking on the rain clock, the column's stamp was set; -1 for none.
float vptColumnStampAge(ivec4 data) {
    if (data.a == 255 || data.a == 0) { return -1.0; }
    return mod(vptRainClock() - float(data.a - 1) + RAIN_CLOCK_STEPS, RAIN_CLOCK_STEPS) * RAIN_CLOCK_UNIT;
}
// How long ago the column's top changed; -1 for an old top.
float vptColumnSince(ivec4 data) { return vptColumnUnderStamp(data) ? -1.0 : vptColumnStampAge(data); }
// How long ago a block under the column's top was swapped; -1 for none lately.
float vptColumnUnderSince(ivec4 data) { return vptColumnUnderStamp(data) ? vptColumnStampAge(data) : -1.0; }

float vptColumnSoak(ivec4 data) {
    float since = vptColumnSince(data);
    return since < 0.0 ? 1.0 : 1.0 - exp(-since);
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
const float RAIN_PIXELS_PER_BLOCK = 16.0;
// Minecraft's look for the weather's marks: puddles, the rings on them and on water, the wakes, the wetness of every
// surface, the snow and the footprints in it go on in pixels of a sixteenth of a block, the size of a block
// texture's, with hard edges, as a texture painted for the game would show them. Always in Radiante PT; a pack that
// extends it may define VPT_RAIN_PIXELS 0 for smooth, round shapes (Aether's Pixel Weather).
#ifndef VPT_RAIN_PIXELS
#    define VPT_RAIN_PIXELS 1
#endif

float vptRainHash(vec2 cell);
float vptRainValueNoise(vec2 p);

/** Where the rain's marks end, a ragged edge instead of a clean cut: amount (0..1, how far into the change a point
 *  is) held against a noise fixed to the world, of two scales (blotches of a few blocks and single pixels), and with
 *  Pixel Rain stepped from pixel to pixel. 1 where the mark is, 0 where it is not, soft-edged without Pixel Rain.
 *  salt keeps the edges of different things (a roof, a fire, a biome) from fraying alike. */
float vptRainFray(vec2 world, float amount, float salt) {
    if (amount <= 0.0) { return 0.0; }
    if (amount >= 1.0) { return 1.0; }
    vec2 pixel = VPT_RAIN_PIXELS != 0 ? (floor(world * RAIN_PIXELS_PER_BLOCK) + 0.5) / RAIN_PIXELS_PER_BLOCK : world;
    float n = 0.65 * vptRainValueNoise(pixel * 1.3 + salt) +
              0.35 * vptRainHash(floor(pixel * RAIN_PIXELS_PER_BLOCK) + vec2(salt * 97.0, 0.0));
    return VPT_RAIN_PIXELS != 0 ? step(n, amount) : smoothstep(n - 0.15, n + 0.15, amount);
}

/** Where in its column (0..1 each way) the pixel of a point lies, at the pixel's centre with Pixel Rain: what is
 *  measured from it (how far to a fire, to the edge of a roof, to the open) is then one value over the whole pixel.
 *  Measured at the point itself, a ring round a lantern cut through the pixels on its edge along its curve: pixels
 *  sliced in two on a slant, like little triangles, at the rim of the melt and of the rime round it. */
vec2 vptRainPixelWithin(vec2 within) {
    return VPT_RAIN_PIXELS != 0 ? (floor(within * RAIN_PIXELS_PER_BLOCK) + 0.5) / RAIN_PIXELS_PER_BLOCK : within;
}

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
    if (dry <= 0.0) { return 0.0; }
    // A ragged edge, not a rounded square: the dry ground round a campfire frays out into the wet as a drying patch
    // does (vptRainFray).
    float edge = vptRainFray(vec2(column) + withinColumn, min(dry * 1.25, 1.0), 0.0);
    return max(edge * HEAT_DRYING, dry * dry * HEAT_DRYING);
}

// The height of the column's top block, fences, walls and panes left out (red and green's low 3 bits, offset by 1024);
// green's top bit: whether its last change was water or lava coming or going; the next three: what its top has half a
// block up (RainExposure.halfTopOf: 0 nothing, 1 a bottom slab, 2 to 5 a bottom stair facing north, east, south, west)
// or 6, a fence, a wall or a pane standing on it (RainExposure.isThin); bit 3: vptColumnUnderStamp.
int vptColumnHeight(ivec4 data) { return data.r + 256 * (data.g & 7) - 1024; }
bool vptColumnThinAbove(ivec4 data) { return ((data.g >> 4) & 7) == 6; }
bool vptColumnFluidChange(ivec4 data) { return (data.g & 128) != 0; }
int vptColumnHalfTop(ivec4 data) { return (data.g >> 4) & 7; }

/** Whether the top edge half a block up of a side of the column's top block is open to the snow at within (0..1 in
 *  the column): all of a slab's, and of a stair's the half of its step, away from the way it faces; elsewhere on the
 *  stair the side goes on up into its upper step. */
bool vptHalfEdgeOpen(ivec4 data, vec2 within) {
    int kind = vptColumnHalfTop(data);
    if (kind == 1) { return true; }
    if (kind < 2 || kind > 5) { return false; }
    vec2 facing = kind == 2 ? vec2(0.0, -1.0) : kind == 3 ? vec2(1.0, 0.0) : kind == 4 ? vec2(0.0, 1.0) : vec2(-1.0, 0.0);
    return dot(within - 0.5, facing) < -0.01;
}

bool vptRainColumnBlocks(ivec4 data, int surfaceBlockY) {
    return data.a == 0 || vptColumnHeight(data) > surfaceBlockY;
}

// How damp sheltered ground gets at most (beside a wall, at the rim of a tree's shade), against open ground, and how
// far in from the open the rain blown under a roof or a canopy reaches, in blocks.
const float SHELTER_DAMP = 0.45;
const float SHELTER_REACH = 2.0;
// Ground under leaves: the rain drips through them, so it is never quite dry there.
const float LEAF_DAMP = 0.3;
// How far the columns round a point count towards the weather there (vptBiomeShares), in blocks from their middles.
const float BIOME_REACH = 1.5;
// The fray both the rain and the snow cross a biome's edge by (vptRainFray), so that where they meet each pixel takes
// the one or the other: frayed apart, a pixel could be both wet and snowy, or neither.
const float BIOME_FRAY_SALT = 13.7;

#ifndef VPT_RAIN_WET_WALLS
#    define VPT_RAIN_WET_WALLS 0.8
#endif


/** How much of the rain (x) and of the snow (y) falls at a point, 0..1, from the columns round it, each counting as
 *  much as its middle is near (BIOME_REACH), then drawn in to a band about a block wide: the edge of a biome runs as
 *  one smooth line along its columns, rounding their corners, and vptRainFray frays it a pixel or two either way, as
 *  round a campfire. Columns read every two blocks and evened out over eight, it was a strip several blocks wide of
 *  scattered specks and block-sized steps. The two shares add up to 1 where rain and snow meet, so each pixel of the
 *  edge takes the one or the other (the line's wander moves both alike). */
vec2 vptBiomeShares(ivec2 column, vec2 within, uint textureId) {
    vec2 sum = vec2(0.0);
    float total = 0.0;
    for (int dz = -1; dz <= 1; dz++) {
        for (int dx = -1; dx <= 1; dx++) {
            vec2 d = abs(within - (vec2(dx, dz) + 0.5));
            float weight = max(1.0 - d.x / BIOME_REACH, 0.0) * max(1.0 - d.y / BIOME_REACH, 0.0);
            if (weight <= 0.0) { continue; }
            int b = vptRainColumn(column + ivec2(dx, dz), textureId).b;
            sum += weight * vec2((b & 128) != 0 ? 1.0 : 0.0, (b & 160) == 32 ? 1.0 : 0.0);
            total += weight;
        }
    }
    // The line wanders a little either way over a few blocks, so it does not trace the columns' corners: following
    // them alone, the edge was a chain of rounded steps, too regular to be the ground's own.
    float wander = (vptRainValueNoise((vec2(column) + within) * 0.45 + 71.0) - 0.5) * 0.4;
    return smoothstep(vec2(0.3), vec2(0.7), sum / max(total, 1e-4) + vec2(wander, -wander));
}

/** Under cover: how far the closest column open at the surface's height is from a point of column, in blocks (1e3 with
 *  none within two columns), and the way to its nearest point. */
float vptShelterNearest(ivec2 column, vec2 withinColumn, int surfaceBlockY, uint textureId, out vec2 toward) {
    float nearest = 1e3;
    toward = vec2(0.0);
    for (int dz = -2; dz <= 2; dz++) {
        for (int dx = -2; dx <= 2; dx++) {
            if (dx == 0 && dz == 0) { continue; }
            if (vptRainColumnBlocks(vptRainColumn(column + ivec2(dx, dz), textureId), surfaceBlockY)) { continue; }
            vec2 to = clamp(withinColumn, vec2(dx, dz), vec2(dx, dz) + 1.0) - withinColumn;
            float away = length(to);
            if (away < nearest) {
                nearest = away;
                toward = to;
            }
        }
    }
    return nearest;
}

// What pt_material.glsl found for the upward face being shaded (vptShelterProbe and a ray): how far the closest open
// column is that the rain and the snow can come in from, 1e3 with a wall in the way; negative when not worked out,
// and the columns alone are gone by. The columns do not tell a wall from the room behind it (both lie under the
// roof), so by them alone the rain blown in and the snow drifting in reached through every outside wall: a damp,
// frosted strip along the inside of a house's walls, and specks of water on the floor of its corners.
float g_vptShelterNearest = -1.0;

/** Whether position is on an upward face under cover with an open column in reach; then how far, and the way to it. */
bool vptShelterProbe(vec3 position, out float nearest, out vec2 toward) {
    nearest = 1e3;
    toward = vec2(0.0);
    uint textureId = vptRainTextureId();
    if (textureId == 0u || max(abs(position.x), abs(position.z)) > 238.0) { return false; }
    ivec3 cameraCell = ivec3(floor(worldUBO.cameraPos.xyz));
    vec3 relativePosition = position + vec3(worldUBO.cameraPos.xyz - dvec3(cameraCell));
    ivec2 column = cameraCell.xz + ivec2(floor(relativePosition.xz));
    int surfaceBlockY = cameraCell.y + int(floor(relativePosition.y - 0.001));
    ivec4 center = vptRainColumn(column, textureId);
    if (center.b == 0 || !vptRainColumnBlocks(center, surfaceBlockY)) { return false; }
    nearest = vptShelterNearest(column, vptRainPixelWithin(fract(relativePosition.xz)), surfaceBlockY, textureId, toward);
    return true;
}

/** How the rain reaches a face here, whatever the weather.
 *  x: how open an upward face is to the sky, 0..1, for standing water: 1 on the top of its column, fading towards a
 *  wall or overhang and at the edge of the texture.
 *  y: how damp the face gets, 0..1. Upward faces: as x in the open but never under SHELTER_DAMP there; under cover
 *  the rain blown in from the open columns around, fading over SHELTER_REACH blocks, and under leaves never less
 *  than LEAF_DAMP. Walls and other upright faces: VPT_RAIN_WET_WALLS where the column in front of them is open to
 *  the sky at their height. Undersides stay dry. Both fade over a few blocks at the edge of a biome where it rains
 *  (vptBiomeShares). It was cut hard at the edge of the open sky and of the biome before, and only floors got wet. */
vec2 vptGroundRain(vec3 position, vec3 outwardNormal) {
    g_vptColumnSoak = 1.0;
    uint textureId = vptRainTextureId();
    if (textureId == 0u || outwardNormal.y < -0.3) { return vec2(0.0); }
    // The texture covers 256 blocks each way.
    if (max(abs(position.x), abs(position.z)) > 238.0) { return vec2(0.0); }
    ivec3 cameraCell = ivec3(floor(worldUBO.cameraPos.xyz));
    vec3 relativePosition = position + vec3(worldUBO.cameraPos.xyz - dvec3(cameraCell));
    ivec2 column = cameraCell.xz + ivec2(floor(relativePosition.xz));
    vec2 biomeWithin = vptRainPixelWithin(fract(relativePosition.xz));
    float biome = vptBiomeShares(column, biomeWithin, textureId).x;
    if (biome <= 0.0) { return vec2(0.0); }
    // Across a biome's edge the wet ground frays out into the dry instead of thinning evenly: the pixels the snow of a
    // snowy biome beside does not take (vptGroundSnow), and only those.
    biome = 1.0 - vptRainFray(vec2(column) + biomeWithin, 1.0 - biome, BIOME_FRAY_SALT);
    if (biome <= 0.0) { return vec2(0.0); }
    float fade = (1.0 - smoothstep(220.0, 238.0, max(abs(position.x), abs(position.z)))) * biome;

    if (outwardNormal.y < 0.5) {
        // An upright face: wet where the air in front of it is open to the sky at its height.
        ivec2 front = cameraCell.xz + ivec2(floor(relativePosition.xz + outwardNormal.xz * 0.5));
        int faceBlockY = cameraCell.y + int(floor(relativePosition.y));
        ivec4 frontData = vptRainColumn(front, textureId);
        if (vptRainColumnBlocks(frontData, faceBlockY)) {
            // Covered lately (a block put down in front of the wall in the rain): the wall under it dries over a
            // minute or so of rain instead of at once.
            float since = vptColumnSince(frontData);
            float covered = since >= 0.0 && (frontData.b & 4) == 0 ? exp(-since * 1.5) : 0.0;
            return vec2(0.0, clamp(VPT_RAIN_WET_WALLS, 0.0, 1.0) * fade * covered);
        }
        // A face the column in front of it just opened up (the sides of a hole dug in the rain) soaks as that
        // column's top does.
        // Only a change that lowered the column in front (a hole dug) opens new faces: a block put down in front
        // leaves the wall above it as wet as it was. Taking that soaking for every change, the whole strip of wall
        // over a block put against it went dry at once.
        g_vptColumnSoak = (frontData.b & 4) != 0 ? vptColumnSoak(frontData) : 1.0;
        float heatDry = vptHeatDryness(column, textureId, vptRainPixelWithin(fract(relativePosition.xz)));
        return vec2(0.0, clamp(VPT_RAIN_WET_WALLS, 0.0, 1.0) * fade * (1.0 - heatDry));
    }
    fade *= smoothstep(0.5, 0.95, outwardNormal.y);

    int surfaceBlockY = cameraCell.y + int(floor(relativePosition.y - 0.001));
    ivec4 center = vptRainColumn(column, textureId);
    vec2 withinColumn = vptRainPixelWithin(fract(relativePosition.xz));

    // Only a top that a block taken away laid bare (RainExposure's bit 2) starts dry: it was covered until then. A
    // block put down in the rain comes out as wet as the ground around, where it showed a dry top that soaked in.
    bool laidBare = (center.b & 4) != 0;
    g_vptColumnSoak = laidBare ? vptColumnSoak(center) : 1.0;
    if (vptRainColumnBlocks(center, surfaceBlockY)) {
        // A block put down on wet ground in the rain covers it: the ground under it dries over a minute or so of
        // rain on the clock, where it went dry at once. (The soaking of a new top is the new top's, not this.)
        g_vptColumnSoak = 1.0;
        float since = vptColumnSince(center);
        float covered = since >= 0.0 ? exp(-since * 1.5) : 0.0;
        // Under cover: how near the closest open column is.
        vec2 shelterToward;
        float nearest = g_vptShelterNearest >= 0.0 ? g_vptShelterNearest
            : vptShelterNearest(column, withinColumn, surfaceBlockY, textureId, shelterToward);
        float blownIn = 1.0 - smoothstep(0.0, SHELTER_REACH, nearest);
        // Under the edge of a roof or a canopy the rain blown in wets the ground in a ragged fringe, wet pixels
        // reaching in among dry ones, and the water standing outside creeps in a little the same way, instead of
        // both stopping at a straight line under the eaves.
        vec2 world = vec2(column) + withinColumn;
        float fringe = vptRainFray(world, 1.0 - smoothstep(0.0, 0.9, nearest), 3.1);
        float creep = vptRainFray(world, 1.0 - smoothstep(0.0, 0.45, nearest), 5.3);
        // A column whose top is leaves (RainExposure marks it): the rain drips through.
        float leaves = (center.b & 192) == 128 ? LEAF_DAMP : 0.0;
        return vec2(max(creep, covered),
                    max(max(max(SHELTER_DAMP * blownIn * blownIn, 0.9 * fringe), leaves), covered)) * fade;
    }

    // A top that changed lately (a block taken away) is dry, but not as a clean square in the wet: the wet ground
    // round it reaches in from its edges in a ragged fringe that grows as it soaks (vptRainFray), the way the dry
    // patch round a fire frays out.
    float since = laidBare ? vptColumnSince(center) : -1.0;
    if (since >= 0.0) {
        vec2 edge = min(withinColumn, vec2(1.0) - withinColumn);
        float fromEdge = min(edge.x, edge.y);
        float reach = mix(0.3, 0.65, clamp(since, 0.0, 1.0));
        float creep = vptRainFray(vec2(column) + withinColumn, 1.0 - smoothstep(0.0, reach, fromEdge), 11.3);
        g_vptColumnSoak = max(g_vptColumnSoak, creep);
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
// A wall's place for Pixel Rain's runs of water, from the world rather than the texture: how far below the top of
// the wall a point is, in pixels (a sixteenth of a block), and which column of pixels along the wall it is in, so the
// water runs down a whole wall from its top, across the joins between its blocks, instead of starting again at the
// top of each block. x: pixels from the top, over the wall's height in blocks (so 16 is its foot); y: the pixel
// column; z: the pixel row (for drops caught on the way).
vec3 vptWallRainPlace(vec3 position, vec3 outwardNormal) {
    uint textureId = vptRainTextureId();
    vec3 world = vec3(worldUBO.cameraPos.xyz - dvec3(floor(worldUBO.cameraPos.xyz))) + position;
    ivec3 cameraCell = ivec3(floor(worldUBO.cameraPos.xyz));
    vec2 inside = world.xz - outwardNormal.xz * 0.5;
    ivec2 column = cameraCell.xz + ivec2(floor(inside));
    ivec4 data = vptRainColumn(column, textureId);
    float top = float(vptColumnHeight(data) + 1);
    float worldY = float(cameraCell.y) + world.y;
    // The wall's height over the ground in front of it (six blocks at most): the runs are spread over it, so with the
    // rain going on the longest of them reach the ground.
    ivec4 front = vptRainColumn(cameraCell.xz + ivec2(floor(world.xz + outwardNormal.xz * 0.5)), textureId);
    float ground = float(vptColumnHeight(front) + 1);
    float height = clamp(top - max(ground, top - 6.0), 1.0, 6.0);
    vec2 along = vec2(-outwardNormal.z, outwardNormal.x);
    vec2 absXZ = vec2(cameraCell.xz) + world.xz;
    return vec3(max(top - worldY, 0.0) * RAIN_PIXELS_PER_BLOCK / height, floor(dot(absXZ, along) * RAIN_PIXELS_PER_BLOCK),
                floor(worldY * RAIN_PIXELS_PER_BLOCK));
}

float vptRainPixelWetness(float wetness, ivec2 texel, float row, uint seed, bool upright, float soak) {
    if (wetness <= 0.0) { return 0.0; }
    if (upright) {
        float column = vptRainHash(vec2(float(texel.x) + float(seed & 0xFFFu) * 31.0, float(seed >> 12u)));
        float reach = (0.25 + 0.75 * column) * RAIN_PIXELS_PER_BLOCK * (0.3 + 0.7 * soak);
        // A drop or two caught lower down the column.
        float speck = vptRainHash(vec2(texel) + vec2(float(seed & 0xFFFu), 911.0)) < 0.12 * soak ? 1.0 : 0.0;
        return wetness * max(row < reach ? 1.0 : WET_PIXEL_DAMP, speck);
    }
    // Drops of a few sizes, as the snow's flakes (vptSnowCover): single texels and clumps of two and three a side,
    // wet at their own moments evenly over the face, the clumps sooner. Single texels alone fizzed.
    vec2 salt = vec2(float(seed & 0xFFFu) * 17.0, float(seed >> 12u) * 5.0);
    float cover = 0.2 + 0.8 * soak;
    float small = vptRainHash(vec2(texel) + salt);
    float medium = vptRainHash(floor(vec2(texel) / 2.0) + salt + vec2(613.0, 151.0));
    float large = vptRainHash(floor((vec2(texel) + vec2(1.0, 2.0)) / 3.0) + salt + vec2(887.0, 433.0));
    bool wet = small < cover * 1.02 || medium < cover * 0.75 || large < cover * 0.5;
    return wetness * (wet ? 1.0 : WET_PIXEL_DAMP);
}

/** The normal turned by the ripples (slope from vptRainRippleSlope), on an upward face. */
vec3 vptApplyRainRipples(vec3 normal, vec2 slope, float strength) {
    if (strength <= 0.0 || dot(slope, slope) <= 0.0) { return normal; }
    return normalize(normal + vec3(-slope.x, 0.0, -slope.y) * strength);
}

// ---- Progressive snow and frost in cold biomes

#ifndef VPT_SNOW_FROST_STRENGTH
#    define VPT_SNOW_FROST_STRENGTH 1.0
#endif

float g_vptSnowSoak = 1.0;

/** Heat melting around torches, lanterns, campfires, furnaces and lava, and bright lights (glowstone, froglights,
 *  sea lanterns: RainExposure's bit 4 where it snows, melting as a torch does): x melts the snow blanket, y melts the
 *  thinner rime frost further out, both frayed with vptRainFray instead of stopping at a clean circle. A light's own
 *  face keeps clear (vptApplySnowFrost), and the snow round it stopped dead at its edge before. */
vec2 vptSnowHeatMelt(ivec2 column, uint textureId, vec2 withinColumn) {
    float melt = 0.0;
    for (int dz = -2; dz <= 2; dz++) {
        for (int dx = -2; dx <= 2; dx++) {
            ivec4 data = vptRainColumn(column + ivec2(dx, dz), textureId);
            int heat = max(vptColumnHeat(data), data.b > 0 && (data.b & 16) != 0 ? 2 : 0);
            if (heat == 0) { continue; }
            vec2 away = max(abs(withinColumn - (vec2(dx, dz) + 0.5)) - 0.5, vec2(0.0));
            float reach = heat == 3 ? 2.2 : 1.25;
            melt = max(melt, (float(heat) / 3.0) * (1.0 - smoothstep(0.0, reach, length(away))));
        }
    }
    if (melt <= 0.0) { return vec2(0.0); }
    vec2 world = vec2(column) + withinColumn;
    float snowMelt = max(vptRainFray(world, min(melt * 1.35, 1.0), 19.1), melt * melt);
    float frostMelt = max(vptRainFray(world, min(melt * 1.15, 1.0), 23.7), melt * melt * 0.9);
    return clamp(vec2(snowMelt, frostMelt), vec2(0.0), vec2(1.0));
}

/** The melt round fire alone (vptColumnHeat 3: fire, lava, magma, a lit campfire or furnace), for the snow on fences,
 *  walls and panes: a torch or a lantern does not clear the snow off the fence it hangs from or stands by, as its light
 *  does off the ground round it. */
vec2 vptSnowFireMelt(ivec2 column, uint textureId, vec2 withinColumn) {
    float melt = 0.0;
    for (int dz = -2; dz <= 2; dz++) {
        for (int dx = -2; dx <= 2; dx++) {
            if (vptColumnHeat(vptRainColumn(column + ivec2(dx, dz), textureId)) != 3) { continue; }
            vec2 away = max(abs(withinColumn - (vec2(dx, dz) + 0.5)) - 0.5, vec2(0.0));
            melt = max(melt, 1.0 - smoothstep(0.0, 2.2, length(away)));
        }
    }
    if (melt <= 0.0) { return vec2(0.0); }
    vec2 world = vec2(column) + withinColumn;
    float snowMelt = max(vptRainFray(world, min(melt * 1.35, 1.0), 19.1), melt * melt);
    float frostMelt = max(vptRainFray(world, min(melt * 1.15, 1.0), 23.7), melt * melt * 0.9);
    return clamp(vec2(snowMelt, frostMelt), vec2(0.0), vec2(1.0));
}

// Whether the top (of a face up) or the top edge (of a side) of a fence, a wall or a pane above its column's top is
// open to the sky: the column map lets the snow fall past them (RainExposure.isThin) and does not know how high they
// stand, so pt_material.glsl asks with a ray straight up (vptSnowOnThin says when).
bool g_vptThinTopOpen = false;

/** Whether a face is part of fences, walls or panes standing over its column's top in a snowy column while snow may
 *  lie: the case g_vptThinTopOpen is needed for. */
bool vptSnowOnThin(vec3 position, vec3 outwardNormal) {
    if (skyUBO.rainGradient <= 0.2 && skyUBO.rainStoppedFor > max(VPT_RAIN_DRYING_TIME, 1.0) * 1.35) { return false; }
    uint textureId = vptRainTextureId();
    if (textureId == 0u || max(abs(position.x), abs(position.z)) > 238.0) { return false; }
    ivec3 cameraCell = ivec3(floor(worldUBO.cameraPos.xyz));
    vec3 relativePosition = position + vec3(worldUBO.cameraPos.xyz - dvec3(cameraCell));
    bool up = outwardNormal.y > 0.5;
    ivec2 own = cameraCell.xz + ivec2(floor(relativePosition.xz - (up ? vec2(0.0) : outwardNormal.xz * 0.02)));
    int blockY = cameraCell.y + int(floor(relativePosition.y - (up ? 0.001 : 0.0)));
    ivec4 data = vptRainColumn(own, textureId);
    return data.a != 0 && (data.b & 160) == 32 && vptColumnThinAbove(data) && blockY > vptColumnHeight(data);
}

/** How far the snow has settled on a top that has soaked this far (g_vptSnowSoak), 0..1: evenly over the Wet-Up
 *  Time while it snows, thinning over VPT_RAIN_DRYING_TIME after it stops; times the Snow & Frost Cover setting. */
float vptSnowSpread(float topSoak) {
    float strength = clamp(VPT_SNOW_FROST_STRENGTH, 0.0, 2.0);
    if (strength <= 0.0) { return 0.0; }
    bool snowing = skyUBO.rainGradient > 0.2;
    float ground = max(skyUBO.rainWetness.x, 1e-4);
    float soaked = min(ground, topSoak);
    float dryingTime = max(VPT_RAIN_DRYING_TIME, 1.0) * 1.35;
    if (!snowing && skyUBO.rainStoppedFor > dryingTime) { return 0.0; }
    // While it snows the blanket is whole by about half the ground's soaking (a minute or so at the default Wet-Up
    // Time): it took nearly all of it, which left the surface half-covered in flakes for minutes.
    float spread = snowing ? smoothstep(0.05, 0.5, soaked) :
                             (1.0 - smoothstep(dryingTime * 0.12, dryingTime, skyUBO.rainStoppedFor)) *
                                 smoothstep(0.35, 0.90, soaked / ground);
    return clamp(spread * min(strength, 1.35), 0.0, 1.0);
}

/** The snow on the side of a block whose top carries it, 0..1: a band a few pixels deep along the top edge with a
 *  ragged lower edge, deeper as the snow on the top builds, the way Minecraft's snowy grass block draws its sides.
 *  below: how far under the block's top the point is, in blocks; along: across the face, in blocks. The walls took a
 *  pale film of rime over their whole height before, which read as a washed-out wall more than as snow. */
// How wide the view's ray is where it meets the surface, in blocks, the camera-relative height of the top edge of the
// face hit (above 1e8, the top of the block), and, for a side under a roof (vptSnowSideUnderCover), whether there is
// open air over its top edge. pt_material.glsl sets them before vptGroundSnow.
float g_vptSnowFootprint = 0.0;
float g_vptSnowFaceTop = 1e9;
bool g_vptSnowAboveOpen = false;

float vptSnowSideFringe(float below, float along, float spread, float salt) {
    if (spread <= 0.0 || below > 0.3) { return 0.0; }
    // Where a screen pixel covers more than a pixel of the texture, more and more of the columns take the band's
    // average depth, so its lower edge goes straight: one pixel wide and one apart, its steps flickered from frame to
    // frame as the ray landed on one or the next. Each pixel stays snow or no snow: blending the two, the band went
    // half see-through in a stretch of every wall at middle distance.
    float footprint = max(g_vptSnowFootprint, 0.004);
    if (VPT_RAIN_PIXELS != 0) {
        float column = floor(along * RAIN_PIXELS_PER_BLOCK);
        float depth = spread * mix(1.0, 4.6, vptRainHash(vec2(column, salt)));
        // Each column of the band comes in with a pixel of its own once the snow has settled that far, as the flakes
        // come in on the top: under a light snow the band stayed away altogether (no column a pixel deep yet) while
        // the top over it was already speckled, so a stair or a block put down lately had a white top and bare sides.
        depth = spread >= 0.8 * vptRainHash(vec2(column, salt + 23.0)) ? max(depth, 1.0) : 0.0;
        float straight = smoothstep(0.7, 1.6, footprint * RAIN_PIXELS_PER_BLOCK);
        if (vptRainHash(vec2(column, salt + 57.0)) < straight) { depth = max(floor(spread * 2.8 + 0.5), 1.0); }
        return floor(below * RAIN_PIXELS_PER_BLOCK) < floor(depth) ? 1.0 : 0.0;
    }
    float average = spread * 2.8 / RAIN_PIXELS_PER_BLOCK;
    float smoothBand = 1.0 - smoothstep(average - footprint, average + footprint, below);
    float depth = spread * mix(1.0, 4.6, vptRainValueNoise(vec2(along * 4.0, salt))) / RAIN_PIXELS_PER_BLOCK;
    float band = 1.0 - smoothstep(depth - 0.015, depth + 0.015, below);
    return mix(band, smoothBand, smoothstep(0.03, 0.09, footprint));
}

/** How snowfall and frost reach a surface in a snowy biome.
 *  On upward faces, x: openness to falling snow (0..1), melted near heat sources and frayed under roof eaves; y: rime
 *  frost reach (0..1), including the ground under leaves and by the eaves. On upright faces, x: the snow along the top
 *  edge of a block whose top carries it, already as far as it has settled (vptSnowSideFringe); y: none. */
/** How far the snow has settled round a plant at position, 0..1, for the dusting on grass and ferns (pt_material.glsl):
 *  as on the open ground it grows from (the top of its column, one or two blocks under it), frayed at a biome's edge
 *  and melted round heat as that ground is; none under leaves or over water, where the ground takes none either. */
float vptPlantSnowSettled(vec3 position) {
    if (skyUBO.rainGradient <= 0.2 && skyUBO.rainStoppedFor > max(VPT_RAIN_DRYING_TIME, 1.0) * 1.35) { return 0.0; }
    uint textureId = vptRainTextureId();
    if (textureId == 0u || max(abs(position.x), abs(position.z)) > 238.0) { return 0.0; }
    ivec3 cameraCell = ivec3(floor(worldUBO.cameraPos.xyz));
    vec3 relativePosition = position + vec3(worldUBO.cameraPos.xyz - dvec3(cameraCell));
    ivec2 column = cameraCell.xz + ivec2(floor(relativePosition.xz));
    ivec4 data = vptRainColumn(column, textureId);
    int plantY = cameraCell.y + int(floor(relativePosition.y));
    int height = vptColumnHeight(data);
    if (data.a == 0 || plantY <= height || plantY > height + 2 || (data.b & 224) != 96 || (data.b & 8) != 0) {
        return 0.0;
    }
    vec2 within = vptRainPixelWithin(fract(relativePosition.xz));
    float biome = vptBiomeShares(column, within, textureId).y;
    if (biome <= 0.0) { return 0.0; }
    biome = vptRainFray(vec2(column) + within, biome, BIOME_FRAY_SALT);
    if (biome <= 0.0) { return 0.0; }
    float spread = vptSnowSpread(vptColumnSince(data) >= 0.0 ? vptColumnSoak(data) : 1.0);
    vec2 melt = vptSnowHeatMelt(column, textureId, within);
    return spread * (1.0 - melt.x) * (1.0 - smoothstep(220.0, 238.0, max(abs(position.x), abs(position.z))));
}

/** Whether a side is that of a block under a roof in a snowy column, facing a column open at its height: the one case
 *  vptGroundSnow needs to know about the air over the block's top for (g_vptSnowAboveOpen), told by the columns. */
bool vptSnowSideUnderCover(vec3 position, vec3 outwardNormal) {
    uint textureId = vptRainTextureId();
    if (textureId == 0u || max(abs(position.x), abs(position.z)) > 238.0) { return false; }
    ivec3 cameraCell = ivec3(floor(worldUBO.cameraPos.xyz));
    vec3 relativePosition = position + vec3(worldUBO.cameraPos.xyz - dvec3(cameraCell));
    ivec2 front = cameraCell.xz + ivec2(floor(relativePosition.xz + outwardNormal.xz * 0.02));
    ivec2 own = cameraCell.xz + ivec2(floor(relativePosition.xz - outwardNormal.xz * 0.02));
    if (front == own) { return false; }
    int faceBlockY = cameraCell.y + int(floor(relativePosition.y));
    ivec4 ownData = vptRainColumn(own, textureId);
    if (ownData.a == 0 || (ownData.b & 160) != 32 || vptColumnHeight(ownData) <= faceBlockY) { return false; }
    return !vptRainColumnBlocks(vptRainColumn(front, textureId), faceBlockY);
}

vec2 vptGroundSnow(vec3 position, vec3 outwardNormal) {
    g_vptSnowSoak = 1.0;
    // Nothing falling and the last snow long gone (as vptSnowCover reads it): every hit in a snowy biome on a clear
    // day still paid for the biome, heat and neighbour lookups only to have the result thrown away.
    if (skyUBO.rainGradient <= 0.2 && skyUBO.rainStoppedFor > max(VPT_RAIN_DRYING_TIME, 1.0) * 1.35) {
        return vec2(0.0);
    }
    uint textureId = vptRainTextureId();
    if (textureId == 0u || outwardNormal.y < -0.25) { return vec2(0.0); }
    if (max(abs(position.x), abs(position.z)) > 238.0) { return vec2(0.0); }
    ivec3 cameraCell = ivec3(floor(worldUBO.cameraPos.xyz));
    vec3 relativePosition = position + vec3(worldUBO.cameraPos.xyz - dvec3(cameraCell));
    ivec2 column = cameraCell.xz + ivec2(floor(relativePosition.xz));
    vec2 withinColumn = vptRainPixelWithin(fract(relativePosition.xz));
    float biome = vptBiomeShares(column, withinColumn, textureId).y;
    if (biome <= 0.0) { return vec2(0.0); }
    biome = vptRainFray(vec2(column) + withinColumn, biome, BIOME_FRAY_SALT);
    if (biome <= 0.0) { return vec2(0.0); }
    float fade = (1.0 - smoothstep(220.0, 238.0, max(abs(position.x), abs(position.z)))) * biome;

    if (outwardNormal.y < 0.5) {
        // The columns a hair either side of the face: a face on a column's edge has its block behind and the next
        // column in front, and one inside its block (the riser of a stair, between its two steps) has its own column
        // on both sides. Half a block either side put that riser's own column exactly on the edge, so it was taken
        // for the column behind or the one in front from one frame to the next: the band flickered or was missing.
        ivec2 front = cameraCell.xz + ivec2(floor(relativePosition.xz + outwardNormal.xz * 0.02));
        ivec2 own = cameraCell.xz + ivec2(floor(relativePosition.xz - outwardNormal.xz * 0.02));
        int faceBlockY = cameraCell.y + int(floor(relativePosition.y));
        // Under an overhang, or against a block in front: no snow reaches it. A face inside its block has open air
        // in front once its column's top is this block (below).
        if (front != own && vptRainColumnBlocks(vptRainColumn(front, textureId), faceBlockY)) { return vec2(0.0); }
        // The side of the top block of its column, whose top the snow lies on, and not where water is the top; or of
        // a block under a roof with open air over it (the rim of a well under its roof, a step under a porch), on the
        // side facing the open, where the snow blown in under the roof lies along its top's edge.
        ivec4 ownData = vptRainColumn(own, textureId);
        if (ownData.a == 0 || (ownData.b & 8) != 0) { return vec2(0.0); }
        int ownHeight = vptColumnHeight(ownData);
        bool sheltered = ownHeight > faceBlockY && front != own && g_vptSnowAboveOpen;
        // A fence post, a wall or a pane over the column's top, its top open to the sky: the band runs along its own
        // top edge, wherever that is (a fence's rails, a wall's lower arms).
        bool thinSide = vptColumnThinAbove(ownData) && faceBlockY > ownHeight && g_vptThinTopOpen;
        if (ownHeight != faceBlockY && !sheltered && !thinSide) { return vec2(0.0); }
        float underSince = vptColumnUnderSince(ownData);
        float spread = sheltered ? vptSnowSpread(underSince >= 0.0 ? 1.0 - exp(-underSince) : 1.0)
                                 : vptSnowSpread(vptColumnSince(ownData) >= 0.0 ? vptColumnSoak(ownData) : 1.0);
        vec2 sideWorld = vptRainWorldXZ(position);
        float along = abs(outwardNormal.z) > abs(outwardNormal.x) ? sideWorld.x : sideWorld.y;
        float salt = float(faceBlockY & 255) + (abs(outwardNormal.x) > 0.5 ? 17.0 : 0.0) +
                     (outwardNormal.x + outwardNormal.z > 0.0 ? 41.0 : 0.0);
        // Measured from the top edge of the face itself, so a slab's sides carry the band under its top half a
        // block up; on a full block's (or a stair's upper half's) side that is the block's top. A face whose top
        // edge lies inside the block anywhere else (the lower half of a stair's side, under its upper step) has none.
        float faceTop = g_vptSnowFaceTop > 1e8 ? float(faceBlockY - cameraCell.y + 1)
                                               : g_vptSnowFaceTop + float(worldUBO.cameraPos.y - double(cameraCell.y));
        float topInBlock = faceTop - float(faceBlockY - cameraCell.y);
        bool topEdge = thinSide || abs(topInBlock - 1.0) < 0.01 || (sheltered && abs(topInBlock - 0.5) < 0.01) ||
                       (abs(topInBlock - 0.5) < 0.01 &&
                        vptHalfEdgeOpen(ownData, clamp(relativePosition.xz - vec2(own - cameraCell.xz), 0.0, 1.0)));
        if (!topEdge) { return vec2(0.0); }
        float fringe = vptSnowSideFringe(faceTop - relativePosition.y, along, spread, salt);
        if (fringe <= 0.0) { return vec2(0.0); }
        vec2 sideWithin = vptRainPixelWithin(clamp(relativePosition.xz - vec2(own - cameraCell.xz), vec2(0.0),
                                                   vec2(0.9999)));
        vec2 melt = thinSide ? vptSnowFireMelt(own, textureId, sideWithin)
                             : vptSnowHeatMelt(own, textureId, sideWithin);
        return vec2(fringe * fade * (1.0 - melt.x), 0.0);
    }
    fade *= smoothstep(0.5, 0.95, outwardNormal.y);

    int surfaceBlockY = cameraCell.y + int(floor(relativePosition.y - 0.001));
    ivec4 center = vptRainColumn(column, textureId);
    bool laidBare = (center.b & 4) != 0;
    // A new top starts bare and gathers the snow as the ground did when it began to snow: a block put down (it came
    // out under as much snow as the ground round it), and water or lava come or gone, with no snow reaching in from
    // its edges. Only a top a block taken away laid bare has the snow round it reach in from its edges too.
    bool fluidChange = vptColumnFluidChange(center) && vptColumnSince(center) >= 0.0;
    g_vptSnowSoak = vptColumnSince(center) >= 0.0 ? vptColumnSoak(center) : 1.0;

    // A top above the one its column holds: a block just put down, before RainExposure has read it (that takes a
    // moment). It is bare, as it will be once read; taken for the open top of its column it came out under the snow
    // of the ground it was put on, and lost it a second or two later.
    // The top of a fence, a wall or a pane over the top block takes the snow as that top does, however high it stands
    // (a post on a post, the beam of a frame, a wall on a wall), wherever nothing is over it (g_vptThinTopOpen): the
    // lower rail of a fence, under the upper one, takes none.
    bool thinTop = vptColumnThinAbove(center) && vptColumnHeight(center) < surfaceBlockY && g_vptThinTopOpen;
    if (center.a != 0 && vptColumnHeight(center) < surfaceBlockY && !thinTop) {
        return vec2(0.0);
    }

    if (vptRainColumnBlocks(center, surfaceBlockY)) {
        // The floor of a pool or a river: snow does not lie under water, nor drift in from the bank. It showed the
        // snow that lay there before the water came, and drifts reaching in from the snowy ground round the pool.
        if ((center.b & 8) != 0) { return vec2(0.0); }
        g_vptSnowSoak = 1.0;
        float since = vptColumnSince(center);
        float covered = since >= 0.0 ? exp(-since * 1.5) : 0.0;
        // A block swapped under the roof lately: the snow blown in on it gathers again, as on a new top in the open.
        float underSince = vptColumnUnderSince(center);
        float regrow = underSince >= 0.0 ? vptSnowSpread(1.0 - exp(-underSince)) /
                                               max(vptSnowSpread(1.0), 1e-4) : 1.0;
        vec2 shelterToward;
        float nearest = g_vptShelterNearest >= 0.0 ? g_vptShelterNearest
            : vptShelterNearest(column, withinColumn, surfaceBlockY, textureId, shelterToward);
        vec2 world = vec2(column) + withinColumn;
        float driftCreep = vptRainFray(world, 1.0 - smoothstep(0.0, 0.65, nearest), 15.3);
        float frostFringe = vptRainFray(world, 1.0 - smoothstep(0.0, 1.4, nearest), 17.9);
        bool underLeaves = (center.b & 224) == 32;
        vec2 melt = vptSnowHeatMelt(column, textureId, withinColumn);
        float snowOpen = max(max(driftCreep, underLeaves ? 0.22 : 0.0), covered) * (1.0 - melt.x) * regrow;
        float frostOpen = max(max(0.75 * frostFringe, underLeaves ? 0.40 : 0.0), covered) * (1.0 - melt.y) * regrow;
        return vec2(snowOpen, frostOpen) * fade;
    }

    float since = laidBare && !fluidChange ? vptColumnSince(center) : -1.0;
    if (since >= 0.0) {
        vec2 edge = min(withinColumn, vec2(1.0) - withinColumn);
        float fromEdge = min(edge.x, edge.y);
        float reach = mix(0.28, 0.65, clamp(since, 0.0, 1.0));
        float creep = vptRainFray(vec2(column) + withinColumn, 1.0 - smoothstep(0.0, reach, fromEdge), 29.3);
        g_vptSnowSoak = max(g_vptSnowSoak, creep);
    }

    vec2 melt = thinTop ? vptSnowFireMelt(column, textureId, withinColumn)
                        : vptSnowHeatMelt(column, textureId, withinColumn);
    return vec2(fade * (1.0 - melt.x), fade * (1.0 - melt.y));
}

/** Snow blanket (0..1) on upward faces: settles evenly everywhere at once as snowfall continues, a dusting of
 *  single pixels that thickens into the whole blanket (a film that thickens without Pixel Rain), and thins the same
 *  way over VPT_RAIN_DRYING_TIME after the storm clears. It grew in patches a few blocks across that spread and
 *  joined up, the way puddles gather in the rain, which looked wrong for snow falling on everything alike. */
float vptSnowCover(vec3 position, float open) {
    float strength = clamp(VPT_SNOW_FROST_STRENGTH, 0.0, 2.0);
    if (strength <= 0.0 || open <= 0.0) { return 0.0; }
    float spread = vptSnowSpread(g_vptSnowSoak);
    if (spread <= 0.0) { return 0.0; }
    float cover = open * spread;
    if (VPT_RAIN_PIXELS != 0) {
        // Flakes of a few sizes settle at their own moments, spread evenly over the surface: single pixels of a
        // sixteenth of a block, and clumps of two and three pixels a side that come down sooner, so the blanket
        // fills in fewer, larger steps. Single pixels alone, the surface fizzed with tiny flakes while it filled.
        vec2 pixel = floor(vptRainWorldXZ(position) * RAIN_PIXELS_PER_BLOCK);
        float small = vptRainHash(pixel + vec2(311.0, 977.0));
        float medium = vptRainHash(floor(pixel / 2.0) + vec2(613.0, 151.0));
        float large = vptRainHash(floor((pixel + vec2(1.0, 2.0)) / 3.0) + vec2(887.0, 433.0));
        float settled = max(step(small, cover * 1.02), max(step(medium, cover * 0.75), step(large, cover * 0.5)));
        return settled * min(strength, 1.0);
    }
    return clamp(cover * clamp(strength, 0.0, 1.5), 0.0, 1.0);
}

// ---- Footprints (skyUBO.snowPrints, SnowPrints on the Java side)

// What a print is, as SnowPrints packs it (its age, plus twice this, plus sixteen times its size in pixels): pressed
// into the snow that settles on the
// ground, pressed into a snow layer's or snow block's own snow, snow and water trodden together by feet still wet
// from the water, snow carried off on the feet onto a floor, and water carried off onto one.
const int SNOW_PRINT_PRESSED = 0;
const int SNOW_PRINT_PACKED = 1;
const int SNOW_PRINT_SLUSH = 2;
const int SNOW_PRINT_CARRIED_SNOW = 3;
const int SNOW_PRINT_WET = 4;
// The snow melted round a heat source the player holds standing still (SnowPrints): size is the melted round's width
// in pixels; it fills back in from its rim as the snow falls on it.
const int SNOW_PRINT_MELT = 5;
// A step pressed into sand, mud, clay or soul sand: no snow in it, only the ground lower and in its own shade.
const int SNOW_PRINT_SOFT = 6;

/** How much of each kind of print is at a point, 0 none to 1 fresh: clear, how far the print clears the snow
 *  vptSnowCover lays; packed, a snow layer's or block's snow packed greyer; rim, the print's outer pixels, in the
 *  shade of the step; slush, snow and water trodden together; snow, snow left on a floor; wet, water left on one. */
struct VptSnowPrints {
    float clear;
    float melt;
    float packed;
    float rim;
    float slush;
    float snow;
    float wet;
    float soft;
};

/** The prints at a camera-relative point of an upward face. */
VptSnowPrints vptSnowPrints(vec3 position) {
    VptSnowPrints prints = VptSnowPrints(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    int count = clamp(int(skyUBO.snowPrintControl.x + 0.5), 0, PRISM_SNOW_PRINT_MAX);
    vec2 cameraFraction = vec2(mod(worldUBO.cameraPos.xz, dvec2(1.0)));
    for (int i = 0; i < count; i++) {
        vec4 print = skyUBO.snowPrints[i];
        vec3 d = position - print.xyz;
        if (abs(d.y) > 0.3) { continue; }
        float stamp = skyUBO.snowPrintAge[i / 4][i % 4];
        float size = floor(stamp / 16.0);
        // Out past the corners of the square (a slime's is up to a block and a half across).
        float reach = (0.5 * size + 1.0) / RAIN_PIXELS_PER_BLOCK;
        if (dot(d.xz, d.xz) > 2.0 * reach * reach) { continue; }
        stamp -= 16.0 * size;
        int kind = int(stamp * 0.5);
        float age = stamp - 2.0 * float(kind);
        float inside;
        float rim;
        // Two numbers for each pixel of each print, for which pixels go first as it fills in, melts or dries.
        vec2 cell = floor(vptRainWorldXZ(position) * RAIN_PIXELS_PER_BLOCK);
        float h = vptRainHash(cell + vec2(17.0 + float(i), 5.0));
        float h2 = vptRainHash(cell + vec2(3.0, 29.0 + float(i)));
        if (kind == SNOW_PRINT_MELT) {
            // A round melted out from the feet with a ragged pixel edge, as round a campfire, shrinking from its rim
            // as the snow fills it back in, its last pixels going at their own moments.
            float radius = size / (2.0 * RAIN_PIXELS_PER_BLOCK) * (1.0 - age);
            float amount = clamp((radius - length(d.xz)) / 0.6 + 0.35, 0.0, 1.0);
            float melt = vptRainFray(vptRainWorldXZ(position), amount, 41.3);
            melt *= VPT_RAIN_PIXELS != 0 ? step(age, 0.15 + 0.85 * h) : 1.0 - age;
            prints.melt = max(prints.melt, melt);
            continue;
        }
        if (VPT_RAIN_PIXELS != 0) {
            // The square of the leg that came down, in whole pixels on the world's grid as Minecraft would draw it:
            // four across for a player's leg, the same square whichever way the walker faced, where the foot came
            // down. Turned with the walker, the pixels of a diagonal step made a different blob each time, some
            // nearly round. Its outer pixels are the rim.
            float halfSize = 0.5 * size;
            vec2 grid = (print.xz + cameraFraction) * RAIN_PIXELS_PER_BLOCK;
            vec2 corner = mod(size, 2.0) == 0.0 ? floor(grid + 0.5) : floor(grid) + 0.5;
            vec2 offset = (position.xz + cameraFraction) * RAIN_PIXELS_PER_BLOCK - corner;
            vec2 local = abs(offset);
            inside = step(local.x, halfSize) * step(local.y, halfSize);
            rim = inside * step(halfSize - 1.0, max(local.x, local.y));
        } else {
            // The leg's square across and along the way the walker faced, its corners rounded.
            vec2 across = vec2(cos(print.w), sin(print.w));
            vec2 local = abs(vec2(dot(d.xz, across), dot(d.xz, vec2(-across.y, across.x)))) * RAIN_PIXELS_PER_BLOCK;
            float e = length(max(local - vec2(0.3 * size), vec2(0.0))) / (0.2 * size + 0.5);
            inside = 1.0 - smoothstep(0.8, 1.05, e);
            rim = inside * smoothstep(0.35, 0.85, max(local.x, local.y) / (0.5 * size));
        }
        if (inside <= 0.0) { continue; }
        bool pixels = VPT_RAIN_PIXELS != 0;
        if (kind == SNOW_PRINT_SOFT) {
            // Pressed into soft ground: deepest in the middle, half as deep at the rim, settling back a pixel at a
            // time from the rim in.
            float depth = inside * (pixels ? step(age, (rim > 0.0 ? 0.55 : 1.0) * (0.15 + 0.85 * h))
                                           : 1.0 - smoothstep(0.0, 1.0, age + 0.3 * rim));
            prints.soft = max(prints.soft, depth * (rim > 0.0 ? 0.5 : 1.0));
            continue;
        }
        if (kind <= SNOW_PRINT_SLUSH) {
            // Pressed into snow, filled back in by the snow falling on it a pixel at a time, from the rim in.
            float depth = inside * (pixels ? step(age, (rim > 0.0 ? 0.6 : 1.0) * (0.12 + 0.88 * h))
                                           : 1.0 - smoothstep(0.0, 1.0, age + 0.3 * rim));
            if (kind == SNOW_PRINT_SLUSH) {
                // Wetter in some pixels than others: the water stands in the print in two tones.
                prints.slush = max(prints.slush, depth * (pixels ? 0.7 + 0.3 * step(0.5, h2) : 0.85));
            } else {
                prints.clear = max(prints.clear, depth);
                if (kind == SNOW_PRINT_PACKED) { prints.packed = max(prints.packed, depth); }
            }
            prints.rim = max(prints.rim, rim * depth);
        } else if (kind == SNOW_PRINT_CARRIED_SNOW) {
            // Clumps of snow off the boots, fewer as they melt, each leaving a little water where it was that dries
            // in turn.
            float snow = pixels ? step(h, 0.85 * pow(1.0 - age, 0.7)) : 1.0 - smoothstep(0.25, 0.75, age);
            float water = pixels ? (1.0 - snow) * step(h, 0.8) * step(age, 0.75 + 0.25 * h2)
                                 : smoothstep(0.15, 0.55, age) * (1.0 - smoothstep(0.7, 1.0, age));
            prints.snow = max(prints.snow, inside * snow);
            prints.wet = max(prints.wet, inside * water);
        } else {
            // Water off wet feet, drying a pixel at a time.
            float water = pixels ? step(h, 0.92) * step(age, 0.2 + 0.8 * h2) : 1.0 - smoothstep(0.4, 1.0, age);
            prints.wet = max(prints.wet, inside * water * 0.9);
        }
    }
    return prints;
}

// How much darker the middle of a print in sand or mud is than the ground round it.
const float SOFT_PRINT_SHADE = 0.34;

/** Lays the prints on a material, after the snow (vptApplySnowFrost): the pressed prints have cleared its blanket
 *  already. dryAlbedo is the material's own colour, before rain and snow. */
void vptApplySnowPrints(inout LabPBRMat material, VptSnowPrints prints, vec3 dryAlbedo) {
    if (material.transmission > 0.0) { return; }
    // A step in sand or mud: the ground pressed down lies in its own shade, a darker square of the same colour.
    if (prints.soft > 0.0) { material.albedo *= 1.0 - SOFT_PRINT_SHADE * prints.soft; }
    // A snow layer's or snow block's own snow, packed down: a little greyer and bluer, smoother, its rim in the shade
    // of the step. Only on white tops: the print may reach the next block.
    if (prints.packed > 0.0 && dot(dryAlbedo, vec3(0.2126, 0.7152, 0.0722)) > 0.45) {
        material.albedo *= mix(vec3(1.0), vec3(0.76, 0.81, 0.91), prints.packed) * (1.0 - 0.14 * prints.rim);
        material.roughness = mix(material.roughness, max(material.roughness * 0.75, 0.3), prints.packed);
    }
    // Slush: grey-blue, glossy with the water in it, darker at the rim.
    if (prints.slush > 0.0) {
        material.albedo *= mix(vec3(1.0), vec3(0.47, 0.54, 0.66), prints.slush) * (1.0 - 0.10 * prints.rim);
        material.roughness = mix(material.roughness, 0.22, prints.slush * 0.85);
        material.f0 = mix(material.f0, max(material.f0, vec3(0.02)), prints.slush);
    }
    if (prints.snow > 0.0) {
        const vec3 snowAlbedo = vec3(0.90, 0.93, 0.97);
        material.albedo = mix(material.albedo, snowAlbedo, prints.snow * 0.9);
        material.metallic *= 1.0 - prints.snow;
        material.roughness = mix(material.roughness, 0.7, prints.snow);
        material.f0 = mix(material.f0, vec3(0.028), prints.snow);
    }
    vptApplyGroundWetness(material, prints.wet, false);
}

void vptApplySnowFrost(inout LabPBRMat material, vec3 worldPos, float frost, float snowCover, bool upward) {
    if ((frost <= 0.0 && snowCover <= 0.0) || material.transmission > 0.0) { return; }
    float selfCool = 1.0 - smoothstep(0.02, 0.15, material.emission);
    frost *= selfCool;
    snowCover *= selfCool;
    if (frost <= 0.0 && snowCover <= 0.0) { return; }

    const vec3 frostTint = vec3(0.86, 0.91, 0.97);
    const vec3 snowAlbedo = vec3(0.90, 0.93, 0.97);

    float rime = clamp(frost * (upward ? 0.38 : 0.28), 0.0, 0.65);
    material.albedo = mix(material.albedo, max(material.albedo, frostTint), rime);
    material.roughness = mix(material.roughness, 0.44, rime * 0.7);
    material.f0 = mix(material.f0, max(material.f0, vec3(0.03)), rime);

    if (snowCover > 0.0) {
        vec2 crystalCell = floor(vptRainWorldXZ(worldPos) * RAIN_PIXELS_PER_BLOCK);
        float glint = step(0.94, vptRainHash(crystalCell + vec2(41.0, 73.0)));
        float coat = clamp(snowCover * 0.92, 0.0, 0.96);
        material.albedo = mix(material.albedo, snowAlbedo, coat);
        material.metallic *= (1.0 - coat);
        float snowRough = mix(0.72, 0.18, glint * snowCover);
        material.roughness = mix(material.roughness, snowRough, coat);
        material.f0 = mix(material.f0, vec3(mix(0.028, 0.048, glint)), coat);
    }
}

#endif
