#ifndef RAY_GLSL
#define RAY_GLSL

// Path state helpers: the bit layout of MainRay.stateBits with a getter and setter per flag, the hand-off of the
// last shaded material through the material-state image, and the albedo decode shared by the hit shaders.

#include "util/ray_payloads.glsl"

// Three layers per pixel holding the material of the last surface shaded (see rayStoreMaterial / rayLoadMaterial).
#if defined(VPT_MATERIAL_STATE_BINDING)
layout(set = 5, binding = VPT_MATERIAL_STATE_BINDING, rgba16f) uniform image2DArray rayMaterialStateImage;
#else
layout(set = 5, binding = 7, rgba16f) uniform image2DArray rayMaterialStateImage;
#endif

// Shadow rays stop at the first blocker, and the shadow hit group has nothing to do in a closest-hit shader.
#define VPT_SHADOW_RAY_FLAGS (gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT)

// ---- stateBits layout ----------------------------------------------------------------------------------------
// bits 0-7    bounce count
// bits 12-13  lobe the path last left through (0 diffuse, 1 reflection, 2 transmission)
// the rest    one flag each, described where declared
const uint rayBounceMask = 0xFFu;
const uint rayInsideBoatBit = 1u << 8u;
const uint rayStopBit = 1u << 9u;
const uint rayContinueBit = 1u << 10u;
const uint rayNoisyBit = 1u << 11u;
const uint rayLobeShift = 12u;
const uint rayLobeMask = 0x3u << rayLobeShift;
const uint raySkipFogBit = 1u << 14u;
const uint rayIgnoreWaterSelfBit = 1u << 15u;
const uint rayIndirectVolumetricCloudBit = 1u << 19u;
// The surface the ray just left gathered its block light from the light list, and the ray left it through the
// diffuse lobe, so a light the ray now runs into has been counted already. Survives bounces (resetMainRay keeps it).
const uint rayBlockLightSampledBit = 1u << 20u;
// The primary ray went through a surface without seeing it (a see-through parallax edge): the denoisers' depth has
// to come from what is visible behind it.
const uint rayPassThroughBit = 1u << 21u;
// The ray is travelling through water, so the next surface is seen through it. Survives bounces and is toggled by
// the water surface the ray refracts through.
const uint rayInWaterBit = 1u << 22u;
// The ray is still the camera's own: up to now it has only gone through see-through surfaces (clear glass, sheets
// of water). The far face of a glass block, or the second side of a pane, is then looked through like the first
// rather than shaded as a mirror that would reflect the outside back at the viewer.
const uint rayCameraPathBit = 1u << 23u;

// The surface the ray just left sampled block lights only in the eight sections closest to it (bounced light)
// instead of all 27 around it, so a light hit outside those was not counted there. See block_light.glsl.
const uint rayBlockLightLeanBit = 1u << 24u;
// Experimental original-RTX transport: a single glass layer, remembering the water around it. These do not change
// the payload layout, and resetMainRay keeps the medium state.
const uint rayInGlassBit = 1u << 25u;
const uint rayGlassParentWaterBit = 1u << 26u;
// The camera's view has already passed through a face of glass (or ice, slime, honey). Survives bounces.
const uint rayCrossedGlassBit = 1u << 27u;
// The path is filling the radiance cache (traceSharcUpdate). Survives bounces.
const uint rayCacheUpdateBit = 1u << 28u;

// Every flag accessor below has one of two shapes: test the bit, or set / clear it.

bool rayCacheUpdate(MainRay ray) { return (ray.stateBits & rayCacheUpdateBit) != 0u; }
void raySetCacheUpdate(inout MainRay ray, bool enabled) {
    ray.stateBits = enabled ? (ray.stateBits | rayCacheUpdateBit) : (ray.stateBits & ~rayCacheUpdateBit);
}

bool rayCrossedGlass(MainRay ray) { return (ray.stateBits & rayCrossedGlassBit) != 0u; }
void raySetCrossedGlass(inout MainRay ray, bool enabled) {
    ray.stateBits = enabled ? (ray.stateBits | rayCrossedGlassBit) : (ray.stateBits & ~rayCrossedGlassBit);
}

