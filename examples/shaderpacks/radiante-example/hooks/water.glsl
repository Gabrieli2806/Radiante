#ifndef RADIANTE_HOOK_WATER_GLSL
#define RADIANTE_HOOK_WATER_GLSL

// Radiante Example Pack: red absorbed faster and blue slower than radiante-pt's water, so deep water turns teal sooner.
void hookWaterAbsorption(inout vec3 extinction) {
    extinction *= EXAMPLE_WATER_TINT;
}

#endif
