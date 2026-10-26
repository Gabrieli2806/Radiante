#ifndef VPT_PT_OUTPUTS_GLSL
#define VPT_PT_OUTPUTS_GLSL

// Output helpers: the glow outline of glowing entities, traced through the shadow hit group. Moved verbatim out of
// world.rgen.

// Glowing effect: glowing entities have a copy in their outline colour under GLOW_OUTLINE_MASK, which nothing but
// these rays sees. They go through the shadow hit group with the query flag set, and its any-hit hands back the
// copy's colour. Walls are not in the mask, so the outline shows through them like vanilla's.
#define GLOW_OUTLINE_RADIANCE 2.0

bool traceGlowOutline(vec2 pixelPos, vec2 resolution, vec3 origin, out vec3 color) {
    vec2 ndc = pixelPos / resolution * 2.0 - 1.0;
    vec4 viewNear = worldUBO.cameraProjMatInv * vec4(ndc, 0.0, 1.0);
    viewNear /= viewNear.w;
    vec3 direction = normalize(vec3(worldUBO.cameraEffectedViewMatInv * vec4(viewNear.xyz, 0.0)));

    shadowRay.radiance = vec3(0.0);
    shadowRay.throughput = vec3(1.0);
    shadowRay.insideBoat = 0u;
    shadowRay.pad0 = GLOW_OUTLINE_QUERY;
    traceRayEXT(topLevelAS, gl_RayFlagsNoOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT, GLOW_OUTLINE_MASK, 0, 0,
                0, origin, 0.01, direction, 512.0, 1);
    shadowRay.pad0 = 0u;
    color = shadowRay.radiance;
    return shadowRay.throughput.x < 0.0;
}

// Vanilla draws only the rim: a pixel outside the silhouette with the silhouette within a couple of pixels.
bool glowOutline(vec2 pixelCenter, vec2 resolution, vec3 origin, out vec3 color) {
    color = vec3(0.0);
    if (traceGlowOutline(pixelCenter, resolution, origin, color)) { return false; }
    float radius = max(1.0, resolution.y / 540.0);
    for (int i = 0; i < 8; i++) {
        float angle = float(i) * (PI / 4.0);
        if (traceGlowOutline(pixelCenter + vec2(cos(angle), sin(angle)) * radius, resolution, origin, color)) {
            return true;
        }
    }
    return false;
}

#endif // VPT_PT_OUTPUTS_GLSL