bool rayInGlass(MainRay ray) { return (ray.stateBits & rayInGlassBit) != 0u; }
bool rayGlassParentWater(MainRay ray) { return (ray.stateBits & rayGlassParentWaterBit) != 0u; }
void raySetInGlass(inout MainRay ray, bool enabled) {
    ray.stateBits = enabled ? (ray.stateBits | rayInGlassBit) : (ray.stateBits & ~rayInGlassBit);
}
void raySetGlassParentWater(inout MainRay ray, bool enabled) {
    ray.stateBits = enabled ? (ray.stateBits | rayGlassParentWaterBit) : (ray.stateBits & ~rayGlassParentWaterBit);
}

void raySetBlockLightLean(inout MainRay ray, bool enabled) {
    ray.stateBits = enabled ? (ray.stateBits | rayBlockLightLeanBit) : (ray.stateBits & ~rayBlockLightLeanBit);
}
bool rayBlockLightLean(MainRay ray) { return (ray.stateBits & rayBlockLightLeanBit) != 0u; }

void raySetCameraPath(inout MainRay ray, bool enabled) {
    ray.stateBits = enabled ? (ray.stateBits | rayCameraPathBit) : (ray.stateBits & ~rayCameraPathBit);
}
bool rayCameraPath(MainRay ray) { return (ray.stateBits & rayCameraPathBit) != 0u; }

// The material-state image texel of this launch for a given layer.
ivec3 rayMaterialStateCoord(int layer) {
    return ivec3(ivec2(gl_LaunchIDEXT.xy), layer);
}

void raySetBounce(inout MainRay ray, uint bounce) {
    ray.stateBits = (ray.stateBits & ~rayBounceMask) | (bounce & rayBounceMask);
}
uint rayBounce(MainRay ray) {
    return ray.stateBits & rayBounceMask;
}

void raySetInsideBoat(inout MainRay ray, bool enabled) {
    ray.stateBits = enabled ? (ray.stateBits | rayInsideBoatBit) : (ray.stateBits & ~rayInsideBoatBit);
}
bool rayInsideBoat(MainRay ray) { return (ray.stateBits & rayInsideBoatBit) != 0u; }

void raySetStop(inout MainRay ray, bool enabled) {
    ray.stateBits = enabled ? (ray.stateBits | rayStopBit) : (ray.stateBits & ~rayStopBit);
}
bool rayShouldStop(MainRay ray) { return (ray.stateBits & rayStopBit) != 0u; }

void raySetContinue(inout MainRay ray, bool enabled) {
    ray.stateBits = enabled ? (ray.stateBits | rayContinueBit) : (ray.stateBits & ~rayContinueBit);
}
bool rayShouldContinue(MainRay ray) { return (ray.stateBits & rayContinueBit) != 0u; }

void raySetNoisy(inout MainRay ray, bool enabled) {
    ray.stateBits = enabled ? (ray.stateBits | rayNoisyBit) : (ray.stateBits & ~rayNoisyBit);
}
bool rayIsNoisy(MainRay ray) { return (ray.stateBits & rayNoisyBit) != 0u; }

void raySetSkipFog(inout MainRay ray, bool enabled) {
    ray.stateBits = enabled ? (ray.stateBits | raySkipFogBit) : (ray.stateBits & ~raySkipFogBit);
}
bool raySkipFog(MainRay ray) { return (ray.stateBits & raySkipFogBit) != 0u; }

void raySetIgnoreWaterSelf(inout MainRay ray, bool enabled) {
    ray.stateBits = enabled ? (ray.stateBits | rayIgnoreWaterSelfBit) : (ray.stateBits & ~rayIgnoreWaterSelfBit);
}
bool rayIgnoreWaterSelf(MainRay ray) { return (ray.stateBits & rayIgnoreWaterSelfBit) != 0u; }

void raySetIndirectVolumetricCloud(inout MainRay ray, bool enabled) {
    ray.stateBits = enabled ? (ray.stateBits | rayIndirectVolumetricCloudBit) : (ray.stateBits & ~rayIndirectVolumetricCloudBit);
}
bool rayUseIndirectVolumetricCloud(MainRay ray) { return (ray.stateBits & rayIndirectVolumetricCloudBit) != 0u; }

void raySetLobeType(inout MainRay ray, uint lobeType) {
    ray.stateBits = (ray.stateBits & ~rayLobeMask) | ((lobeType & 0x3u) << rayLobeShift);
}
uint rayLobeType(MainRay ray) {
    return (ray.stateBits & rayLobeMask) >> rayLobeShift;
}

