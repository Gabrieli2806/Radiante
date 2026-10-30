#ifndef VPT_PT_BINDINGS_GLSL
#define VPT_PT_BINDINGS_GLSL

// Every descriptor and ray payload the world raygen declares, in one place (except froxel fog's grids, see the end).
//
// Moved verbatim out of world.rgen, in their old order; the ReSTIR image, which used to be declared just before
// main(), now comes last (the SPIR-V does not change). Two declarations here are shared with
// common/block_light.glsl, which declares them for the closest-hit shaders: the set 2 binding 1 "last frame" world
// uniform and the ReSTIR reservoir image. GLSL does not allow the same binding to be declared twice in one module,
// so once the raygen includes block_light.glsl it must skip its own copies. PT_DECLARES_LAST_WORLD_UBO and
// PT_DECLARES_RESTIR_IMAGE tell it that this file has already declared them (block_light.glsl then reads
// lastWorldUBO under its own name, vptLastWorldUBO, through a macro).

layout(set = 5, binding = 0) uniform sampler2D transLUT;
layout(set = 5, binding = 2) uniform samplerCube skyFull;

layout(set = 1, binding = 0) uniform accelerationStructureEXT topLevelAS;

layout(set = 1, binding = 1) readonly buffer BLASOffsets {
    uint offsets[];
}
blasOffsets;

layout(set = 2, binding = 0) uniform WorldUniform {
    WorldUBO worldUBO;
};

layout(set = 2, binding = 1) uniform LastWorldUniform {
    WorldUBO lastWorldUBO;
};
#define PT_DECLARES_LAST_WORLD_UBO 1

// The incident light meter (tone mapping's metering mode "incident"): one sample per cell of a 16 x 16 grid.
layout(set = 2, binding = 3) buffer LightMeterBuffer {
    vec4 samples[256];
    vec4 daylight;
    vec4 bedrockMeter; // The experimental Bedrock profile's meter state, from exposure.comp (see there).
    // The experimental Bedrock profile's samples over time: the light accumulated and its frames, and the surface it
    // lies on (render-space point, and its face's axis as 1 to 6).
    vec4 sampleHistory[256];
    vec4 sampleSurface[256];
}
lightMeter;

layout(set = 2, binding = 2) uniform SkyUniform {
    SkyUBO skyUBO;
};

// These libraries read the uniforms above and declare textures of their own. They are included here, between
// the uniforms and the output images, where world.rgen always included them: the declaration order decides the
// order of the unused resources in unoptimised SPIR-V, and keeping it keeps that output byte-equal as well.
#include "common/celestial.glsl"
#include "common/volumetric_cloud.glsl"
#include "common/water_medium.glsl"
#include "common/reflection_guide.glsl"
#include "common/biome_fog_light.glsl"
#include "common/bedrock_atmosphere.glsl"

layout(set = 3, binding = 0, rgba16f) uniform image2D outputImage;
layout(set = 3, binding = 1, rgba8) uniform image2D diffuseAlbedoImage;
layout(set = 3, binding = 2, rgba8) uniform image2D specularAlbedoImage;
layout(set = 3, binding = 3, rgba16f) uniform image2D normalRoughnessImage;
layout(set = 3, binding = 4, rg16f) uniform image2D motionVectorImage;
layout(set = 3, binding = 5, r16f) uniform image2D linearDepthImage;
layout(set = 3, binding = 6, r16f) uniform image2D specularHitDepthImage;
layout(set = 3, binding = 7, r16f) uniform image2D firstHitDepthImage;
layout(set = 3, binding = 8, rgba16f) uniform image2D firstHitDiffuseDirectLightImage;
layout(set = 3, binding = 9, rgba16f) uniform image2D firstHitDiffuseIndirectLightImage;
layout(set = 3, binding = 10, rgba16f) uniform image2D firstHitSpecularImage;
layout(set = 3, binding = 11, rgba16f) uniform image2D firstHitClearImage;
layout(set = 3, binding = 12, rgba16f) uniform image2D firstHitBaseEmissionImage;
layout(set = 3, binding = 13, rgba16f) uniform image2D fogImage;
layout(set = 3, binding = 15, rgba16f) uniform image2D fogTransmissionImage;
layout(set = 3, binding = 14, rgba16f) uniform image2D firstHitRefractionImage;
layout(set = 5, binding = 32, r16f) uniform image2D postStarCloudTransmittanceImage;

#if USE_SHARC
struct SharcConfig {
    uvec2 hashEntriesAddress;
    uvec2 lockAddress;
    uvec2 accumulationAddress;
    uvec2 resolvedAddress;
    vec4 cameraPosition;
    vec4 cameraPositionPrev;
    float sceneScale;
    float radianceScale;
    uint accumulationFrameNum;
    uint staleFrameNumMax;
    uint capacity;
    uint frameIndex;
    uint enableAntiFireflyFilter;
    uint useLockBuffer;
    uint debugMode;
    uint updateDownsampleFactor;
};

layout(set = 4, binding = 0) readonly buffer SharcConfigBuffer {
    SharcConfig sharcConfig;
};
layout(set = 4, binding = 1) readonly buffer SharcHashEntriesBuffer {
    uint64_t sharcHashEntriesRaw[];
};
layout(set = 4, binding = 2) readonly buffer SharcAccumulationBuffer {
    uvec4 sharcAccumulationRaw[];
};
layout(set = 4, binding = 3) readonly buffer SharcResolvedBuffer {
    uvec4 sharcResolvedRaw[];
};
layout(set = 4, binding = 4) readonly buffer SharcLockBuffer {
    uint sharcLockRaw[];
};
#endif

layout(location = 0) rayPayloadEXT MainRay mainRay;
layout(location = 1) rayPayloadEXT ShadowRay shadowRay;

const uint VPT_WORLD_MISS_INDEX = 1u;
const uint VPT_HAND_MISS_INDEX = 2u;
const uint VPT_WORLD_NO_VOLUMETRIC_MISS_INDEX = 3u;

// ReSTIR reservoirs for the block lights (written by common/block_light.glsl, cleared at the start of main).
#ifndef VPT_RESTIR
#    define VPT_RESTIR 0
#endif
#if VPT_RESTIR != 0
#    ifndef VPT_RESTIR_BINDING
#        define VPT_RESTIR_BINDING 39
#    endif
layout(set = 5, binding = VPT_RESTIR_BINDING, rgba32f) uniform image2DArray vptRestirImage;
#    define PT_DECLARES_RESTIR_IMAGE 1
#endif

// The volumetric fog's world grids (set 5 bindings 40 to 43, 46, 47, 49) are declared in pt_froxel.glsl, after
// common/froxel_fog.glsl and block_light_data.glsl: they exist only with the fog's grid on, and declared here they would
// reorder the unused resources of the SHaRC update variant in unoptimised SPIR-V.

#endif // VPT_PT_BINDINGS_GLSL
