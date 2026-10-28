#ifndef RADIANTE_HOOK_SKY_GLSL
#define RADIANTE_HOOK_SKY_GLSL

// Radiante Example Pack: the clear sky tinted (a cooler blue by default), against the warmer grade.
void hookSky(inout vec3 radiance, vec3 direction) {
    radiance *= EXAMPLE_SKY_TINT;
}

#endif
