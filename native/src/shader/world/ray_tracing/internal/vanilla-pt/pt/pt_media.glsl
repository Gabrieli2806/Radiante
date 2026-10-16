#ifndef VPT_PT_MEDIA_GLSL
#define VPT_PT_MEDIA_GLSL

// The water medium at a hit: the stretch of water the ray crossed to reach it, and (outside the Bedrock profile)
// the view passing through a vertical side of water. Moved out of the default closest-hit shader unchanged; the
// hit is read through g_hit.

// How far past a surface the view seen through it carries on from. Minecraft draws the side of water against glass
// (an aquarium window) a thousandth of a block inside the water: stepping two thousandths past the glass jumped over
// the water's face, so the view never entered the water. The sea behind every window showed no water at all - sky
// blue where the view came out through its surface, or what lay far beyond, sharp and untinted - while the same sea
// seen from above, through its top, was deep and dark.
const float PASS_THROUGH_STEP = 0.0002;

void ptWaterMediumAtHit(uint bounce) {
    // The stretch of water this ray just crossed. The camera's own stretch, when it is under water, is the ray
    // generation's to fog; the rays leaving the first surface it sees are still in the water.
    if (waterMediumActive()) {
        if (rayInWater(mainRay)) {
            vec3 waterTransmittance;
            vec3 waterScatter;
            // A ray that reached the water through a window is the camera's view of it as much as one that came
            // straight down from the air: counted by bounces it passed for light filling the water, and a tank
            // behind glass glowed with the open sky along every long sight line through it.
            uint mediumBounce = g_viewBounce || rayCameraPath(mainRay) ? min(bounce, 1u) : bounce;
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
            float waterDistance = VPT_MEDIUM_DISTANCE_CLAMP != 0 ? min(g_hit.hitT, 50.0) : g_hit.hitT;
            waterMediumSegmentBedrock(waterDistance, waterMediumBounceDensity(mediumBounce), g_hit.rayOrigin,
                                      g_hit.rayDir, waterTransmittance, waterScatter);
#else
            waterMediumSegment(g_hit.hitT, waterMediumBounceDensity(mediumBounce), waterTransmittance, waterScatter);
#endif
            float waterAmbient = max(waterMediumBounceAmbient(mediumBounce), 0.0);
            if (waterAmbient > 0.0) {
                // Some light still reaches under an overhang sideways through the water around it. Asked at a random
                // point of the stretch, towards a random direction near straight up, so that over frames it averages
                // the whole stretch and a soft cone of sky. Asked at the stretch's middle only, the sky's outline
                // above (a dome's ribs, the edge of a roof) was projected down into the water as sharp shapes that
                // slid and changed with every step the camera took.
                float skyAlong = rand(mainRay.seed);
                vec3 skyJitter = vec3(rand(mainRay.seed), rand(mainRay.seed), rand(mainRay.seed)) * 2.0 - 1.0;
                vec3 skyDirection = normalize(vec3(skyJitter.x * 0.5, 1.0, skyJitter.z * 0.5));
                waterAmbient *= mix(0.35, 1.0,
                                    skyVisibilityAlong(g_hit.rayOrigin + g_hit.rayDir * (g_hit.hitT * skyAlong),
                                                       skyDirection));
            }
            mainRay.radiance += mainRay.throughput * waterScatter * waterAmbient;
            mainRay.throughput *= waterTransmittance;
        } else if (bounce == 0u && skyUBO.cameraSubmersionType == 1 && !rayInGlass(mainRay)) {
            raySetInWater(mainRay, true);
        }
    }
}

#if !defined(VPT_BEDROCK_ORIGINAL) || VPT_BEDROCK_ORIGINAL == 0
// A side of the water (where a sheet of water stands against glass or an opening, as in a wall of aquarium
// windows) is seen straight through, as in Bedrock RTX: the view carries on into the water, clear and sharp,
// with what is behind it as the surface the denoiser sees. Refracting and shading it as a surface turned it
// a milky smear. The medium still colours the stretch beyond.
// Also for what the camera sees through a surface: through the top of the water down into a waterfall,
// or reflected in it. Shaded as a surface there, a thin column of falling water trapped the view between its
// sides (total internal reflection at a glancing angle, as in a light pipe) and showed flat sky blue instead
// of what lies behind; the same water seen from above as the sea is shows what lies under it.
// True when the view went through the side; the hit is then finished.
bool ptWaterSidePassThrough(bool isWaterMaterial, vec3 planeGeoNormal, vec3 baseGeoNormal, uint bounce) {
    bool viewThroughSide = bounce == 0u || rayCameraPath(mainRay) || g_viewBounce;
    if (isWaterMaterial && abs(planeGeoNormal.y) <= 0.75 && viewThroughSide) {
        mainRay.hitT = g_hit.hitT;
        mainRay.coneWidth += mainRay.hitT * mainRay.coneSpread;
        mainRay.origin = g_hit.rayOrigin + g_hit.rayDir * (g_hit.hitT + PASS_THROUGH_STEP);
        ptClearReflectionRequest();
        mainRay.hasPrevScenePos = 0u;
        // Where the view goes into the water from the air, the side is the same water surface the top is, and
        // looks the way the top does from above: clear, with the world reflected in it by Fresnel (rising at a
        // glancing view). The reflection is traced by the ray generation shader once the view behind is done,
        // as glass's is (ptRequestReflection).
        // Only on entering: a curtain of falling water is one body of water to look through, not a stack of
        // sheets. Shading every face it crossed, and drawing the pack's flowing texture on each, made it read
        // as layered, streaked glass with lines that flickered as the view moved.
        // The reflection is traced for the camera's own ray only; a view already reflected or refracted once
        // goes on through.
        if (!rayInWater(mainRay) && (bounce == 0u || rayCameraPath(mainRay))) {
            vec3 sideIncident = normalize(g_hit.rayDir);
            float sideCos = clamp(abs(dot(sideIncident, baseGeoNormal)), 0.0, 1.0);
            float sideFresnel = clamp(vptInterfaceFresnel(sideCos, 1.0 / 1.333), 0.0, 1.0);
            ptRequestReflection(reflect(sideIncident, baseGeoNormal), mainRay.throughput * sideFresnel, 0u);
            mainRay.throughput *= 1.0 - sideFresnel;
        }
        ptStoreMaterial(vec4(1.0), vec3(0.02), 1.0, 0.0, 1.0, 1.0, 0.0);
        raySetInWater(mainRay, !rayInWater(mainRay));
        raySetNoisy(mainRay, false);
        raySetContinue(mainRay, true);
        raySetPassThrough(mainRay, true);
        raySetStop(mainRay, false);
        return true;
    }
    return false;
}
#endif

#endif // VPT_PT_MEDIA_GLSL
