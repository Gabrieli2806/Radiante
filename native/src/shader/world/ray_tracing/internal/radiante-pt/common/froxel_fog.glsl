#ifndef VPT_FROXEL_FOG_GLSL
#define VPT_FROXEL_FOG_GLSL

// Volumetric fog's light, kept in the world (the "froxel" fog of the settings; the name stayed when the grid moved).
//
// The light in the air around the camera lives in two grids of cells fixed to the world: a fine one of half-block
// cells reaching 32 blocks out (16 up and down), and a coarse one of two-block cells reaching 128 (64 up and down).
// Each grid follows the camera a whole cell at a time and wraps around (a cell's texel is its world cell modulo the
// grid's size), so a cell keeps its light for as long as the camera stays near it, and nothing is ever reprojected.
// Each cell holds what reaches it, in two textures: the sun or moon through a shadow ray (rgb) and how much of the sky
// it sees (a); the block lights' glow (rgb, fine grid only) and how many looks the cell has had (a). The world pass relights a share of the cells every frame at points spread through each cell over time, and
// each pixel then marches its own ray through the grids, filtered by the sampler, with the medium (how thick the air
// is, how it scatters towards the eye) worked out along the ray itself.
//
// It used to be a grid cut along the view (froxels), each cell lit at a point on its own view ray. Turning the camera
// cut the air into other cells: every cell then stood for another piece of the world, its history was reprojected
// out of cells of another shape, and the fog moved and changed shape with the view while the player only looked
// around.
//
// Needs worldUBO, lastWorldUBO and skyUBO.

#ifndef VPT_VOLUMETRIC_LIGHT_MAX_DISTANCE
#    define VPT_VOLUMETRIC_LIGHT_MAX_DISTANCE 128.0
#endif

#ifndef VPT_BEDROCK_FOG
#    define VPT_BEDROCK_FOG 0
#endif
#ifndef VPT_BEDROCK_FOG_G
#    define VPT_BEDROCK_FOG_G 0.6
#endif

// Both grids have this many cells (powers of two: a cell's texel is its world cell masked by size - 1).
const ivec3 FOG_VOLUME_EXTENT = ivec3(128, 64, 128);
// Half a block near the camera, so a shaft through a one-block window keeps its edges; two further out.
const float FOG_VOLUME_NEAR_CELL = 0.5;
const float FOG_VOLUME_FAR_CELL = 2.0;
// Bedrock RTX's volume reaches 50 blocks (observed).
const float BEDROCK_FOG_RANGE = 50.0;

float froxelRange() {
#if VPT_BEDROCK_FOG != 0
    return BEDROCK_FOG_RANGE;
#else
    return clamp(VPT_VOLUMETRIC_LIGHT_MAX_DISTANCE, 48.0, 192.0);
#endif
}

float fogVolumeCellSize(int cascade) {
    return cascade == 0 ? FOG_VOLUME_NEAR_CELL : FOG_VOLUME_FAR_CELL;
}

// The world cell the camera is in, and where in it (0 to 1 per axis), for a camera position.
// The block is split off in double precision once (a floor and a subtraction); the cell sizes are powers of two, so
// the rest is integer and float work. A double division per pixel cost about a millisecond on GPUs with slow fp64.
ivec3 fogVolumeCameraCell(dvec3 cameraPos, int cascade, out vec3 fraction) {
    dvec3 blockFloor = floor(cameraPos);
    ivec3 block = ivec3(blockFloor);
    vec3 inBlock = vec3(cameraPos - blockFloor);
    if (cascade == 0) {
        vec3 scaled = inBlock * 2.0;
        vec3 half2 = floor(scaled);
        fraction = scaled - half2;
        return block * 2 + ivec3(half2);
    }
    // Two-block cells: an arithmetic shift is a floor division for negative blocks too.
    ivec3 cell = block >> 1;
    fraction = (vec3(block - cell * 2) + inBlock) * 0.5;
    return cell;
}

// A world cell's texel: two's complement makes the mask a true modulo for negative cells too.
ivec3 fogVolumeTexel(ivec3 worldCell) {
    return worldCell & (FOG_VOLUME_EXTENT - 1);
}

// The world cell a texel stands for while the camera is in cameraCell: the grid covers the cells from cameraCell -
// extent / 2 up to (not including) cameraCell + extent / 2.
ivec3 fogVolumeCellOfTexel(ivec3 texel, ivec3 cameraCell) {
    ivec3 low = cameraCell - FOG_VOLUME_EXTENT / 2;
    return low + ((texel - low) & (FOG_VOLUME_EXTENT - 1));
}

#ifndef VPT_ATMOSPHERE_BETA_R
#    define VPT_ATMOSPHERE_BETA_R vec3(0.000005802, 0.000013558, 0.0000331)
#endif
#ifndef VPT_ATMOSPHERE_BETA_M
#    define VPT_ATMOSPHERE_BETA_M vec3(0.000021)
#endif

// Keep RGB extinction for original RTX; a luminance scalar cannot represent coloured absorption.
vec3 froxelBedrockExtinctionRGB(float worldY) {
    vec2 heights = skyUBO.biomeFogHeights.xy;
    float height = clamp((heights.y - worldY) / max(heights.y - heights.x, 1.0), 0.0, 1.0);
    float density = worldUBO.skyType == 1 ? max(skyUBO.biomeFog.a, 0.0) : 0.0;
    return max(skyUBO.biomeFogChroma.rgb, vec3(0.0)) * density * height;
}

// The medium's extinction per block at a world height, as the per-pixel march has it (volumetricAirMedium and the
// biome fog in world.rgen), as one luminance.
float froxelExtinction(float worldY) {
#if VPT_BEDROCK_FOG != 0
    // The pack's air: extinction per channel scaled by the height profile (full below max_density_height, none
    // from zero_density_height up), as one luminance.
    return max(dot(froxelBedrockExtinctionRGB(worldY), vec3(0.2126, 0.7152, 0.0722)), 1e-6);
#endif
    float sunHeight = abs(normalize(skyUBO.sunDirection).y);
    float airDensity = mix(1.0, 0.7, smoothstep(0.05, 0.35, sunHeight));
    vec3 betaR = VPT_ATMOSPHERE_BETA_R * 18.0;
    vec3 betaM = VPT_ATMOSPHERE_BETA_M * mix(18.0, 54.0, skyUBO.rainGradient);
    vec3 air = (betaR + betaM * 1.15) * airDensity * 0.45 * max(skyUBO.fogControls.z, 0.0);
    return max(dot(air + froxelBedrockExtinctionRGB(worldY), vec3(0.2126, 0.7152, 0.0722)), 1e-6);
}

#endif