void raySetBlockLightSampled(inout MainRay ray, bool enabled) {
    ray.stateBits = enabled ? (ray.stateBits | rayBlockLightSampledBit) : (ray.stateBits & ~rayBlockLightSampledBit);
}
bool rayBlockLightSampled(MainRay ray) { return (ray.stateBits & rayBlockLightSampledBit) != 0u; }

void raySetPassThrough(inout MainRay ray, bool enabled) {
    ray.stateBits = enabled ? (ray.stateBits | rayPassThroughBit) : (ray.stateBits & ~rayPassThroughBit);
}
bool rayIsPassThrough(MainRay ray) { return (ray.stateBits & rayPassThroughBit) != 0u; }

void raySetInWater(inout MainRay ray, bool enabled) {
    ray.stateBits = enabled ? (ray.stateBits | rayInWaterBit) : (ray.stateBits & ~rayInWaterBit);
}
bool rayInWater(MainRay ray) { return (ray.stateBits & rayInWaterBit) != 0u; }

// Written to a ray's pad0 before tracing, for the miss shader's benefit (a hit overwrites pad0 with its own data):
// the ray carries what the camera sees off or through a surface - a reflection or refraction - rather than light
// gathered for that surface.
const uint RAY_VIEW_BOUNCE_MARK = 1u;

// Minecraft stores textures and tints for display (sRGB), not as reflectance. Lit as stored, everything looked
// washed out: dark planks turned light cream, grass a pale yellow-green, and dark and bright blocks barely differed.
// Raising them to this power gets (close to) the linear colour the lighting expects; 2.2 is the full conversion,
// 1.0 leaves the stored value as it is.
#ifndef VPT_ALBEDO_GAMMA
#    define VPT_ALBEDO_GAMMA 2.2
#endif
// Bedrock compatibility (observed): Bedrock RTX linearises the colour texture with the exact piecewise sRGB curve
// rather than a power. Off by default, so the power above remains Radiante's own look; the Bedrock RTX profile
// turns it on.
#ifndef VPT_ALBEDO_SRGB_EXACT
#    define VPT_ALBEDO_SRGB_EXACT 0
#endif
vec3 vptSurfaceColour(vec3 stored) {
    stored = max(stored, vec3(0.0));
#if VPT_ALBEDO_SRGB_EXACT != 0
    vec3 curved = pow((stored + 0.055) / 1.055, vec3(2.4));
    return mix(stored / 12.92, curved, greaterThan(stored, vec3(0.04045)));
#else
    return pow(stored, vec3(VPT_ALBEDO_GAMMA));
#endif
}

// ---- material hand-off ---------------------------------------------------------------------------------------
// Layer 0: albedo (rgba). Layer 1: f0 and roughness. Layer 2: metallic, transmission, ior, emission.
// The ray argument is unused; it stays so call sites read as operations on the path.

void rayStoreMaterial(inout MainRay ray,
                      vec4 albedoValue,
                      vec3 f0,
                      float roughness,
                      float metallic,
                      float transmission,
                      float ior,
                      float emission) {
    imageStore(rayMaterialStateImage, rayMaterialStateCoord(0), albedoValue);
    imageStore(rayMaterialStateImage, rayMaterialStateCoord(1), vec4(f0, roughness));
    imageStore(rayMaterialStateImage, rayMaterialStateCoord(2), vec4(metallic, transmission, ior, emission));
}

// Two half floats of extra data packed into pad0.
void rayStoreAux(inout MainRay ray, vec2 aux) {
    ray.pad0 = packHalf2x16(aux);
}

vec2 rayLoadAux(MainRay ray) {
    return unpackHalf2x16(ray.pad0);
}

MaterialInfo rayLoadMaterial(MainRay ray) {
    MaterialInfo info;
    // The closest-hit shader wrote these on this same thread; make those writes visible before reading back.
    memoryBarrierImage();
    vec4 layerAlbedo = imageLoad(rayMaterialStateImage, rayMaterialStateCoord(0));
    vec4 layerSpecular = imageLoad(rayMaterialStateImage, rayMaterialStateCoord(1));
    vec4 layerScalars = imageLoad(rayMaterialStateImage, rayMaterialStateCoord(2));

    info.albedoValue = layerAlbedo;
    info.f0 = layerSpecular.rgb;
    info.roughness = layerSpecular.a;
    info.metallic = layerScalars.x;
    info.transmission = layerScalars.y;
    info.ior = layerScalars.z;
    info.emission = layerScalars.w;
    return info;
}

#endif
