#ifndef RADIANTE_HOOK_WATER_SURFACE_GLSL
#define RADIANTE_HOOK_WATER_SURFACE_GLSL

// First Light: the mirror-like water of the showcases, where lakes and pools held a sharp image of the sky, the trees
// and the lights. The waves' normals are drawn back towards the flat face and the roughness lowered by FL_WATER_MIRROR,
// so the reflection is calm and clear; the water's colour, depth and absorption stay radiante-pt's. Not defining
// RADIANTE_HOOK_WATER_SURFACE_DEFAULT here turns the call on.
#ifndef FL_WATER_MIRROR
#    define FL_WATER_MIRROR 0.6
#endif

void hookWaterSurface(inout SampledSurface surface, vec3 position, float waveHeight) {
    float mirror = clamp(float(FL_WATER_MIRROR), 0.0, 1.0);
    if (mirror <= 0.0) { return; }
    surface.shadingNormal = normalize(mix(surface.shadingNormal, surface.geometricNormal, 0.6 * mirror));
    surface.mat.roughness *= 1.0 - 0.8 * mirror;
}

#endif
