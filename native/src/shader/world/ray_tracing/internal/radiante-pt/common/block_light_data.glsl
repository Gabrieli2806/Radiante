#ifndef VPT_BLOCK_LIGHT_DATA_GLSL
#define VPT_BLOCK_LIGHT_DATA_GLSL

// The block light list (see block_light.glsl): one entry per emissive triangle, grouped by chunk section. Shared with
// the froxel fog, which lights the air with the same lights.

struct VptChunkLightData {
    int x;
    int y;
    int z;
    uint geometryCount;
    uint lightCount;
    // Which of the 27 sections around this one hold lights (bit (dz + 1) * 9 + (dy + 1) * 3 + (dx + 1)), and
    // VPT_LIGHT_NEIGHBOURS_KNOWN once the renderer has worked that out (Chunks::updateLightNeighbours).
    uint lightNeighbours;
    uint64_t lightBufferAddress;
};
const uint VPT_LIGHT_NEIGHBOURS_KNOWN = 1u << 31;

struct VptPackedLight {
    vec4 p0Area;
    vec4 p1;
    vec4 p2;
    vec4 p3;
    vec4 normal;
    // rgb; w how it is scaled: 0 a light level's scale, 1 exactly the glow its surface shows (a Bedrock pack's MER
    // emission: see vptCastLight), 2 one of Bedrock RTX's point lights (below).
    vec4 radiance;
    vec4 sourceIDData;
};

// The colour of the lights through a pack's hooks/block_light.glsl (nothing in radiante-pt).
#include "common/block_light_hook.glsl"

// One of the lights Bedrock RTX adds at a few small light blocks - torches, lanterns, end rods (radiance.w 2): a
// sphere at p0Area.xyz of radius normal.x whose radiance.rgb is its strength, falling off with the square of the
// distance to its centre, without a direction of its own. The point a surface aims its shadow ray at is on the half
// of the sphere facing it, as Bedrock aims it.
bool vptIsPointLight(VptPackedLight light) {
    return light.radiance.w > 1.5;
}

vec3 vptPointLightSample(VptPackedLight light, vec2 xi, vec3 scenePos) {
    vec3 centre = light.p0Area.xyz - vec3(worldUBO.cameraPos.xyz);
    float z = 1.0 - 2.0 * xi.x;
    float r = sqrt(max(1.0 - z * z, 0.0));
    float phi = 2.0 * PI * xi.y;
    vec3 offset = vec3(r * cos(phi), r * sin(phi), z) * light.normal.x;
    if (dot(offset, centre - scenePos) > 0.0) { offset = -offset; }
    return centre + offset;
}

// How a small flame's light falls off with the distance squared d2. Not the pure inverse square of an ideal point:
// that makes the wall beside a torch burn white and the room a few blocks off nearly dark. This is flat close to the
// flame and falls a little more slowly than the square far from it (1.024 / (2 + d^1.8), the same strength as the
// square at three blocks), so the light is softer near and reaches further. An earlier, flatter curve
// (1.09 / (4 + d^1.6)) lit a whole wall almost evenly: the torch glowed, but the light did not seem to come from it.
// With this one the wall has a pool of light around the flame. Shared by the placed point lights and the held one,
// so a torch lights alike in the hand and set down.
float vptFlameFalloff(float d2) {
    return 1.024 / (2.0 + exp2(0.9 * log2(max(d2, 1e-4))));
}

// How a point light's strength falls off at scenePos.
float vptPointLightFalloff(VptPackedLight light, vec3 scenePos) {
    vec3 toCentre = light.p0Area.xyz - vec3(worldUBO.cameraPos.xyz) - scenePos;
    return vptFlameFalloff(dot(toCentre, toCentre));
}

// The exposure the picture is shown at, as Radiante PT's meter tells it (exposure.comp), or a negative value where
// that meter is not the one in use.
float vptMeteredExposure() {
    if (lightMeter.bedrockMeter.w != 1.0) { return -1.0; }
    // The Nether's eye may close further (exposure.comp, which marks it with a negative daylight.w).
    float minEV = lightMeter.daylight.w < 0.0 ? -4.0 : -1.0;
    return exp2(clamp(lightMeter.bedrockMeter.x, minEV, 6.0) + log2(clamp(lightMeter.daylight.y, 0.05, 1.0)));
}

