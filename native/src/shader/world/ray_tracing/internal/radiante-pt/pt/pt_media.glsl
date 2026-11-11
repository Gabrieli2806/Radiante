#ifndef VPT_PT_MEDIA_GLSL
#define VPT_PT_MEDIA_GLSL

// The water medium at a hit: the stretch of water the ray crossed to reach it, and
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
            float waterDistance = VPT_MEDIUM_DISTANCE_CLAMP != 0 ? min(g_hit.hitT, 50.0) : g_hit.hitT;
            waterMediumSegmentBedrock(waterDistance, waterMediumBounceDensity(mediumBounce), g_hit.rayOrigin,
                                      g_hit.rayDir, waterTransmittance, waterScatter);
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
                // The light scattered in the water is the sky's and the sun's (waterMediumSegment*), so it is there
                // only in sight of the sky: left to a ray traced as the hit shader ends (see PT_SHADOW_SKY), with
                // the throughput of now. A share of it used to be added under a roof as well, and a pool in a
                // closed cave glowed pale blue at night with no light anywhere near it (0.35 of the open sky's).
                vec3 ambientLight = mainRay.throughput * waterScatter * waterAmbient;
                // Started 0.0099 along: deferred rays begin at 0.0001, skyVisibilityAlong's at 0.01.
                ptDeferShadowRay(PT_SHADOW_SKY,
                                 g_hit.rayOrigin + g_hit.rayDir * (g_hit.hitT * skyAlong) + skyDirection * 0.0099,
                                 skyDirection, 256.0 - 0.0099, WORLD_MASK, SKY_VISIBILITY_QUERY, ambientLight);
                g_shadowInsideBoat[PT_SHADOW_SKY] = 0u;
            }
            mainRay.throughput *= waterTransmittance;
        } else if (bounce == 0u && skyUBO.cameraSubmersionType == 1 && !rayInGlass(mainRay)) {
            raySetInWater(mainRay, true);
        }
    }
}


#endif // VPT_PT_MEDIA_GLSL
