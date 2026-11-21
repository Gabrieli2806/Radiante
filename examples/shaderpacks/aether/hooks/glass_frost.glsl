#ifndef RADIANTE_HOOK_GLASS_FROST_GLSL
#define RADIANTE_HOOK_GLASS_FROST_GLSL

#include "common/pack_world.glsl"
#include "aether/aether_settings.glsl"

// Aether: frost on windows on a clear cold night. Radiante PT grows the frost on glass only while it snows; here it
// also creeps in through the night where it is cold enough to snow, and goes again as the sun climbs, the way a
// window frosts before dawn. AETHER_NIGHT_FROST (live) is how far it grows by night, 0 leaves it to the snow. The
// snow still grows it the whole way, and the frost stays thin by a lamp, as Radiante PT lays it.
//
// The night and the cold are the camera's (common/pack_world.glsl): every window in view frosts together, as the
// frost of one night would.
void hookGlassFrost(inout float growth, vec3 position) {
    float nightFrost = clamp(float(AETHER_NIGHT_FROST), 0.0, 1.0);
    if (nightFrost <= 0.0) { return; }
    float clearColdNight = radianteNight() * radianteBiomeSnowy() * (1.0 - radianteRain());
    growth = max(growth, nightFrost * clearColdNight);
}

#endif
