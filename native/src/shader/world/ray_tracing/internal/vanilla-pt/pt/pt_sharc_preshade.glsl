#ifndef VPT_PT_SHARC_PRESHADE_GLSL
#define VPT_PT_SHARC_PRESHADE_GLSL

// SHaRC radiance cache, pre-shade order (PT_SHARC_QUERY_ORDER_PRESHADE, world.rgen; off by default). A deep bounce
// asks the cache for the light leaving the surface it reached before that surface is shaded, and when the cache
// answers the path ends there: the material, the direct light (the sun's shadow ray, up to sixteen block light rays)
// and the BSDF sample are never computed for it. In the legacy order (pt/pt_segment.glsl) the surface was shaded
// first, its emission and direct light were added, the throughput went through its BSDF, and only then did the cache
// answer for the light beyond: the same bounce adds a different amount of light in the two orders, so this one is
// not the legacy image, and is kept opt-in until a parity run against the Bedrock reference signs it off.
//
// Without the shading, the lookup has only what the hit itself gives: where it is, the triangle's own normal, and
// the ray that arrived (its lobe and the roughness of the surface it left). The update paths
// (pt/pt_sharc_update.glsl) file their hits under the same normal in this order, so lookups find what they store.
//
// Needs the hit context (pt/pt_hit_context.glsl), the water medium (pt/pt_media.glsl) and the query rule
// (pt/pt_sharc.glsl) before it.

#if PT_SHARC_PRESHADE
// The normal a hit is filed under in the cache in the pre-shade order, by the update paths and the lookups alike:
// the triangle's own normal (the one SHaRC asks for: the shading normal is not known before shading), turned to face
// the ray that reached it, so both sides of a thin wall stay apart. Read from g_hit, which holds the step's hit.
vec3 ptSharcHitNormal() {
    vec3 normal = normalizeF(g_hit.normalMatrix * cross(g_hit.p1.pos - g_hit.p0.pos, g_hit.p2.pos - g_hit.p0.pos),
                             vec3(0.0, 1.0, 0.0));
    return dot(normal, g_hit.rayDir) > 0.0 ? -normal : normal;
}
#endif

#if PT_SHARC_PRESHADE && SHARC_QUERY
// What the bounce loop (pt/pt_segment.glsl) tells the step about the cache before each trace, and what it learns
// back. Set for every step, the camera's included (which never asks).
struct PtSharcPreShade {
    bool query;              // the step may take the cache (where the legacy order asked it, see pt_segment)
    bool deepBounce;         // sharcAllowQuery's deepBounce: a diffuse arrival takes the cache however close
    float arrivingRoughness; // roughness of the surface the ray left by its lobe (the cone the arrival spreads in)
    bool cacheHit;           // out: the cache answered and the path ended at this hit
};

PtSharcPreShade g_sharcPreShade;

void ptSharcPreShadeReset() {
    g_sharcPreShade.query = false;
    g_sharcPreShade.deepBounce = false;
    g_sharcPreShade.arrivingRoughness = 1.0;
    g_sharcPreShade.cacheHit = false;
}

/**
 * Asks the cache for the light leaving the hit in g_hit before it is shaded. True when the cache answered: the
 * light reaching the hit times the cached radiance is added, and the path ends.
 * kind: the hit's PT_KIND_*; viewMark: the ray belongs to a view chain; arrivingLobe: the lobe the ray left its last
 * surface by (0 diffuse, 1 reflection, 2 refraction).
 */
bool ptSharcPreShadeQuery(uint kind, bool viewMark, uint arrivingLobe) {
    if (!g_sharcPreShade.query) return false;
    // Only surfaces shaded by the default shading. Clouds, the End portal and gateway, the boat's water patch and
    // the sheets are their own kind of hit (some carry the ray on, some end it with their own light) and are shaded
    // as always.
    if (kind != PT_KIND_DEFAULT && kind != PT_KIND_NO_HEIGHT) return false;
    // Never where the ray may only pass through, which the shading decides and the cache must not answer for (the
    // legacy order asks only after a hit that was not passed through). Without shading that is known only roughly,
    // so every hit that could be one is left out: anything a view sees (the side of water, glass and mirrors for
    // the camera's own view and view chains), and under the Bedrock profile every glass and blended surface, which
    // light on its way crosses straight on.
    if (viewMark || rayCameraPath(mainRay)) return false;
#    if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    if (isGlassSurface(g_hit.packedData) || isBlendSurface(g_hit.packedData)) return false;
#    endif

    SharcParameters sharcParameters = makeSharcParameters();
    vec3 positionWorld = g_hit.planeHitWorldPos + vec3(worldUBO.cameraPos.xyz);
    // The legacy rule with what the arrival gives: its lobe, and the roughness of the surface that sent it (the
    // cone a glossy reflection spreads in is that surface's, so both describe the same scattering). The legacy order
    // used the lobe this hit would leave by and this hit's roughness, both known only after shading it.
    if (!sharcAllowQuery(false, g_sharcPreShade.deepBounce, g_hit.hitT, g_sharcPreShade.arrivingRoughness,
                         arrivingLobe, sharcParameters, positionWorld)) {
        return false;
    }
    SharcHitData sharcHitData;
    sharcHitData.positionWorld = positionWorld;
    sharcHitData.normalWorld = ptSharcHitNormal();
    vec3 cachedRadiance = vec3(0.0);
    if (!SharcGetCachedRadiance(sharcParameters, sharcHitData, cachedRadiance, false)) return false;

    // The light that reaches the surface: through the water the ray crossed to get there, as the shading would have
    // taken it first (its transmittance and the light scattered into the stretch). viewMark is false here, so the
    // view flag it reads is the one ptBeginRaygenShade cleared.
    ptWaterMediumAtHit(rayBounce(mainRay));
    mainRay.radiance += cachedRadiance * mainRay.throughput * VPT_SHARC_PRESHADE_GAIN;
    mainRay.hitT = g_hit.hitT;
    raySetStop(mainRay, true);
    g_sharcPreShade.cacheHit = true;
    return true;
}
#endif

#endif // VPT_PT_SHARC_PRESHADE_GLSL
