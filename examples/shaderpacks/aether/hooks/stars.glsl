#ifndef RADIANTE_HOOK_STARS_GLSL
#define RADIANTE_HOOK_STARS_GLSL

// Aether: a dense, coloured, twinkling star field in place of Radiante PT's stars, under the pack's clouds. Two
// layers (a sparse one with the bright stars, a dense faint one, denser still along the milky way), fixed to the
// night sky's own frame so they turn with the sun across the night (aetherCelestial). They come out through blue hour
// and fade towards the horizon, where they twinkle the most, and behind the rain. The milky way itself and the faint
// glow round it are in hooks/sky_emission.glsl, since they also light the world a little.
//
// Brightness: on Radiante PT's night sky (aetherSkyReference), so they keep their place against it whatever the
// exposure. Only the camera's sky calls this hook, so the stars' detail never reaches the light the denoiser cleans.
#include "aether/aether_sky.glsl"
#include "aether/aether_stars.glsl"

float hookStars(inout vec3 radiance, vec3 direction) {
    float stars = clamp(float(AETHER_STARS), 0.0, 3.0);
    if (stars <= 0.0) { return 1.0; }
    AetherTime t = aetherTime();
    float visible = (t.night + 0.3 * t.blue) * smoothstep(-0.02, 0.16, direction.y) * (1.0 - radianteRain());
    if (visible > 0.001) {
        vec3 celestial = aetherCelestial(direction);
        float band = exp(-pow(dot(celestial, AETHER_GALAXY_POLE) / 0.16, 2.0));
        float time = mod(aetherSeconds(), 3600.0);
        float twinkle = 0.35 + 0.65 * (1.0 - smoothstep(0.05, 0.5, direction.y));
        vec3 light = aetherStarLayer(celestial, 110.0, 0.10, 17u, time, twinkle);
        light += 0.45 * aetherStarLayer(celestial, 170.0, 0.07 + 0.10 * band, 71u, time, twinkle);
        // The faintest a little over the night sky's own brightness, the brightest some sixty times it.
        radiance += light * (24.0 * aetherSkyReference(true) * stars * visible);
    }
    // Radiante PT's own stars are hidden: these take their place.
    return 0.0;
}

#endif
