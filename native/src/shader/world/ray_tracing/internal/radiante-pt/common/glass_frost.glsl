#ifndef VPT_GLASS_FROST_GLSL
#define VPT_GLASS_FROST_GLSL

// How far the frost on glass has grown at this hit, for the any-hit shaders: radiante-pt's growth with the snow
// (glassFrostGrowth, util/alpha_mode.glsl), as a pack's hooks/glass_frost.glsl changes it. Needs skyUBO declared and
// an any-hit stage (the hit's position). VPT_GLASS_FROST_GROWTH() is radiante-pt's own expression without a pack's
// hook: even a function nothing else needs changes the order glslang declares types in, and radiante-pt's shaders
// stay the same as before the hook existed.

#include "hooks/glass_frost.glsl"

#ifdef RADIANTE_HOOK_GLASS_FROST_DEFAULT
#    define VPT_GLASS_FROST_GROWTH() glassFrostGrowth(skyUBO.rainWetness.x)
#else
float vptGlassFrostGrowth() {
    float growth = glassFrostGrowth(skyUBO.rainWetness.x);
    hookGlassFrost(growth, gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT);
    return clamp(growth, 0.0, 1.0);
}
#    define VPT_GLASS_FROST_GROWTH() vptGlassFrostGrowth()
#endif

#endif
