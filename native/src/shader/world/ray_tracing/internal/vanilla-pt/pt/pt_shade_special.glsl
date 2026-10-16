#ifndef VPT_PT_SHADE_SPECIAL_GLSL
#define VPT_PT_SHADE_SPECIAL_GLSL

// The shading of the special hit groups: vanilla clouds, the End portal and gateway, the boat's water patch,
// world_no_reflect, and the emissive sheets (lightning, beacon beam). Their closest hits only record the hit
// (world/hit/*.rchit with their PT_KIND_*), and ptShadeRecorded (pt/pt_trace.glsl) picks the function below by the
// recorded kind.
//
// Each function is the main() of the closest hit it replaced (clouds.rchit, end_portal.rchit, end_gateway.rchit,
// water_mask.rchit, world_no_reflect.rchit, transparent_only.rchit), statement for statement and in the same order,
// so the light, the material hand-off and the shadow trace come out as they did. The only changes are how the hit is
// read (g_hit, filled from the record, in place of the hit built-ins and the triangle loads) and that the material
// goes through ptStoreMaterial (g_lastMaterial) instead of the material-state image.
//
// One value cannot be rebuilt exactly. The End portal, the End gateway and the water patch placed the hit at
// the triangle's interpolated object position times the instance's full object-to-world matrix; the record carries
// the rotation and scale but not the translation (design: no translation, the hit is origin + direction * distance
// in the raygen). The hit on the ray is the same point up to float rounding, far below what the starfield
// projection or the water patch's 0.001 step past the surface can show.
//
// Needs pt/pt_shade_default.glsl first (g_hit, ptStoreMaterial, loadPreviousScenePos, the set 0 textures and set 1
// descriptors, ptBeginRaygenShade).

#include "common/end_portal.glsl"

// ---- vanilla clouds (world/hit/clouds.rchit)

// Share of the light behind a cloud it hides. Bedrock's clouds are translucent rather than solid white blocks; a
// ray entering a cloud takes this much of the cloud's colour and carries on through it with the rest.
#ifndef VPT_VANILLA_CLOUD_OPACITY
#    define VPT_VANILLA_CLOUD_OPACITY 0.6
#endif
// Inside a cloud: how fast its haze thickens with the distance seen through it (per block), and the most of the
// view it takes.
#ifndef VPT_VANILLA_CLOUD_INSIDE_DENSITY
#    define VPT_VANILLA_CLOUD_INSIDE_DENSITY 0.18
#endif
#ifndef VPT_VANILLA_CLOUD_INSIDE_MAX
#    define VPT_VANILLA_CLOUD_INSIDE_MAX 0.85
#endif
#ifndef VPT_VANILLA_CLOUD_SKY_LIGHT
#    define VPT_VANILLA_CLOUD_SKY_LIGHT 1.6
#endif