// How much more a lamp's light counts while the eye is closed down for a brighter place. A torch's light is a
// fixed amount, and the eye by day is closed some hundred times further than in a cave lit by that torch: by the
// numbers alone a torch, a campfire or a lantern lit nothing at all by day, not even the shaded ground at its
// foot, and looked switched off. The eye does not work by one number: next to a flame, in the shade, it still
// tells the warm light apart. So the light is lifted by the square root of how far the eye is closed past
// VPT_LIFT_EXPOSURE (the exposure of a room lit by lamps, where nothing is lifted), VPT_LIFT_MAX times at most:
// under the sun it still drowns, in the shade it is a faint warm pool, and by night nothing changes.
// Only as far as it is day and the camera stands open to the sky: the eye closes for a sea of lava too, and lifted
// there, in the Nether or in a cave, lava's own light was multiplied by five and burnt every block around it.
#ifndef VPT_LIFT_EXPOSURE
#    define VPT_LIFT_EXPOSURE 12.0
#endif
#ifndef VPT_LIFT_MAX
#    define VPT_LIFT_MAX 5.0
#endif
// Worked out once for each hit: every light a surface considers asks for it.
float g_vptDayLift = -1.0;
float vptDayLift() {
    if (g_vptDayLift < 0.0) {
        float exposure = vptMeteredExposure();
        float daylight = clamp(lightMeter.daylight.x, 0.0, 1.0) * clamp(skyUBO.biomeFogHeights.z, 0.0, 1.0);
        g_vptDayLift = exposure <= 0.0 || worldUBO.skyType != 1 ? 1.0 :
                       mix(1.0, clamp(sqrt(VPT_LIFT_EXPOSURE / exposure), 1.0, VPT_LIFT_MAX), daylight);
    }
    return g_vptDayLift;
}

// The least a glowing texel shows of its colour, as a share of white on the screen, for a texel that glows in
// full (emission 1), and in proportion to the root of its emission below that (a Bedrock pack's emission is the
// square of what its map holds). A flame is about as bright as a white wall in the sun: by day it is still a
// flame. Without it the eye, closed for the sun, showed a torch, a campfire or a lantern by the daylight on its
// texture alone, as a thing with no light in it. In the dark the glow is far over this and nothing changes.
#ifndef VPT_GLOW_DISPLAY
#    define VPT_GLOW_DISPLAY 0.9
#endif
float vptSeenGlowFloor(float emission) {
    float exposure = vptMeteredExposure();
    if (exposure <= 0.0 || emission <= 0.0) { return 0.0; }
    return VPT_GLOW_DISPLAY * sqrt(min(emission, 1.0)) /
           (exposure * VPT_BEDROCK_SEEN_EMISSION * max(worldUBO.emissionBrightness, 1e-3));
}

// The light of an exact light (radiance.w 1) on the scale of the others, which the shaders multiply by
// VPT_INDIRECT_LIGHT_STRENGTH and the Emission Brightness setting: what its surface shows, which is its radiance
// times VPT_BEDROCK_SEEN_EMISSION and the same setting (default.rchit).
float vptExactLightScale() {
#ifdef VPT_INDIRECT_LIGHT_STRENGTH
    return VPT_BEDROCK_SEEN_EMISSION / max(VPT_INDIRECT_LIGHT_STRENGTH, 1e-3);
#else
    return VPT_BEDROCK_SEEN_EMISSION / 16.0;
#endif
}

// A point light's strength (radiance.w 2) on that scale: Bedrock's own, taken as it is at the Emission Brightness
// setting's default (12, which reaches the shaders as 0.12).
float vptPointLightScale() {
#ifdef VPT_INDIRECT_LIGHT_STRENGTH
    return (100.0 / 12.0) / max(VPT_INDIRECT_LIGHT_STRENGTH, 1e-3);
#else
    return (100.0 / 12.0) / 16.0;
#endif
}

layout(std430, set = 1, binding = 9) readonly buffer VptChunkPackedDataBuffer {
    VptChunkLightData vptChunkLights[];
};

layout(std430, buffer_reference, buffer_reference_align = 16) readonly buffer VptPackedLightBuffer {
    VptPackedLight lights[];
};

ivec3 vptSectionOf(vec3 scenePos) {
    return ivec3(floor((scenePos + vec3(worldUBO.cameraPos.xyz)) / 16.0));
}

// In integers: through a float division, a section a whole number of grids from zero (x 33 of a 33 wide grid, render
// distance 16) came out a hair under that number on the GPU, its slot a whole grid too far - in the next row, holding
// another section - and every light in that section's column was lost. GLSL leaves % of a negative number undefined,
// so a negative value is folded onto a positive one first.
int vptFloorMod(int value, int divisor) {
    int quotient = value >= 0 ? value / divisor : -((-value - 1) / divisor) - 1;
    return value - divisor * quotient;
}

