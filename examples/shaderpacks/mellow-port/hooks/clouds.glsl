#ifndef RADIANTE_HOOK_CLOUDS_GLSL
#define RADIANTE_HOOK_CLOUDS_GLSL

// Mellow port: Mellow's clouds as the sky's cloud layer, drawn after Radiante greys the sky in rain and after the sun
// and moon, so they cover the sun and moon as in Mellow and stay in rain, thicker and greyer, as Mellow's do. Every
// ray that leaves the world sees them, so they show in reflections and light the world. The clouds themselves are
// in mellow/mellow_clouds.glsl (from shaders/global/atmosphere/clouds.glsl of Mellow by TheCMK, MIT, LICENSE-MELLOW).
#include "mellow/mellow_clouds.glsl"

vec4 hookCloudLayer(vec3 direction) {
    return mellowCloudLayer(direction);
}

#endif
