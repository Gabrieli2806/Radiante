#ifndef VPT_PT_FROXEL_GLSL
#define VPT_PT_FROXEL_GLSL

// Froxel fog (common/froxel_fog.glsl): this thread's share of the fresh cells, and the per-pixel lookup into last
// frame's integrated grid. Only compiled with froxel fog on. Its three grids are declared here rather than in
// pt_bindings.glsl (see the note there).
// Moved verbatim out of world.rgen.

#if VPT_FROXEL_ACTIVE
#    include "common/froxel_fog.glsl"
#    include "common/block_light_data.glsl"

layout(set = 5, binding = 40, rgba16f) uniform image3D froxelLightImage;
const float FROXEL_BLOCK_LIGHT_REACH = 32.0;
// How much of the block lights the air scatters: a soft glow, not a lit wall of haze across a street of signs.
const float FROXEL_BLOCK_LIGHT_GLOW = 0.12;
layout(set = 5, binding = 42) uniform sampler3D froxelIntegralTexture;
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0 && VPT_BEDROCK_FOG != 0
layout(set = 5, binding = 47) uniform sampler3D froxelTransmissionTexture;
#endif

// Light reaching a point in the air from the block lights around it (torches, lanterns, glowstone...), picked as the
// surfaces pick them (block_light.glsl) but without a surface: the fog glows around each light.
vec3 froxelBlockLight(vec3 position, inout uint seed) {
    if (worldUBO.blockLightSampling == 0u) { return vec3(0.0); }
    // The eight sections nearest the point (the 2x2x2 block around its closest section corner), not all 27 around
    // it: lights within about 8 to 24 blocks, which is as far as a glow in the air carries.
    int slots[9];
    int slotCount = 0;
    ivec3 base = ivec3(floor((position + vec3(worldUBO.cameraPos.xyz) - 8.0) / 16.0));
    ivec3 center = vptSectionOf(position);
    uint neighbours;
    if (vptLightNeighbours(center, neighbours)) {
        VPT_APPEND_NEIGHBOUR_SLOTS(center, neighbours & vptNearNeighbourBits(base, center), slots, slotCount)
    } else {
        for (int corner = 0; corner < 8; corner++) {
            int slot = vptSectionLightSlot(base + ivec3(corner & 1, (corner >> 1) & 1, corner >> 2));
            if (slot >= 0) { slots[slotCount++] = slot; }
        }
    }
    if (vptHasEntityLights()) { slots[slotCount++] = VPT_ENTITY_LIGHT_SLOT; }
    if (slotCount == 0) { return vec3(0.0); }

    const int CANDIDATES = 2;
    vec3 cameraPos = vec3(worldUBO.cameraPos.xyz);
    float weightSum = 0.0;
    float chosenTarget = 0.0;
    vec3 chosenContribution = vec3(0.0);
    vec3 chosenDir = vec3(0.0);
    float chosenDistance = 0.0;
    for (int i = 0; i < CANDIDATES; i++) {
        int slot = slots[min(int(rand(seed) * float(slotCount)), slotCount - 1)];
        uint slotLightCount = vptSlotLightCount(slot);
        uint lightIndex = min(uint(rand(seed) * float(slotLightCount)), slotLightCount - 1u);
        VptPackedLight light = VptPackedLightBuffer(vptSlotLightAddress(slot)).lights[lightIndex];
        float area = light.p0Area.w;
        if (area <= 1e-8) { continue; }
        bool pointLight = vptIsPointLight(light);
        vec3 point = pointLight ? light.p0Area.xyz - cameraPos :
                                  vptSampleTrianglePoint(light.p0Area.xyz, light.p1.xyz, light.p2.xyz,
                                                         vec2(rand(seed), rand(seed))) - cameraPos;
        vec3 toLight = point - position;
        float distance2 = dot(toLight, toLight);
        if (distance2 <= 1e-6) { continue; }
        float lightDistance = sqrt(distance2);
        vec3 dir = toLight / lightDistance;
        float cosLight = pointLight ? 1.0 : dot(light.normal.xyz, -dir);
        if (cosLight <= 0.0) { continue; }
        // Kept from growing without bound right next to a light, where one cell would flare.
        vec3 radiance = pointLight ? light.radiance.rgb * vptPointLightScale() :
                        (light.radiance.w > 0.5 ? light.radiance.rgb * vptExactLightScale() : light.radiance.rgb);
#ifndef RADIANTE_HOOK_BLOCK_LIGHT_DEFAULT
        hookBlockLight(radiance, uint(clamp(light.radiance.w + 0.5, 0.0, 2.0))); // hooks/block_light.glsl
#endif
        vec3 contribution = radiance * (cosLight / max(distance2, 0.5));
        float target = dot(contribution, vec3(0.2126, 0.7152, 0.0722));
        if (target <= 1e-10) { continue; }
        float sourcePdf = 1.0 / (float(slotCount) * float(slotLightCount) * area);
        float weight = target / sourcePdf;
        weightSum += weight;
        if (rand(seed) * weightSum < weight) {
            chosenTarget = target;
            chosenContribution = contribution;
            chosenDir = dir;
            chosenDistance = lightDistance;
        }
    }
    if (chosenTarget <= 0.0) { return vec3(0.0); }

    shadowRay.radiance = vec3(0.0);
    shadowRay.throughput = vec3(1.0);
    shadowRay.insideBoat = 0u;
    shadowRay.pad0 = BLOCK_LIGHT_QUERY;
    traceRayEXT(topLevelAS, VPT_SHADOW_RAY_FLAGS, WORLD_MASK | PLAYER_MASK, 0, 0, 0, position, 0.001, chosenDir,
                max(chosenDistance - 0.02, 0.002), 1);
    shadowRay.pad0 = 0u;
    vec3 visibility = shadowRay.radiance * shadowRay.throughput;
    vec3 estimate = chosenContribution / chosenTarget * (weightSum / float(CANDIDATES));
    return VPT_INDIRECT_LIGHT_STRENGTH * worldUBO.emissionBrightness * estimate * visibility;
}