void ptShadeClouds() {
    vec3 viewDir = -mainRay.direction;

    MaterialVertex m0 = g_hit.m0;
    MaterialVertex m1 = g_hit.m1;
    MaterialVertex m2 = g_hit.m2;

    vec3 baryCoords = g_hit.bary;
    vec3 worldPos = g_hit.rayOrigin + g_hit.rayDir * g_hit.hitT;
    uint coordinate = getCoordinate(m0.packedData);
    vec3 normal = baryCoords.x * m0.norm + baryCoords.y * m1.norm + baryCoords.z * m2.norm;
    if (coordinate == 1) {
        normal = normalize(mat3(worldUBO.cameraViewMatInv) * normal);
    } else {
        normal = normalize(normal);
    }
    bool useColorLayer = hasColorLayer(m0.packedData);
    vec3 colorLayer;
    if (useColorLayer) {
        colorLayer = (baryCoords.x * m0.colorLayer + baryCoords.y * m1.colorLayer + baryCoords.z * m2.colorLayer).rgb;
    } else {
        colorLayer = vec3(1.0);
    }

    bool useTexture = hasTexture(m0.packedData);
    vec2 textureUV = baryCoords.x * m0.textureUV + baryCoords.y * m1.textureUV + baryCoords.z * m2.textureUV;
    uint textureID = m0.textureID;

    vec4 albedoSample = vec4(1.0);
    if (useTexture) { albedoSample = sampleTexture(textures[nonuniformEXT(textureID)], textureUV, false); }

    vec3 albedo = albedoSample.rgb;
    float alpha = albedoSample.a;
    // Vanilla darkens the cloud colour itself at dusk and at night, to stand in for the light it cannot compute.
    // Here the light does that, so only the colour's hue is kept: darkened twice, clouds turned into black
    // silhouettes against the afterglow.
    float colorPeak = max(colorLayer.r, max(colorLayer.g, colorLayer.b));
    vec3 tint = albedo * (colorPeak > 1e-3 ? colorLayer / colorPeak : vec3(1.0));

    LabPBRMat mat;
    mat.albedo = tint;
    mat.f0 = vec3(0.04);
    mat.roughness = 1.0;
    mat.metallic = 0.0;
    mat.subSurface = 0.0;
    mat.transmission = 0.0;
    mat.ior = 1.5;
    mat.emission = 0.0;

    mainRay.hitT = g_hit.hitT;

    vec3 rayOrigin = worldPos;

    // shadow ray for direct lighting
    vec3 sunDir = celestialSunDirection();
    vec3 sampledLightDir = sunDir;
    if (sampledLightDir.y < 0) { sampledLightDir = -sampledLightDir; }

    // Clouds are mostly viewed from below; keep underside lit via two-sided + backlit response.
    float ndotl = dot(normal, sampledLightDir);
    float ndotv = dot(normal, viewDir);
    float frontLit = max(ndotl, 0.0);
    float wrappedLit = clamp((abs(ndotl) + 0.4) / 1.4, 0.0, 1.0);
    float backLit = max(-ndotl, 0.0) * max(-ndotv, 0.0);
    float cloudPhase = max(frontLit, max(wrappedLit * 0.6, backLit * 1.35));
    vec3 lightBRDF = tint * (INV_PI * cloudPhase);

    shadowRay.radiance = vec3(0.0);
    shadowRay.throughput = vec3(1.0);
    shadowRay.insideBoat = 0u;
    shadowRay.pad0 = 0u;
    traceRayEXT(topLevelAS, VPT_SHADOW_RAY_FLAGS,
                WORLD_MASK, // masks
                0,          // sbtRecordOffset
                0,          // sbtRecordStride
                0,          // missIndex
                rayOrigin, 0.001, sampledLightDir, 1000, 1);

    vec3 lightContribution = shadowRay.radiance;
    bool leaving = dot(normal, mainRay.direction) > 0.0;

    float progress = skyUBO.rainGradient;
    vec3 lightRadiance = lightContribution * mainRay.throughput * lightBRDF;
    lightRadiance *= alpha * 0.65;

    float dayFactor = smoothstep(-0.3, 0.3, sunDir.y);
    vec3 skyAmbient = texture(skyFull, normalize(viewDir)).rgb;
    // Light a cloud scatters from the whole sky, not just the sun: what keeps an overcast underside pale grey
    // rather than dark, and a translucent cloud reading as white against the blue behind it.
    // The sky above, and the horizon on the sun's side: at sunset and in the blue hour the zenith is already
    // dark while the horizon still glows, and that glow is what lights the underside of clouds orange and pink.
    vec3 skyAbove = texture(skyFull, vec3(0.0, 1.0, 0.0)).rgb;
    vec3 lightHorizon = texture(skyFull, normalize(vec3(sunDir.x, 0.08, sunDir.z))).rgb;
    // Only clouds on the sun's side of the sky catch that glow, and the more so the closer they are to it.
    vec2 cloudSide = normalize(mainRay.direction.xz + vec2(1e-5));
    vec2 sunSide = normalize(sunDir.xz + vec2(1e-5));
    float glowSide = max(dot(cloudSide, sunSide), 0.0);
    // Around sunrise and sunset only: at noon the zenith already lights them, and deep in the night there is no glow.
    float glowTime = smoothstep(-0.35, -0.05, sunDir.y) * (1.0 - smoothstep(0.2, 0.45, sunDir.y));
    float horizonWeight = 0.3 * glowSide * glowSide * glowTime;
    vec3 skyLight = skyAbove * (1.0 - horizonWeight) + lightHorizon * horizonWeight;
    lightRadiance += tint * skyLight * VPT_VANILLA_CLOUD_SKY_LIGHT * mainRay.throughput;
    vec3 rainyRadiance = mix(skyAmbient * 0.12, vec3(0.08), dayFactor);
    vec3 wetCloudRadiance = lightRadiance * mix(0.2, 0.35, dayFactor) + rainyRadiance;
    // Only the face the ray enters through adds the cloud; the one it leaves through just lets it out again.
    if (!leaving) {
        mainRay.radiance += VPT_VANILLA_CLOUD_OPACITY * mix(lightRadiance, wetCloudRadiance, progress);
        mainRay.throughput *= 1.0 - VPT_VANILLA_CLOUD_OPACITY;
    } else if (rayBounce(mainRay) == 0u) {
        // The view leaves a cloud it never entered: the camera is inside it. The cloud is a haze there, thicker the
        // further the view runs through it. With nothing of it in front of the eyes the cloud vanished once inside
        // and there was no telling whether one was in it.
        float haze = VPT_VANILLA_CLOUD_INSIDE_MAX * (1.0 - exp(-g_hit.hitT * VPT_VANILLA_CLOUD_INSIDE_DENSITY));
        mainRay.radiance += haze * mix(lightRadiance, wetCloudRadiance, progress);
        mainRay.throughput *= 1.0 - haze;
    }

    mainRay.hitT = g_hit.hitT;
    mainRay.normal = vec3(0.0);
    // Stored as fully transmissive so the bounce loop passes through it as it does through glass.
    ptStoreMaterial(vec4(tint, 1.0 - VPT_VANILLA_CLOUD_OPACITY), vec3(0.04), 1.0, 0.0, 1.0, 1.0, 0.0);
    raySetNoisy(mainRay, false);
    mainRay.hasPrevScenePos = 0u;
    mainRay.origin = worldPos + mainRay.direction * 0.001;
    mainRay.coneWidth += g_hit.hitT * mainRay.coneSpread;
    raySetContinue(mainRay, true);
    raySetStop(mainRay, false);
}

