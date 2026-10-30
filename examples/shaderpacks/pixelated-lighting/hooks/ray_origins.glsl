#ifndef RADIANTE_HOOK_RAY_ORIGINS_GLSL
#define RADIANTE_HOOK_RAY_ORIGINS_GLSL

// Pixelated Lighting: the shadow rays and the reflection rays of a hit start from the centre of its texture texel,
// which hookSurface (hooks/surface.glsl) works out; the hit is still lit where it is. Leaving
// RADIANTE_HOOK_RAY_ORIGINS_DEFAULT out of this file is what has radiante-pt ask for the offsets below (its
// hooks/ray_origins.glsl says what it does with them; the reflections of water and glass the camera sees come back
// in a fourth layer of material_state, which configs.json gives "depth": 4).
//
// PIXEL_LIGHT_SHADOWS and PIXEL_LIGHT_REFLECTIONS are live settings: read from the world uniform, so tested with if.

#ifndef PIXEL_LIGHT_SHADOWS
#    define PIXEL_LIGHT_SHADOWS 1
#endif
#ifndef PIXEL_LIGHT_REFLECTIONS
#    define PIXEL_LIGHT_REFLECTIONS 1
#endif

// From the hit being shaded to the centre of its texel; zero until hookSurface sets it, and with Light Texels at 0.
vec3 g_pixelLightOffset = vec3(0.0);

vec3 hookShadowOffset() {
    return int(PIXEL_LIGHT_SHADOWS) != 0 ? g_pixelLightOffset : vec3(0.0);
}

vec3 hookReflectionOffset() {
    return int(PIXEL_LIGHT_REFLECTIONS) != 0 ? g_pixelLightOffset : vec3(0.0);
}

// Sun and moon shadows: radiante-pt's spread a penumbra of about a degree, wider than a texel a few blocks from what
// casts them, so the squares melted into a gradient. PIXEL_LIGHT_SHADOW_SOFTNESS narrows it (0 hard, 1 radiante-pt's).
#ifndef PIXEL_LIGHT_SHADOW_SOFTNESS
#    define PIXEL_LIGHT_SHADOW_SOFTNESS 0.15
#endif

float hookShadowSoftness() {
    return int(PIXEL_LIGHT_SHADOWS) != 0 ? float(PIXEL_LIGHT_SHADOW_SOFTNESS) : 1.0;
}

#endif
