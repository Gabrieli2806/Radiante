#ifndef VPT_LIGHT_GRID_GLSL
#define VPT_LIGHT_GRID_GLSL

// Adaptive block light sampling (NEE-AT) in a scrolling world-space 3D cell grid.
//
// Uniformly picking among every emissive triangle in the 27 chunk sections around a surface wastes most shadow rays on
// lights hidden behind walls, floors or pillars when a building or cave has dozens of lamps. Each two-block world cell
// keeps up to seven lights that actually delivered shadowed light there, updated after the world pass from a subsampled
// feedback table. Sampling draws from a defensive mixture (75% learned cell weights, 25% uniform over all reachable
// lights) and divides by the exact combined discrete probability, so every reachable light keeps a positive PDF and the
// converged image is unchanged.

#ifndef VPT_ADAPTIVE_LIGHT_GRID
#    define VPT_ADAPTIVE_LIGHT_GRID 1
#endif

#ifndef RADIANTE_BUFFER_LIGHT_GRID_RESOLVED
#    define RADIANTE_BUFFER_LIGHT_GRID_RESOLVED 51
#endif
#ifndef RADIANTE_BUFFER_LIGHT_GRID_FEEDBACK
#    define RADIANTE_BUFFER_LIGHT_GRID_FEEDBACK 52
#endif

// Power-of-two cell counts (128 x 64 x 128 blocks around the camera at two blocks per cell). Shifting the lookup point
// by 0.6 blocks along the geometric normal separates the two faces of any one-block wall, floor or ceiling by 2.2
// blocks, which never land in the same two-block cell.
const ivec3 VPT_LIGHT_GRID_EXTENT = ivec3(64, 32, 64);
const uint VPT_LIGHT_GRID_CELL_COUNT = 64u * 32u * 64u;
const int VPT_LIGHT_GRID_ENTRIES = 7;
const uint VPT_LIGHT_GRID_WORDS_PER_CELL = 8u; // 1 header uvec4 + 7 entry uvec4s
const int VPT_LIGHT_GRID_BUCKETS = 4;
const float VPT_LIGHT_GRID_FIXED_SCALE = 262144.0;
const uint VPT_LIGHT_GRID_MAX_BUCKET_SAMPLES = 512u;
const float VPT_LIGHT_GRID_MIX_ALPHA = 0.75;

struct VptLightGridBucket {
    uint tag;
    int slot;
    uint index;
    uint id;
    uint count;
    uint sumFixed;
    uint pad0;
    uint pad1;
};

#ifdef VPT_LIGHT_GRID_RESOLVE_PASS
layout(std430, set = 5, binding = RADIANTE_BUFFER_LIGHT_GRID_RESOLVED) buffer VptLightGridResolvedBuffer {
    uvec4 words[];
} vptLightGridResolved;
#else
layout(std430, set = 5, binding = RADIANTE_BUFFER_LIGHT_GRID_RESOLVED) readonly buffer VptLightGridResolvedBuffer {
    uvec4 words[];
} vptLightGridResolved;
#endif

layout(std430, set = 5, binding = RADIANTE_BUFFER_LIGHT_GRID_FEEDBACK) coherent buffer VptLightGridFeedbackBuffer {
    VptLightGridBucket buckets[];
} vptLightGridFeedback;

// Two-block world cell of the camera, splitting the integer block in double precision once to avoid per-pixel fp64 math.
ivec3 vptLightGridCameraCell(dvec3 cameraPos) {
    return ivec3(floor(cameraPos)) >> 1;
}

ivec3 vptLightGridWorldCell(vec3 scenePos, vec3 geometricNormal) {
    dvec3 blockFloor = floor(worldUBO.cameraPos.xyz);
    ivec3 cameraBlock = ivec3(blockFloor);
    vec3 offset = vec3(worldUBO.cameraPos.xyz - blockFloor) + scenePos + geometricNormal * 0.6;
    return (cameraBlock + ivec3(floor(offset))) >> 1;
}

int vptLightGridIndex(ivec3 worldCell, ivec3 cameraCell) {
    ivec3 rel = worldCell - cameraCell;
    if (any(lessThan(rel, -VPT_LIGHT_GRID_EXTENT / 2)) || any(greaterThanEqual(rel, VPT_LIGHT_GRID_EXTENT / 2))) {
        return -1;
    }
    ivec3 texel = worldCell & (VPT_LIGHT_GRID_EXTENT - 1);
    return (texel.z * VPT_LIGHT_GRID_EXTENT.y + texel.y) * VPT_LIGHT_GRID_EXTENT.x + texel.x;
}

ivec3 vptLightGridCellOfIndex(uint cellIdx, ivec3 cameraCell) {
    ivec3 texel = ivec3(int(cellIdx & 63u), int((cellIdx >> 6u) & 31u), int(cellIdx >> 11u));
    ivec3 low = cameraCell - VPT_LIGHT_GRID_EXTENT / 2;
    return low + ((texel - low) & (VPT_LIGHT_GRID_EXTENT - 1));
}