// ---- End portal (world/hit/end_portal.rchit)

// The light the End portal gives off, per unit of surface (radiance): a teal of about the strength of a lamp's.
const vec3 END_PORTAL_GLOW = vec3(0.35, 0.75, 0.85) * 5.0;

// viewMark: the ray belongs to a view chain (what the old closest hit read as pad0 == RAY_VIEW_BOUNCE_MARK).
void ptShadeEndPortal(bool viewMark) {
    vec3 viewDir = -mainRay.direction;

    PositionVertex p0 = g_hit.p0;
    PositionVertex p1 = g_hit.p1;
    PositionVertex p2 = g_hit.p2;

    // The hit on the ray (see the top of this file: the record has no translation).
    vec3 worldPos = g_hit.planeHitWorldPos;

    vec3 faceNormal = normalize(cross(p1.pos - p0.pos, p2.pos - p0.pos));
    // Where the point is in the world, not around the camera (the positions here are camera-relative): anchored to
    // the camera, the starfield slid along with every step and with the view bobbing while walking. Wrapped every
    // 1024 blocks, a whole number of the pattern's repeats, so far from the origin it keeps its precision.
    vec3 anchoredPos = worldPos + vec3(mod(worldUBO.cameraPos.xyz, dvec3(1024.0)));
    vec4 texProj0 = endPortalProjection(anchoredPos, faceNormal);
    vec3 color = computeEndPortalColor(texProj0, 15, worldUBO.endSkyTextureID, worldUBO.endPortalTextureID, worldUBO.gameTime);

    // The colour above is already what vanilla puts on the screen: a nearly black field with a scattering of
    // bright stars. Multiplying it turned the whole block into a flat glowing slab, which is what it is supposed
    // not to look like - the dark part of a portal is dark.
    // And it is a picture in screen colours, not light: taken as it is for light, its dark field came out a glowing
    // grey under the exposure. Converted to linear light, the field is near black again and only the stars shine.
    color = pow(max(color, vec3(0.0)), vec3(2.2));
    mainRay.radiance += color * mainRay.throughput;
    // It is a light source (level 15 in vanilla): a ray that comes to it off another surface takes a cool glow from
    // it besides its stars, so the frame and the room around a portal are lit teal. The camera sees only the
    // starfield; the glow stays off the portal itself, which would otherwise be a flat lit slab. So does a view off
    // a mirror or the water, which shows the portal as the camera would.
    if (!rayCameraPath(mainRay) && !viewMark) {
        mainRay.radiance += END_PORTAL_GLOW * worldUBO.emissionBrightness * mainRay.throughput;
    }
    mainRay.hitT = g_hit.hitT;
    // Its own face, and a surface that reflects nothing (fully rough, no reflectance) for the denoisers: with no
    // normal and no roughness given they took the starfield for a perfect mirror, and carried it along with the view
    // as they would a reflection.
    // faceNormal * objectToWorld is the closest hit's vec4(faceNormal, 0.0) * gl_ObjectToWorld3x4EXT: the record's
    // matrix is mat3(gl_ObjectToWorld3x4EXT).
    vec3 worldNormal = normalize(faceNormal * g_hit.objectToWorld);
    mainRay.normal = dot(worldNormal, viewDir) < 0.0 ? -worldNormal : worldNormal;
    ptStoreMaterial(vec4(0.0), vec3(0.0), 1.0, 0.0, 0.0, 1.0, 0.0);
    raySetNoisy(mainRay, false);
    mainRay.hasPrevScenePos = 0u;
    raySetStop(mainRay, true);
}