/** The slot the grid gives a section, or -1 when it is outside the grid. */
int vptSectionSlot(ivec3 section) {
    int sizeX = worldUBO.chunkGridInfo.x;
    int sizeY = worldUBO.chunkGridInfo.y;
    int sizeZ = worldUBO.chunkGridInfo.z;
    int bottom = worldUBO.chunkGridInfo.w;
    if (sizeX <= 0 || sizeY <= 0 || sizeZ <= 0) { return -1; }
    if (section.y < bottom || section.y >= bottom + sizeY) { return -1; }
    return (vptFloorMod(section.z, sizeZ) * sizeY + (section.y - bottom)) * sizeX + vptFloorMod(section.x, sizeX);
}

/** The loaded section's slot in the light list, or -1 when it is outside the grid or holds no lights. */
int vptSectionLightSlot(ivec3 section) {
    int slot = vptSectionSlot(section);
    if (slot < 0) { return -1; }
    VptChunkLightData data = vptChunkLights[slot];
    // A slot is reused as the grid scrolls; one still holding another section is no use here.
    if (data.x != section.x * 16 || data.y != section.y * 16 || data.z != section.z * 16) { return -1; }
    if (data.lightCount == 0u || data.lightBufferAddress == 0ul) { return -1; }
    return slot;
}

// Lights in emissive entities (a dropped glowstone, an item frame holding a sea lantern) are one more slot next to
// the sections': the list the renderer rebuilds every frame with the ones near the camera (WorldUBO.entityLight*).
const int VPT_ENTITY_LIGHT_SLOT = 1 << 30;

// Whether slot names a list at all: the entities' or one of the grid's. A slot read back from a reservoir kept over
// frames (block_light.glsl) is checked first: an image is not cleared when the pipeline is built again, and a slot
// out of the grid read the light list's address from past its end - a fault that took the whole device down.
bool vptValidLightSlot(int slot) {
    if (slot == VPT_ENTITY_LIGHT_SLOT) { return true; }
    ivec4 grid = worldUBO.chunkGridInfo;
    return slot >= 0 && grid.x > 0 && grid.y > 0 && grid.z > 0 && slot < grid.x * grid.y * grid.z;
}

uint vptSlotLightCount(int slot) {
    return slot == VPT_ENTITY_LIGHT_SLOT ? worldUBO.entityLightCount : vptChunkLights[slot].lightCount;
}

uint64_t vptSlotLightAddress(int slot) {
    return slot == VPT_ENTITY_LIGHT_SLOT ?
               packUint2x32(uvec2(worldUBO.entityLightAddressLo, worldUBO.entityLightAddressHi)) :
               vptChunkLights[slot].lightBufferAddress;
}

bool vptHasEntityLights() {
    return worldUBO.entityLightCount > 0u && (worldUBO.entityLightAddressLo | worldUBO.entityLightAddressHi) != 0u;
}

/**
 * The mask of the sections around section that hold lights (VptChunkLightData.lightNeighbours, without the known
 * bit), as the renderer worked it out for that section; false when the grid's slot for it holds another section or
 * none was worked out yet, and the sections have to be looked at one by one.
 */
bool vptLightNeighbours(ivec3 section, out uint mask) {
    mask = 0u;
    int slot = vptSectionSlot(section);
    if (slot < 0) { return false; }
    VptChunkLightData data = vptChunkLights[slot];
    if (data.x != section.x * 16 || data.y != section.y * 16 || data.z != section.z * 16) { return false; }
    if ((data.lightNeighbours & VPT_LIGHT_NEIGHBOURS_KNOWN) == 0u) { return false; }
    mask = data.lightNeighbours & ~VPT_LIGHT_NEIGHBOURS_KNOWN;
    return true;
}

// The slots of the sections a mask of vptLightNeighbours names around center, appended to slots in the order the
// section by section search finds them (z, then y, then x, each from -1 to 1): the same list, from one read of the
// table instead of one per section. A macro, not a function, for the reason VPT_PICK_LIGHT is one: no copy of the
// slot array.
#define VPT_APPEND_NEIGHBOUR_SLOTS(center, mask, slots, slotCount)                                                    \
    {                                                                                                                 \
        uint vptRemaining = (mask);                                                                                   \
        while (vptRemaining != 0u) {                                                                                  \
            int vptBit = findLSB(vptRemaining);                                                                       \
            vptRemaining &= vptRemaining - 1u;                                                                        \
            slots[slotCount++] = vptSectionSlot((center) + ivec3(vptBit % 3, (vptBit / 3) % 3, vptBit / 9) - 1);       \
        }                                                                                                             \
    }

