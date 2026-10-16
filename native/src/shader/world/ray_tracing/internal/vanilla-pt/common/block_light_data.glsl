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
    uint64_t lightBufferAddress;
};

struct VptPackedLight {
    vec4 p0Area;
    vec4 p1;
    vec4 p2;
    vec4 p3;
    vec4 normal;
    // rgb; w how it is scaled: 0 a light level's scale, 1 exactly the glow its surface shows (a Bedrock pack's MER
    // emission, under the experimental profile: see vptCastLight), 2 one of Bedrock RTX's point lights (below).
    vec4 radiance;
    vec4 sourceIDData;
};

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

// How a point light's strength falls off at scenePos: one over the square of the distance to its centre.
float vptPointLightFalloff(VptPackedLight light, vec3 scenePos) {
    vec3 toCentre = light.p0Area.xyz - vec3(worldUBO.cameraPos.xyz) - scenePos;
    return 1.0 / max(dot(toCentre, toCentre), 1e-4);
}

// The light of an exact light (radiance.w 1) on the scale of the others, which the shaders multiply by
// VPT_INDIRECT_LIGHT_STRENGTH and the Emission Brightness setting: what its surface shows, which is its radiance
// times VPT_BEDROCK_SEEN_EMISSION and the same setting (pt/pt_shade_default.glsl).
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

int vptFloorMod(int value, int divisor) {
    return value - divisor * int(floor(float(value) / float(divisor)));
}

/** The loaded section's slot in the light list, or -1 when it is outside the grid or holds no lights. */
int vptSectionLightSlot(ivec3 section) {
    int sizeX = worldUBO.chunkGridInfo.x;
    int sizeY = worldUBO.chunkGridInfo.y;
    int sizeZ = worldUBO.chunkGridInfo.z;
    int bottom = worldUBO.chunkGridInfo.w;
    if (sizeX <= 0 || sizeY <= 0 || sizeZ <= 0) { return -1; }
    if (section.y < bottom || section.y >= bottom + sizeY) { return -1; }
    int slot = (vptFloorMod(section.z, sizeZ) * sizeY + (section.y - bottom)) * sizeX + vptFloorMod(section.x, sizeX);
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

/** The light list slots a surface at scenePos picks from: the 27 sections around it, then the entities'. */
int vptGatherLightSlots(vec3 scenePos, out int slots[28]) {
    int slotCount = 0;
    ivec3 center = vptSectionOf(scenePos);
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
