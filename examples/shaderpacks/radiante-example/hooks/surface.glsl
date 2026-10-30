#ifndef RADIANTE_HOOK_SURFACE_GLSL
#define RADIANTE_HOOK_SURFACE_GLSL

// Radiante Example Pack: notes whether the hit is a leaf or a small plant, for hooks/direct_light.glsl.
bool g_exampleFoliage = false;

void hookSurface(inout SampledSurface surface, uint surfaceFlags, uint bounce) {
    g_exampleFoliage = isLeavesSurface(surfaceFlags) || isPlantSurface(surfaceFlags);
}

#endif