// ---- End gateway (world/hit/end_gateway.rchit)

void ptShadeEndGateway() {
    vec3 viewDir = -mainRay.direction;

    PositionVertex p0 = g_hit.p0;
    PositionVertex p1 = g_hit.p1;
    PositionVertex p2 = g_hit.p2;

    // The hit on the ray (see the top of this file).
    vec3 worldPos = g_hit.planeHitWorldPos;

    vec3 faceNormal = normalize(cross(p1.pos - p0.pos, p2.pos - p0.pos));
    // Anchored to the world, not the camera, as the End portal (ptShadeEndPortal).
    vec3 anchoredPos = worldPos + vec3(mod(worldUBO.cameraPos.xyz, dvec3(1024.0)));
    vec4 texProj0 = endPortalProjection(anchoredPos, faceNormal);
    vec3 color = computeEndPortalColor(texProj0, 15, worldUBO.endSkyTextureID, worldUBO.endPortalTextureID, worldUBO.gameTime);

    // add glowing radiance
    mainRay.radiance += 4.0 * pow(max(color, vec3(0.0)), vec3(2.2)) * mainRay.throughput;
    mainRay.hitT = g_hit.hitT;
    // A surface that reflects nothing, as the End portal (ptShadeEndPortal).
    vec3 worldNormal = normalize(faceNormal * g_hit.objectToWorld);
    mainRay.normal = dot(worldNormal, viewDir) < 0.0 ? -worldNormal : worldNormal;
    ptStoreMaterial(vec4(0.0), vec3(0.0), 1.0, 0.0, 0.0, 1.0, 0.0);
    raySetNoisy(mainRay, false);
    mainRay.hasPrevScenePos = 0u;
    raySetStop(mainRay, true);
}

// ---- the water patch inside a boat (world/hit/water_mask.rchit)

// The patch hides the water inside the hull: the ray steps past it and carries on as inside the boat. It stores no
// material (its old closest hit never wrote one), so g_lastMaterial keeps what the previous hit left.
void ptShadeWaterMask() {
    // The hit on the ray (see the top of this file). The old closest hit loaded the triangle only to place it.
    vec3 worldPos = g_hit.planeHitWorldPos;
    mainRay.origin = worldPos + mainRay.direction * 0.001;

    raySetInsideBoat(mainRay, true);
    // The boat floats in water: past its water patch the ray is below the water line, so what it sees through a
    // see-through hull is seen through water - absorbed and tinted like any other water, not as clear air.
    raySetInWater(mainRay, true);
    raySetContinue(mainRay, true);
    mainRay.hitT = g_hit.hitT;
    mainRay.coneWidth += g_hit.hitT * mainRay.coneSpread;
}

// ---- world_no_reflect (world/hit/no_reflect.rchit; no layer produces it today, kept for the group name)