uint vptLightGridTag(int slot, uint index, uint id) {
    uint h = uint(slot) * 0x9E3779B1u ^ (index * 0x85EBCA77u) ^ (id * 0xC2B2AE3Du);
    h ^= h >> 16u;
    h *= 0x7FEB352Du;
    h ^= h >> 15u;
    if (h == 0u || h == 0xFFFFFFFFu) { h = 1u; }
    return h;
}

bool vptLightGridValidEntry(int slot, uint index, uint id) {
    if (!vptValidLightSlot(slot)) { return false; }
    if (slot == VPT_ENTITY_LIGHT_SLOT && !vptHasEntityLights()) { return false; }
    uint64_t address = vptSlotLightAddress(slot);
    if (address == 0ul || index >= vptSlotLightCount(slot)) { return false; }
    VptPackedLight light = VptPackedLightBuffer(address).lights[index];
    return light.p0Area.w > 1e-8 && floatBitsToUint(light.sourceIDData.x) == id;
}

#ifndef VPT_LIGHT_GRID_RESOLVE_PASS

struct VptLightGridCell {
    int cellIndex;
    int count;
    float mixAlpha;
    int slot[VPT_LIGHT_GRID_ENTRIES];
    uint index[VPT_LIGHT_GRID_ENTRIES];
    float prob[VPT_LIGHT_GRID_ENTRIES];
};

VptLightGridCell vptLoadLightGridCell(vec3 scenePos, vec3 geometricNormal) {
    VptLightGridCell cell;
    cell.cellIndex = -1;
    cell.count = 0;
    cell.mixAlpha = 0.0;
    if (int(VPT_ADAPTIVE_LIGHT_GRID) == 0 || worldUBO.blockLightSampling == 0u) { return cell; }

    ivec3 cameraCell = vptLightGridCameraCell(worldUBO.cameraPos.xyz);
    ivec3 worldCell = vptLightGridWorldCell(scenePos, geometricNormal);
    int cellIdx = vptLightGridIndex(worldCell, cameraCell);
    if (cellIdx < 0) { return cell; }
    cell.cellIndex = cellIdx;

    uint base = uint(cellIdx) * VPT_LIGHT_GRID_WORDS_PER_CELL;
    uvec4 header = vptLightGridResolved.words[base];
    float totalWeight = uintBitsToFloat(header.w);
    if (ivec3(header.xyz) != worldCell || !(totalWeight > 0.0) || isnan(totalWeight) || isinf(totalWeight)) {
        return cell;
    }

    for (int k = 0; k < VPT_LIGHT_GRID_ENTRIES; k++) {
        uvec4 entry = vptLightGridResolved.words[base + 1u + uint(k)];
        float w = uintBitsToFloat(entry.w);
        if (!(w > 0.0) || isnan(w) || isinf(w)) { break; }
        cell.slot[cell.count] = int(entry.x);
        cell.index[cell.count] = entry.y;
        cell.prob[cell.count] = w;
        cell.count++;
    }
    return cell;
}

float vptLightGridProb(VptLightGridCell cell, int slot, uint index) {
    for (int k = 0; k < cell.count; k++) {
        if (cell.slot[k] == slot && cell.index[k] == index) { return cell.prob[k]; }
    }
    return 0.0;
}

// Filters a cell's learned lights against the exact section slots reachable from this surface, renormalizing their
// probabilities so every grid draw lands in the reachable domain and sums to 1. Kept as a macro so the 28-int slot
// array is not copied into a function.
#define VPT_FILTER_LIGHT_GRID_CELL(gridCellArg, slotsArg, slotCountArg)                                               \
    if ((gridCellArg).count > 0) {                                                                                    \
        float vptGridSum = 0.0;                                                                                       \
        for (int vptK = 0; vptK < (gridCellArg).count; vptK++) {                                                      \
            int vptCandSlot = (gridCellArg).slot[vptK];                                                               \
            bool vptReachable = false;                                                                                \
            for (int vptS = 0; vptS < (slotCountArg); vptS++) {                                                       \
                if ((slotsArg)[vptS] == vptCandSlot) {                                                                \
                    vptReachable = true;                                                                              \
                    break;                                                                                            \
                }                                                                                                     \
            }                                                                                                         \
            if (vptReachable && (gridCellArg).index[vptK] < vptSlotLightCount(vptCandSlot)) {                         \
                vptGridSum += (gridCellArg).prob[vptK];                                                               \
            } else {                                                                                                  \
                (gridCellArg).prob[vptK] = 0.0;                                                                       \
            }                                                                                                         \
        }                                                                                                             \
        if (vptGridSum > 1e-12) {                                                                                     \
            float vptInvSum = 1.0 / vptGridSum;                                                                       \
            for (int vptK = 0; vptK < (gridCellArg).count; vptK++) { (gridCellArg).prob[vptK] *= vptInvSum; }         \
            (gridCellArg).mixAlpha = VPT_LIGHT_GRID_MIX_ALPHA;                                                        \
        } else {                                                                                                      \
            (gridCellArg).count = 0;                                                                                  \
            (gridCellArg).mixAlpha = 0.0;                                                                             \
        }                                                                                                             \
    }

