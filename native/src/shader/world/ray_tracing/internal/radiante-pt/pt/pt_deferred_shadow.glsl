#ifndef VPT_PT_DEFERRED_SHADOW_GLSL
#define VPT_PT_DEFERRED_SHADOW_GLSL

// Shadow rays a hit shader's light sampling leaves to be traced as the shader ends (ptTraceDeferredShadowRays in
// pt_direct.glsl, called from the closest hit's main). A ray traced from the middle of the shading has to keep everything the shading still
// needs across the trace; a closest hit traces one to the sun or moon, one to a block light and one per held light,
// each from deep inside it, and in a room lit by a torch the block light's ray alone cost a fifth of the path tracing
// (15 ms against 12 at 1280x720 on an RTX 5070) although it is short and cheap to traverse. Traced last, almost
// nothing is left to keep. The light is the same: each ray, its origin, direction, length and query are the ones the
// sampling chose, the random numbers are drawn in the same order, and the light is added once the ray says how much
// of it arrives.
//
// The shading turns this on (g_deferShadowRays) only around the one light sampling whose rays its main traces: the
// other callers (a blended face's own light, pt_direct.glsl) trace theirs at once, as before.

// A pack's shadow and reflection ray origins (hooks/ray_origins.glsl); radiante-pt's compiles out.
#include "hooks/ray_origins.glsl"

const uint PT_SHADOW_CELESTIAL = 0u; // the sun or the moon (sampleSurfaceDirectLight)
const uint PT_SHADOW_BLOCK = 1u;     // the block light picked (sampleBlockLight, sampleBlockLightRestir)
const uint PT_SHADOW_HELD = 2u;      // the main hand's light (sampleOneHeldLight)
const uint PT_SHADOW_OFF_HAND = 3u;  // the off hand's
// Whether the open sky is in sight from a point of the stretch of water the ray crossed (ptWaterMediumAtHit): its
// light goes to the radiance only, not to the direct light. It is the stretch's sky, asked for every hit under water,
// and traced at the start of the shading it cost the frame under water more than the god rays did (6 to 7 ms at
// 1920x1080 with DLSS Quality on an RTX 5070, against 4 for all eight of the god rays' steps).
const uint PT_SHADOW_SKY = 4u;
const int PT_SHADOW_SLOTS = 5;

bool g_deferShadowRays = false;
// What the shadow rays of the surface being shaded take for the player's own model. The first person hand is that
// model's arm: left in, the invisible body and arm round the camera shaded the hand it belongs to (a black patch on
// it), so the hand's rays leave the model out (ptShadeDefault).
uint g_shadowPlayerMask = PLAYER_MASK;
bool g_shadowPending[PT_SHADOW_SLOTS] = bool[PT_SHADOW_SLOTS](false, false, false, false, false);
vec3 g_shadowOrigin[PT_SHADOW_SLOTS];
vec3 g_shadowDirection[PT_SHADOW_SLOTS];
float g_shadowLength[PT_SHADOW_SLOTS];
uint g_shadowMask[PT_SHADOW_SLOTS];
uint g_shadowQuery[PT_SHADOW_SLOTS];
uint g_shadowInsideBoat[PT_SHADOW_SLOTS];
// What the light that arrives is multiplied by: everything the sampling had, the path throughput included.
vec3 g_shadowScale[PT_SHADOW_SLOTS];
// ReSTIR stored this pixel's reservoir before its ray was traced: an occluded winner has its weight cleared after
// (sampleBlockLightRestir keeps none for an occluded light).
bool g_shadowRestirStored = false;
uint g_shadowRestirHalf = 0u;
// The deferred light also goes into the direct light the ray generation reads (mainRay.directLightRadiance), unless
// the shading replaced that after sampling it (water does).
bool g_deferredToDirect = false;

void ptDeferShadowRay(uint slot, vec3 origin, vec3 direction, float rayLength, uint mask, uint query, vec3 scale) {
#ifndef RADIANTE_HOOK_RAY_ORIGINS_DEFAULT
    // A pack's shadow origin (hooks/ray_origins.glsl): the ray starts that far away, aimed at the same point of a
    // block or held light; the sun's and moon's keep their direction. The open sky of a stretch of water is not the
    // surface's shadow and stays.
    if (slot != PT_SHADOW_SKY) {
        vec3 offset = hookShadowOffset();
        if (dot(offset, offset) > 0.0) {
            vec3 moved = origin + offset;
            if (slot != PT_SHADOW_CELESTIAL) {
                vec3 toLight = origin + direction * rayLength - moved;
                float distanceToLight = length(toLight);
                if (distanceToLight > 1e-4) {
                    direction = toLight / distanceToLight;
                    rayLength = distanceToLight;
                }
            }
            origin = moved;
        }
    }
    // How soft the sun's and moon's shadows are (1 keeps radiante-pt's): the ray is turned from its jittered
    // direction towards the body's centre. Sharp shadows show a pack's moved shadow origins as solid squares; at
    // radiante-pt's softness the penumbra spreads over several texels and the steps blur away.
    if (slot == PT_SHADOW_CELESTIAL && worldUBO.skyType == 1) {
        float softness = clamp(hookShadowSoftness(), 0.0, 1.0);
        if (softness < 1.0) {
            vec3 body = celestialSunDirection();
            if (body.y < 0.0) { body = celestialMoonDirection(); }
            direction = normalize(mix(body, direction, softness));
        }
    }
#endif
    g_shadowPending[slot] = true;
    g_shadowOrigin[slot] = origin;
    g_shadowDirection[slot] = direction;
    g_shadowLength[slot] = rayLength;
    g_shadowMask[slot] = mask;
    g_shadowQuery[slot] = query;
    g_shadowInsideBoat[slot] = rayInsideBoat(mainRay) ? 1u : 0u;
    g_shadowScale[slot] = scale;
}

#endif // VPT_PT_DEFERRED_SHADOW_GLSL