void ptShadeNoReflect() {
    vec3 viewDir = -mainRay.direction;

    PositionVertex p0 = g_hit.p0;
    PositionVertex p1 = g_hit.p1;
    PositionVertex p2 = g_hit.p2;
    MaterialVertex m0 = g_hit.m0;
    MaterialVertex m1 = g_hit.m1;
    MaterialVertex m2 = g_hit.m2;

    vec3 baryCoords = g_hit.bary;
    vec3 worldPos = g_hit.rayOrigin + g_hit.rayDir * g_hit.hitT;
    uint coordinate = getCoordinate(m0.packedData);
    vec3 normal = baryCoords.x * m0.norm + baryCoords.y * m1.norm + baryCoords.z * m2.norm;
    if (coordinate == 1) {
        normal = normalize(mat3(worldUBO.cameraViewMatInv) * normal);
    } else {
        normal = normalize(normal);
    }

    bool useColorLayer = hasColorLayer(m0.packedData);
    vec4 colorLayerValue;
    if (useColorLayer) {
        colorLayerValue = baryCoords.x * m0.colorLayer + baryCoords.y * m1.colorLayer + baryCoords.z * m2.colorLayer;
    } else {
        colorLayerValue = vec4(1.0);
    }
    vec3 colorLayer = colorLayerValue.rgb;

    bool useTexture = hasTexture(m0.packedData);
    float albedoEmission =
        baryCoords.x * m0.albedoEmission + baryCoords.y * m1.albedoEmission + baryCoords.z * m2.albedoEmission;
    uint textureID = m0.textureID;
    uint alphaMode = getAlphaMode(m0.packedData);
    vec4 albedoValue;
    vec4 specularValue;
    vec4 normalValue;
    vec2 textureUV;
    if (useTexture) {
        int specularTextureID = mapping.entries[textureID].specular;
        int normalTextureID = mapping.entries[textureID].normal;
        textureUV = baryCoords.x * m0.textureUV + baryCoords.y * m1.textureUV + baryCoords.z * m2.textureUV;
        vec2 atlasUvMin = min(m0.textureUV, min(m1.textureUV, m2.textureUV));
        vec2 atlasUvMax = max(m0.textureUV, max(m1.textureUV, m2.textureUV));

        // ray cone
        float coneRadiusWorld = mainRay.coneWidth + g_hit.hitT * mainRay.coneSpread;
        vec3 dposdu, dposdv;
        computedposduDv(p0.pos, p1.pos, p2.pos, m0.textureUV, m1.textureUV, m2.textureUV, dposdu, dposdv);
        float lod = lodWithCone(textures[nonuniformEXT(textureID)], textureUV, coneRadiusWorld, dposdu, dposdv);

        albedoValue = sampleTexture(textures[nonuniformEXT(textureID)], textureUV, lod, false);
        albedoValue.a = resolveSurfaceAlpha(albedoValue.a * colorLayerValue.a, alphaMode);
        if (specularTextureID >= 0) {
            specularValue = sampleTexture(textures[nonuniformEXT(specularTextureID)], textureUV, lod, false);
        } else {
            specularValue = vec4(0.0);
        }
        if (normalTextureID >= 0) {
            normalValue = samplePBRTexture(textures[nonuniformEXT(normalTextureID)], textureUV, atlasUvMin, atlasUvMax,
                                           lod, VPT_PBR_SAMPLING_MODE);
        } else {
            normalValue = vec4(0.0);
        }
    } else {
        albedoValue = vec4(1.0);
        specularValue = vec4(0.0);
        normalValue = vec4(0.0);
    }

    bool useGlint = hasGlint(m0.packedData);
    uint glintTexture = m0.glintTexture;
    vec2 glintUV = baryCoords.x * m0.glintUV + baryCoords.y * m1.glintUV + baryCoords.z * m2.glintUV;
    glintUV = (worldUBO.textureMat * vec4(glintUV, 0.0, 1.0)).xy;
    vec3 glint = useGlint ? sampleTexture(textures[nonuniformEXT(glintTexture)], glintUV, false).rgb : vec3(0.0);
    glint = glint * glint;

    bool useOverlay = hasOverlay(m0.packedData);
    vec3 tint = vptSurfaceColour(albedoValue.rgb * colorLayer) + glint;
    if (useOverlay) {
        ivec2 overlayUV = m0.overlayUV;
        vec4 overlayColor = sampleTexture(textures[nonuniformEXT(worldUBO.overlayTextureID)], overlayUV, 0, false);
        tint = mix(overlayColor.rgb, vptSurfaceColour(albedoValue.rgb * colorLayer), overlayColor.a) + glint;
    }

    albedoValue = vec4(tint, albedoValue.a);
    LabPBRMat mat = convertLabPBRMaterial(albedoValue, specularValue, normalValue);

    // add glowing radiance
    float brightness = worldUBO.emissionBrightness;
    mainRay.radiance += 16.0 * brightness * tint * mat.emission * mainRay.throughput;
    mainRay.hitT = g_hit.hitT;
    mainRay.normal = vec3(0.0);
    ptStoreMaterial(albedoValue, mat.f0, mat.roughness, mat.metallic, mat.transmission, mat.ior, mat.emission);
    raySetNoisy(mainRay, false);
    mainRay.hasPrevScenePos = 0u;
    raySetStop(mainRay, true);
}

// ---- emissive sheets: lightning, beacon beam (world/hit/sheet.rchit)