// Draws one light from the defensive mixture (mixAlpha * p_grid + (1 - mixAlpha) * p_uniform) and sets outPDiscrete to
// the exact combined probability of choosing (outSlot, outIndex) across both branches.
#define VPT_PICK_ADAPTIVE_LIGHT(gridCellArg, slotsArg, slotCountArg, seedArg, outSlot, outIndex, outPDiscrete, outPicked) \
    if ((gridCellArg).mixAlpha > 0.0 && rand(seedArg) < (gridCellArg).mixAlpha) {                                     \
        float vptU = rand(seedArg);                                                                                   \
        float vptCdf = 0.0;                                                                                           \
        int vptPickedK = 0;                                                                                           \
        for (int vptK = 0; vptK < (gridCellArg).count; vptK++) {                                                      \
            if ((gridCellArg).prob[vptK] <= 0.0) { continue; }                                                        \
            vptPickedK = vptK;                                                                                        \
            vptCdf += (gridCellArg).prob[vptK];                                                                       \
            if (vptU <= vptCdf) { break; }                                                                            \
        }                                                                                                             \
        outSlot = (gridCellArg).slot[vptPickedK];                                                                     \
        outIndex = (gridCellArg).index[vptPickedK];                                                                   \
        float vptChoices = float(slotCountArg) * float(vptSlotLightCount(outSlot));                                   \
        outPicked = vptChoices > 0.0;                                                                                 \
        outPDiscrete = outPicked ?                                                                                    \
                           (1.0 - (gridCellArg).mixAlpha) / vptChoices + (gridCellArg).mixAlpha * (gridCellArg).prob[vptPickedK] : \
                           0.0;                                                                                       \
    } else {                                                                                                          \
        float vptChoices = 0.0;                                                                                       \
        VPT_PICK_LIGHT(slotsArg, slotCountArg, seedArg, outSlot, outIndex, vptChoices, outPicked)                     \
        outPDiscrete = outPicked ?                                                                                    \
                           (1.0 - (gridCellArg).mixAlpha) / vptChoices +                                              \
                               (gridCellArg).mixAlpha * vptLightGridProb(gridCellArg, outSlot, outIndex) :            \
                           0.0;                                                                                       \
    }

bool vptShouldRecordLightGrid(uvec2 launchPixel) {
    uint h = launchPixel.x * 1973u + launchPixel.y * 9277u + worldUBO.frameCounter * 26699u;
    return (h & 7u) == 0u;
}

void vptRecordLightGridFeedback(int cellIndex, int slot, uint index, uint id, float unshadowedIncident,
                                vec3 visibility) {
    if (cellIndex < 0 || slot < 0 || int(VPT_ADAPTIVE_LIGHT_GRID) == 0) { return; }
    float visLum = clamp(dot(visibility, vec3(0.2126, 0.7152, 0.0722)), 0.0, 4.0);
    float incident = clamp(unshadowedIncident * visLum, 0.0, 16.0);
    uint quantized = uint(incident * VPT_LIGHT_GRID_FIXED_SCALE + 0.5);
    if (incident > 1e-7 && quantized == 0u) { quantized = 1u; }

    uint tag = vptLightGridTag(slot, index, id);
    uint base = uint(cellIndex) * uint(VPT_LIGHT_GRID_BUCKETS);
    uint start = tag & uint(VPT_LIGHT_GRID_BUCKETS - 1);
    for (uint probe = 0u; probe < 2u; probe++) {
        uint idx = base + ((start + probe) & uint(VPT_LIGHT_GRID_BUCKETS - 1));
        uint prev = atomicCompSwap(vptLightGridFeedback.buckets[idx].tag, 0u, 0xFFFFFFFFu);
        if (prev == 0u) {
            vptLightGridFeedback.buckets[idx].slot = slot;
            vptLightGridFeedback.buckets[idx].index = index;
            vptLightGridFeedback.buckets[idx].id = id;
            memoryBarrierBuffer();
            atomicExchange(vptLightGridFeedback.buckets[idx].tag, tag);
            if (atomicAdd(vptLightGridFeedback.buckets[idx].count, 1u) < VPT_LIGHT_GRID_MAX_BUCKET_SAMPLES) {
                atomicAdd(vptLightGridFeedback.buckets[idx].sumFixed, quantized);
            }
            return;
        }
        if (prev == tag && vptLightGridFeedback.buckets[idx].slot == slot &&
            vptLightGridFeedback.buckets[idx].index == index && vptLightGridFeedback.buckets[idx].id == id) {
            if (atomicAdd(vptLightGridFeedback.buckets[idx].count, 1u) < VPT_LIGHT_GRID_MAX_BUCKET_SAMPLES) {
                atomicAdd(vptLightGridFeedback.buckets[idx].sumFixed, quantized);
            }
            return;
        }
    }
}

#endif // !VPT_LIGHT_GRID_RESOLVE_PASS

#endif // VPT_LIGHT_GRID_GLSL