// How far above a point its light is taken from when the point stands for open air (see froxelFog's tail): over
// every building and hill.
const float FROXEL_OPEN_AIR_LIFT = 500.0;
// Past the froxel volume, over how many blocks a shadow at its far edge fades out of the air behind it.
const float FROXEL_TAIL_SHADOW_REACH = 24.0;

#if VPT_BEDROCK_FOG != 0
// Bedrock RTX's volumetric fog (observed
// structure): the pack's air medium, lit by the sun through a shadow ray with Henyey-Greenstein scattering normalised
// by 1/pi, and by the sky; the sunlit part scales with the square of the height profile, the sky part with it once.
// The anisotropy g is a run-time value in Bedrock (UNKNOWN; VPT_BEDROCK_FOG_G).
vec3 shadeBedrockFogCell(vec3 position, vec3 rayDir, bool openAir) {
    vec3 lightDir = celestialLightDirection();
    float worldY = position.y + float(worldUBO.cameraPos.y);
    float height = biomeFogHeightProfile(worldY);
    vec3 sigmaS = biomeFogChroma() * biomeFogDensity() * clamp(skyUBO.biomeFog.rgb, vec3(0.0), vec3(1.0));
    if (height <= 0.0 || max(sigmaS.r, max(sigmaS.g, sigmaS.b)) <= 0.0) { return vec3(0.0); }

    float g = clamp(VPT_BEDROCK_FOG_G, -0.95, 0.95);
    float cosTheta = clamp(dot(rayDir, lightDir), -1.0, 1.0);
    float phase = (1.0 - g * g) / pow(abs(1.0 + g * g - 2.0 * g * cosTheta), 1.5) / PI;
    vec3 sunPosition = openAir ? position + lightDir * FROXEL_OPEN_AIR_LIFT : position;
    vec3 sun = sampleVolumetricDirectionalLight(sunPosition, lightDir, false) * phase;
    vec3 sky = biomeFogAmbient(rayDir) * (openAir ? 1.0 : volumetricSkyVisibility(position, false)) / PI;
    return sigmaS * (height * height * sun + height * sky);
}
#endif