void ptShadeSheet() {
    uint geometryBufferIndex = g_hit.geometryBufferIndex;

    PositionVertex p0 = g_hit.p0;
    PositionVertex p1 = g_hit.p1;
    PositionVertex p2 = g_hit.p2;
    MaterialVertex m0 = g_hit.m0;
    MaterialVertex m1 = g_hit.m1;
    MaterialVertex m2 = g_hit.m2;

    vec3 bary = g_hit.bary;

    vec4 colorLayer = hasColorLayer(m0.packedData) ?
                          (bary.x * m0.colorLayer + bary.y * m1.colorLayer + bary.z * m2.colorLayer) :
                          vec4(1.0);

    vec4 albedo = vec4(1.0);
    float pbrEmission = 0.0;
    if (hasTexture(m0.packedData)) {
        vec2 uv = bary.x * m0.textureUV + bary.y * m1.textureUV + bary.z * m2.textureUV;
        albedo = sampleTexture(textures[nonuniformEXT(m0.textureID)], uv, 0.0, false);

        int specularTextureID = mapping.entries[m0.textureID].specular;
        if (specularTextureID >= 0) {
            vec4 specular = sampleTexture(textures[nonuniformEXT(specularTextureID)], uv, 0.0, false);
            int intEmission = int(round(specular.a * 255.0));
            if (intEmission != 255) { pbrEmission = intEmission / 254.0; }
        }
    }

    vec4 shaded = albedo * colorLayer;
    float alpha = clamp(shaded.a, 0.0, 1.0);
    vec3 shadedRgb = clamp(shaded.rgb, vec3(0.0), vec3(1.0));
    vec3 transmittance = vec3(clamp(albedo.a, 0.0, 1.0));

    float factor = rayBounce(mainRay) == 0u ? 16.0 * VPT_DIRECT_LIGHT_STRENGTH : VPT_INDIRECT_LIGHT_STRENGTH;
    float brightness = worldUBO.emissionBrightness;
    mainRay.radiance += factor * brightness * shadedRgb * alpha * pbrEmission * mainRay.throughput;
    // Layers that are light themselves rather than lit surfaces carry their emission per vertex: glowing eyes,
    // beacon beams, lightning and the flames on a burning entity are all blended, so this is the only path they
    // ever reach.
    float vertexEmission = bary.x * m0.albedoEmission + bary.y * m1.albedoEmission + bary.z * m2.albedoEmission;
    mainRay.radiance += shadedRgb * alpha * vertexEmission * mainRay.throughput;
    mainRay.throughput *= transmittance;

    vec3 worldPos = g_hit.rayOrigin + g_hit.rayDir * g_hit.hitT;
    vec3 geoNormalObj = normalize(cross(p1.pos - p0.pos, p2.pos - p0.pos));
    mat3 normalMatrix = g_hit.normalMatrix;
    vec3 normal = normalize(normalMatrix * geoNormalObj);
    if (dot(normal, -mainRay.direction) < 0.0) { normal = -normal; }
    float defaultF0 = 0.02;
    float sqrtF0 = sqrt(defaultF0);
    float ior = (1.0 + sqrtF0) / max(1.0 - sqrtF0, 1e-6);
    float payloadTransmission = alpha < 0.999999 ? 1.0 : 0.0;

    mainRay.normal = normal;
    ptStoreMaterial(vec4(shadedRgb, alpha), vec3(defaultF0), 1.0, 0.0, payloadTransmission, ior, 0.0);
    raySetLobeType(mainRay, 0u);
    raySetNoisy(mainRay, false);
    mainRay.directLightRadiance = vec3(0.0);
    mainRay.hasPrevScenePos = 0u;
    if (rayBounce(mainRay) == 0u) {
        vec3 prevScenePos;
        // The same lookup as the closest hit's own copy of loadPreviousScenePos (pt/pt_direct.glsl): last frame's
        // position buffer through the recorded address, the instance's last matrix by its custom index.
        if (loadPreviousScenePos(geometryBufferIndex, g_hit.primitiveId, bary, prevScenePos)) {
            // A raindrop seen here was higher up the sheet last frame.
            if (isRainSurface(m0.packedData)) { prevScenePos.y += worldUBO.rainFallPerFrame; }
            mainRay.prevScenePos = prevScenePos;
            mainRay.hasPrevScenePos = 1u;
        }
    }

    mainRay.origin = worldPos + mainRay.direction * 0.001;
    mainRay.hitT = g_hit.hitT;
    mainRay.coneWidth += g_hit.hitT * mainRay.coneSpread;
    raySetContinue(mainRay, true);
    raySetStop(mainRay, false);
}

#endif // VPT_PT_SHADE_SPECIAL_GLSL