/**
 * Of the 27 sections around center, the bits of the eight in the 2x2x2 block that starts at base (the block around
 * a point's closest section corner): on each axis the pair of offsets base starts.
 */
uint vptNearNeighbourBits(ivec3 base, ivec3 center) {
    ivec3 low = base - center + 1; // 0 or 1: the first offset of the pair on each axis, counted from -1
    uint xBits = 3u << uint(low.x);
    uint bits = 0u;
    for (int dz = 0; dz < 2; dz++) {
        for (int dy = 0; dy < 2; dy++) {
            bits |= xBits << uint((low.z + dz) * 9 + (low.y + dy) * 3);
        }
    }
    return bits;
}

/** The light list slots a surface at scenePos picks from: the 27 sections around it, then the entities'. */
int vptGatherLightSlots(vec3 scenePos, out int slots[28]) {
    int slotCount = 0;
    ivec3 center = vptSectionOf(scenePos);
    uint mask;
    if (vptLightNeighbours(center, mask)) {
        VPT_APPEND_NEIGHBOUR_SLOTS(center, mask, slots, slotCount)
        if (vptHasEntityLights()) { slots[slotCount++] = VPT_ENTITY_LIGHT_SLOT; }
        return slotCount;
    }
    for (int dz = -1; dz <= 1; dz++) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int slot = vptSectionLightSlot(center + ivec3(dx, dy, dz));
                if (slot >= 0) { slots[slotCount++] = slot; }
            }
        }
    }
    if (vptHasEntityLights()) { slots[slotCount++] = VPT_ENTITY_LIGHT_SLOT; }
    return slotCount;
}

/** The eight sections nearest scenePos (the 2x2x2 block around its closest section corner), then the entities'. */
int vptGatherNearLightSlots(vec3 scenePos, out int slots[28]) {
    int slotCount = 0;
    ivec3 base = ivec3(floor((scenePos + vec3(worldUBO.cameraPos.xyz) - 8.0) / 16.0));
    ivec3 center = vptSectionOf(scenePos);
    uint mask;
    if (vptLightNeighbours(center, mask)) {
        VPT_APPEND_NEIGHBOUR_SLOTS(center, mask & vptNearNeighbourBits(base, center), slots, slotCount)
        if (vptHasEntityLights()) { slots[slotCount++] = VPT_ENTITY_LIGHT_SLOT; }
        return slotCount;
    }
    for (int corner = 0; corner < 8; corner++) {
        int slot = vptSectionLightSlot(base + ivec3(corner & 1, (corner >> 1) & 1, corner >> 2));
        if (slot >= 0) { slots[slotCount++] = slot; }
    }
    if (vptHasEntityLights()) { slots[slotCount++] = VPT_ENTITY_LIGHT_SLOT; }
    return slotCount;
}

// One candidate light from the gathered slots: a slot at random, then a light in it. choices is how many lights it
// was drawn among, so a point on it has the probability 1 / (choices * area). A macro, not a function: a function
// copies the slot array in on every candidate, and that costs more than the pick itself.
#define VPT_PICK_LIGHT(slots, slotCount, seed, slot, index, choices, picked)                                         \
    {                                                                                                                 \
        slot = slots[min(int(rand(seed) * float(slotCount)), slotCount - 1)];                                         \
        uint pickSlotLights = vptSlotLightCount(slot);                                                                \
        index = min(uint(rand(seed) * float(pickSlotLights)), pickSlotLights - 1u);                                    \
        choices = float(slotCount) * float(pickSlotLights);                                                           \
        picked = pickSlotLights > 0u;                                                                                 \
    }
/** Whether a light hit at hitPos lies in the eight sections vptGatherNearLightSlots picks for originPos. */
bool vptInNearBlockLightReach(vec3 originPos, vec3 hitPos) {
    ivec3 base = ivec3(floor((originPos + vec3(worldUBO.cameraPos.xyz) - 8.0) / 16.0));
    ivec3 d = vptSectionOf(hitPos) - base;
    return all(greaterThanEqual(d, ivec3(0))) && all(lessThanEqual(d, ivec3(1)));
}

/** Whether a light hit at hitPos from a surface at originPos lies in the sections that surface samples. */
bool vptInBlockLightReach(vec3 originPos, vec3 hitPos) {
    ivec3 d = abs(vptSectionOf(hitPos) - vptSectionOf(originPos));
    return max(d.x, max(d.y, d.z)) <= 1;
}

vec3 vptSampleTrianglePoint(vec3 a, vec3 b, vec3 c, vec2 xi) {
    float s = sqrt(xi.x);
    return a * (1.0 - s) + b * (s * (1.0 - xi.y)) + c * (s * xi.y);
}

#endif
