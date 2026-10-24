#ifndef VPT_PT_FOG_GLSL
#define VPT_PT_FOG_GLSL

// Fog settings, the state of the glass the camera looked through (which tints the fog behind it), and the small
// helpers the fog and the outputs share (finite checks, the half float clamp). Moved verbatim out of world.rgen.

// Under water: the distance (blocks) over which the view fades to about a third, and how bright the water fog is
// against the sky's light.
#ifndef UNDERWATER_VISIBILITY
#    define UNDERWATER_VISIBILITY 22.0
#endif
#ifndef UNDERWATER_FOG_TINT
#    define UNDERWATER_FOG_TINT vec3(0.03, 0.2, 0.42)
#endif
#ifndef UNDERWATER_FOG_LIGHT
#    define UNDERWATER_FOG_LIGHT 0.35
#endif
// How fast the water's own glow darkens with the camera's depth, in visibilities: at 0.5, twenty blocks down in
// water one sees 22 blocks in, about two thirds of it is left.
#ifndef UNDERWATER_DEPTH_DIMMING
#    define UNDERWATER_DEPTH_DIMMING 0.5
#endif

// The Nether's distance fog and the sky it leaves behind, at this share of vanilla's fog colour. The fog is light here,
// and a cavern lit by lava alone is metered up to the brightest exposure (64 times): vanilla's dark red (0.2) came out
// as a flat pink sky.
#ifndef NETHER_FOG_SCALE
#    define NETHER_FOG_SCALE 0.012
#endif

// The Nether's counterpart: how far the eye is held below what the meter would bring it to, so a cavern lit by lava
// stays a dim cavern with a dark red haze. Lava is kept orange by holding down what it shows (NETHER_GLOW_SCALE,
// pt_material.glsl), not by the eye: held as low as lava asked (0.15), torchlit rock was near black.
#ifndef NETHER_EXPOSURE_SCALE
#    define NETHER_EXPOSURE_SCALE 0.25
#endif

// The Bedrock profile's picture by day, against what the meter would bring it to.
#ifndef BEDROCK_DAY_EXPOSURE_SCALE
#    define BEDROCK_DAY_EXPOSURE_SCALE 0.85
#endif

#ifndef END_EXPOSURE_SCALE
#    define END_EXPOSURE_SCALE 0.4
#endif

// The colour of the glass (and sheets of water) the camera's ray passed straight through before what it shows; the
// fog laid over the pixel afterwards lies mostly beyond that glass, so it is tinted by it as well.
vec3 g_passThroughTint = vec3(1.0);
// What that glass added on the way (its faint reflection of the sky, the streaks of a sheet of water).
vec3 g_passThroughRadiance = vec3(0.0);
// How far along the camera's ray that glass is. Only the fog beyond it is seen through it: the fog between the camera
// and a far window is not, and tinting all of it left distant stained and clear glass standing out of the haze,
// dark and coloured, instead of fading into it like the blocks around.
float g_passThroughDistance = 0.0;

// The tint of the glass the camera looked through, for fog that reaches fogDistance along the ray: the share of the
// fog in front of the glass is left untinted.
vec3 fogGlassTint(float fogDistance) {
    vec3 tint = clamp(g_passThroughTint, vec3(0.0), vec3(1.0));
    if (g_passThroughDistance <= 0.0 || fogDistance >= INF_DISTANCE * 0.5) { return tint; }
    float inFront = clamp(g_passThroughDistance / max(fogDistance, 1e-3), 0.0, 1.0);
    return mix(tint, vec3(1.0), inFront);
}
// The first glass the camera's ray passed through asks for its reflection of the world (Bedrock Fresnel): where it
// starts, which way it goes, and how much of it is seen. Traced after the view behind (glassReflection).
bool g_glassReflection = false;
vec3 g_glassReflectionOrigin = vec3(0.0);
vec3 g_glassReflectionDirection = vec3(0.0);
vec3 g_glassReflectionWeight = vec3(0.0);
uint g_glassReflectionMedium = 0u;

void applyFog(vec3 radiance,
              float t,
              float fogStart,
              float dBase,
              float dFog,
              vec3 fogRadiance,
              out vec3 foggedRadiance,
              out float transmittance,
              out vec3 additiveRadiance) {
    float opticalDepth = 0.0;

    if (t <= fogStart) {
        opticalDepth = t * dBase;
    } else {
        float distOutside = fogStart;
        float distInside = t - fogStart;
        opticalDepth = (distOutside * dBase) + (distInside * dFog);
    }

    transmittance = exp(-opticalDepth);
    additiveRadiance = fogRadiance * (1.0 - transmittance) * fogGlassTint(t);
    foggedRadiance = radiance * transmittance + additiveRadiance;
}

bool finiteFloat(float value) {
    return !isnan(value) && !isinf(value);
}

bool finiteVec3(vec3 value) {
    return !any(isnan(value)) && !any(isinf(value));
}

// Radiance as it may be written to the half float images the denoiser and the tone mapper read: nothing that is not a
// number, and nothing past what a half float holds. A sun glint off smooth ice or the core of a beacon went over
// that, was stored as infinity, and came out of the tone mapper (infinity divided by infinity) as a black dot in
// the brightest spot of the picture.
const float STORED_RADIANCE_LIMIT = 60000.0;

vec3 storedRadiance(vec3 value) {
    return vec3(isnan(value.r) ? 0.0 : clamp(value.r, 0.0, STORED_RADIANCE_LIMIT),
                isnan(value.g) ? 0.0 : clamp(value.g, 0.0, STORED_RADIANCE_LIMIT),
                isnan(value.b) ? 0.0 : clamp(value.b, 0.0, STORED_RADIANCE_LIMIT));
}

#endif // VPT_PT_FOG_GLSL