// The light one cell scatters towards the camera per block, from the same medium as the per-pixel march (integrateVolumetricFog): the air, its light shafts and the biome's fog.
// openAir: as it would be with nothing around to shade it, the sun and the sky in full.
vec3 shadeFroxelCell(vec3 position, vec3 rayDir, float distance, inout uint seed, bool openAir) {
#if VPT_BEDROCK_FOG != 0
    return shadeBedrockFogCell(position, rayDir, openAir);
#endif
    vec3 lightDir = celestialLightDirection();
    float cosTheta = clamp(dot(rayDir, lightDir), -1.0, 1.0);
    vec3 sigmaS;
    vec3 sigmaT;
    volumetricAirMedium(cosTheta, sigmaS, sigmaT);
    float airDensity = volumetricAirDensity();
    float fogStrength = max(skyUBO.fogControls.z, 0.0);
    vec3 airSigmaS = sigmaS * airDensity * max(VPT_VOLUMETRIC_LIGHT_SCATTERING_STRENGTH, 0.0) * fogStrength;
    vec3 airSigmaT = sigmaT * airDensity * 0.45 * fogStrength;
    float worldY = position.y + float(worldUBO.cameraPos.y);
    vec3 biomeT = biomeFogChroma() * biomeFogDensity() * biomeFogHeightProfile(worldY);

    vec3 biomeAlbedo = clamp(skyUBO.biomeFog.rgb, vec3(0.0), vec3(1.5));
    float biomePhase = mix(INV_4_PI, min(henyeyGreenstein(cosTheta, VPT_BIOME_FOG_FORWARD), 4.0), 0.6);
    float shaftPhase = mix(INV_4_PI, min(henyeyGreenstein(cosTheta, 0.5), 2.0), 0.5);
    float shaftDensity =
        max(VPT_LIGHT_SHAFTS, 0.0) * (1.0 - smoothstep(LIGHT_SHAFT_REACH * 0.6, LIGHT_SHAFT_REACH, distance));

    vec3 lightRadiance = vec3(0.0);
    if (skyUBO.rainGradient < 0.98) {
        vec3 absolute = position + vec3(worldUBO.cameraPos.xyz);
        vec3 sunPosition = openAir ? position + lightDir * FROXEL_OPEN_AIR_LIFT : position;
        lightRadiance = sampleVolumetricDirectionalLight(sunPosition, lightDir, false) *
                        volumetricCloudLightVisibility(absolute, lightDir, 2, 0.175);
    }
    vec3 skyAmbient = biomeFogAmbient(rayDir) * (openAir ? 1.0 : volumetricSkyVisibility(position, false));
    vec3 source = lightRadiance * (airSigmaS + vec3(shaftDensity * shaftPhase)) + skyAmbient * airSigmaT * 0.35;
    source += (lightRadiance * biomePhase * max(VPT_BIOME_FOG_SCATTERING, 0.0) + skyAmbient) * biomeAlbedo * biomeT;
    // Block lights scatter evenly in every direction: the shafts' density near the camera, the biome's fog, the air.
    vec3 blockScatter = vec3(shaftDensity) + biomeAlbedo * biomeT + airSigmaT * 0.35;
    // Only near the camera, where a glow around a light shows; further out the cells are too coarse for it.
    if (distance < FROXEL_BLOCK_LIGHT_REACH && !openAir) {
        source += froxelBlockLight(position, seed) * blockScatter * (INV_4_PI * FROXEL_BLOCK_LIGHT_GLOW);
    }
    return source;
}

