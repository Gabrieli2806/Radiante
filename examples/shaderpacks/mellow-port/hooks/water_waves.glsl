#ifndef RADIANTE_HOOK_WATER_WAVES_GLSL
#define RADIANTE_HOOK_WATER_WAVES_GLSL

// Mellow port: Mellow's water wave settings on Radiante's waves. Ported from Mellow by TheCMK (MIT, LICENSE-MELLOW):
// WATER_NORMAL_STRENGTH, WATER_NORMAL_SPEED and WATER_NORMAL_SIZE in shaders/lib/settings.glsl, which scale its
// water normals in noise_water (shaders/lib/noise.glsl): the slope, the scroll speed and the size of the pattern.
// They scale Radiante's own waves the same way (and the caustics under them), and Mellow's own normals when Water
// Normals is on (hooks/water_surface.glsl). 1 is Mellow's default for each.
#ifndef MELLOW_WATER_NORMAL_STRENGTH
#    define MELLOW_WATER_NORMAL_STRENGTH 1.0
#endif
#ifndef MELLOW_WATER_NORMAL_SPEED
#    define MELLOW_WATER_NORMAL_SPEED 1.0
#endif
#ifndef MELLOW_WATER_NORMAL_SIZE
#    define MELLOW_WATER_NORMAL_SIZE 1.0
#endif

void hookWaterWaves(inout float strength, inout float speed, inout float size) {
    strength *= float(MELLOW_WATER_NORMAL_STRENGTH);
    speed *= float(MELLOW_WATER_NORMAL_SPEED);
    size *= float(MELLOW_WATER_NORMAL_SIZE);
}

#endif