// This thread's share of the cells lit afresh this frame (froxelIsFresh); froxel/carry.comp has already carried
// the others' history over. Each fresh cell blends its new light into its reprojected history.
void shadeFroxels() {
    if (VPT_VOLUMETRIC_LIGHT_MODE != VPT_VOLUMETRIC_LIGHT_MODE_VOLUMETRIC || worldUBO.skyType != 1) { return; }
    uint threadIndex = gl_LaunchIDEXT.y * gl_LaunchSizeEXT.x + gl_LaunchIDEXT.x;
    uint threadCount = gl_LaunchSizeEXT.x * gl_LaunchSizeEXT.y;
    const ivec2 quarter = FROXEL_EXTENT.xy / 2;
    uint freshCount = uint(quarter.x * quarter.y * FROXEL_EXTENT.z);
    bool historyUsable = froxelHistoryUsable();
    int writeOffset = froxelWriteOffset();
    int readOffset = froxelReadOffset();
    for (uint index = threadIndex; index < freshCount; index += threadCount) {
        int slice = int(index) / (quarter.x * quarter.y);
        int inSlice = int(index) % (quarter.x * quarter.y);
        uint pattern = froxelFreshPattern(slice);
        ivec3 cell = ivec3((inSlice % quarter.x) * 2 + int(pattern & 1u), (inSlice / quarter.x) * 2 + int(pattern >> 1u),
                           slice);
        uint seed = xxhash32(uvec3(uint(cell.x) | (uint(cell.y) << 16u), uint(cell.z), worldUBO.seed ^ 0x6A09E667u));
        vec3 jitter = vec3(rand(seed), rand(seed), rand(seed));
        vec2 uv = (vec2(cell.xy) + jitter.xy) / vec2(FROXEL_EXTENT.xy);
        vec3 rayDir = froxelRayDirection(uv);
        float distance = froxelDistanceFromW((float(cell.z) + jitter.z) / float(FROXEL_EXTENT.z));
        vec3 position = froxelCameraOrigin() + rayDir * distance;

        vec3 inscatter = shadeFroxelCell(position, rayDir, distance, seed, false);
        vec3 previousUvw;
        if (historyUsable && froxelReproject(position, previousUvw)) {
            ivec3 texel = clamp(ivec3(previousUvw * vec3(FROXEL_EXTENT)), ivec3(0), FROXEL_EXTENT - 1);
            vec4 history = imageLoad(froxelLightImage, texel + ivec3(0, 0, readOffset));
            if (finiteVec3(history.rgb)) { inscatter = mix(max(history.rgb, vec3(0.0)), inscatter, 0.3); }
        }
        if (!finiteVec3(inscatter)) { inscatter = vec3(0.0); }
        float extinction = froxelExtinction(position.y + float(worldUBO.cameraPos.y));
        imageStore(froxelLightImage, cell + ivec3(0, 0, writeOffset),
                   vec4(clamp(inscatter, vec3(0.0), vec3(65504.0)), extinction));
    }
}

// The fog between the camera and endDistance along this pixel's ray, read from last frame's integrated grid at where
// the end point sat then; past the grid the medium carries on lit as the grid's last slice.
void froxelFog(vec3 origin, vec3 rayDir, vec2 uv, float endDistance, float biomeReach, bool hitSky,
               out vec3 transmittance, out vec3 radiance) {
    transmittance = vec3(1.0);
    radiance = vec3(0.0);
    if (!finiteFloat(endDistance) || endDistance <= 0.05) { return; }
    float range = froxelRange();
    float gridEnd = min(endDistance, range * 0.999);
    vec3 uvw = vec3(uv, froxelWFromDistance(gridEnd));
    vec3 previousUvw;
    if (froxelHistoryUsable() && froxelReproject(origin + rayDir * gridEnd, previousUvw)) { uvw = previousUvw; }

#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0 && VPT_BEDROCK_FOG != 0
    // Observed: sample log-distance directly; decode after filtering sqrt(inscatter).
    vec3 encoded = texture(froxelIntegralTexture, uvw).rgb;
    vec3 transmissionRGB = texture(froxelTransmissionTexture, uvw).rgb;
    if (!finiteVec3(encoded) || !finiteVec3(transmissionRGB)) { return; }
    radiance = max(encoded, vec3(0.0)) * max(encoded, vec3(0.0));
    transmittance = clamp(transmissionRGB, vec3(0.0), vec3(1.0));
    return;
#endif
    // The integral's cell k runs to the far edge of slice k, so a distance maps half a cell back.
    float slice = uvw.z * float(FROXEL_EXTENT.z);
    vec4 integral = texture(froxelIntegralTexture, vec3(uvw.xy, (max(slice - 1.0, 0.0) + 0.5) / float(FROXEL_EXTENT.z)));
    if (slice < 1.0) { integral = mix(vec4(0.0, 0.0, 0.0, 1.0), integral, clamp(slice, 0.0, 1.0)); }
    if (!finiteVec3(integral.rgb) || !finiteFloat(integral.a)) { return; }
    radiance = max(integral.rgb, vec3(0.0));
    float t = clamp(integral.a, 0.0, 1.0);

    // Past the volume the medium carries on, lit as its last slice. (Bedrock's own fog was taken to stop at the
    // volume's far edge, 50 blocks out: every hill beyond stood equally clear behind a fogged foreground, and with
    // the haze as faint as it now is, 50 blocks of it showed nothing of a biome's air at all.)
    float tail = max(max(endDistance, biomeReach) - range, 0.0);
    if (tail > 0.0 && t > 1e-3) {
        // Bilinear by hand (the grid is a storage image here): nearest cells showed as blocks across the sky.
        vec2 texelPosition = uv * vec2(FROXEL_EXTENT.xy) - 0.5;
        ivec2 base = ivec2(floor(texelPosition));
        vec2 fraction = texelPosition - vec2(base);
        int farSlice = froxelReadOffset() + FROXEL_EXTENT.z - 1;
        vec4 far = vec4(0.0);
        for (int corner = 0; corner < 4; corner++) {
            ivec2 offset = ivec2(corner & 1, corner >> 1);
            ivec2 texel = clamp(base + offset, ivec2(0), FROXEL_EXTENT.xy - 1);
            vec2 weight = mix(vec2(1.0) - fraction, fraction, vec2(offset));
            far += imageLoad(froxelLightImage, ivec3(texel, farSlice)) * (weight.x * weight.y);
        }
        if (finiteVec3(far.rgb) && finiteFloat(far.a)) {
            float extinction = max(far.a, 1e-6);
            float tailTransmittance = exp(-extinction * tail);
            float tailDepth = (1.0 - tailTransmittance) / extinction;
            vec3 tailLight = max(far.rgb, vec3(0.0)) * tailDepth;
            if (hitSky) {
                // Towards the sky the air behind the volume is open: a shadow that falls on the volume's far edge
                // does not run on through all of it. Lit whole as that edge, a tower between the camera and the sun
                // stood a second time in the sky, a dark, stepped copy of itself cut out of the haze around the
                // sun. The edge's own light fades into the open air's over FROXEL_TAIL_SHADOW_REACH.
                float edgeRate = extinction + 1.0 / FROXEL_TAIL_SHADOW_REACH;
                float edgeDepth = (1.0 - exp(-edgeRate * tail)) / edgeRate;
                uint openSeed = 0u;
                vec3 open = shadeFroxelCell(origin + rayDir * range, rayDir, range, openSeed, true);
                if (finiteVec3(open)) {
                    tailLight = max(far.rgb, vec3(0.0)) * edgeDepth + max(open, vec3(0.0)) * (tailDepth - edgeDepth);
                }
            }
            radiance += t * tailLight;
            t *= tailTransmittance;
        }
    }
    radiance = softClampVolumetricLight(radiance);
    transmittance = vec3(t);
}
#endif

#endif // VPT_PT_FROXEL_GLSL
