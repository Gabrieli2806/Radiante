#ifndef RADIANTE_HOOK_SKY_EMISSION_GLSL
#define RADIANTE_HOOK_SKY_EMISSION_GLSL

// Aether: the milky way and a dim nebula glow round it, which light the world a little at night. A band across the
// night sky, warmer and wider towards its bright centre, clumped, with a dark lane of dust along it and pink and teal
// veils of nebula near it; it turns with the stars (aetherCelestial). It comes out at night and late blue hour,
// fades towards the horizon, behind rain, and by half under a full moon high in the sky, which washes it out. Every
// ray reaching the sky sees it, so it is light as well as a picture: faintly cooler, brighter nights on open ground.
//
// The camera's sky (seen) gets the detail, from the noise image. Rays that bounced off the world reach the sky many
// times per pixel in every direction, so they get the band's smooth average instead, with no clumps, dust or
// veils: the light on the ground is that average anyway, and detail there would only be noise for the denoiser.
// Night Sky Light sets how strongly that average lights the world. Brightness is on Radiante PT's night sky
// (aetherSkyReference), so it stays faint at any exposure; the pack's clouds, drawn after this hook, cover it.
#include "aether/aether_sky.glsl"
#include "aether/aether_noise.glsl"
#include "aether/aether_stars.glsl"

// A night-sky direction in galactic terms: x the sine of the latitude above the band, y the longitude in radians,
// 0 at the bright centre.
vec2 aetherGalactic(vec3 celestial) {
    vec3 centre = normalize(cross(AETHER_GALAXY_POLE, vec3(0.0, 0.0, 1.0)));
    vec3 across = cross(AETHER_GALAXY_POLE, centre);
    return vec2(dot(celestial, AETHER_GALAXY_POLE), atan(dot(celestial, across), dot(celestial, centre)));
}

vec3 aetherMilkyWay(vec3 celestial, bool seen) {
    vec2 galactic = aetherGalactic(celestial);
    float latitude = galactic.x;
    float longitude = galactic.y;
    float core = exp(-longitude * longitude / 1.2);
    float width = 0.09 + 0.07 * core;
    float band = exp(-latitude * latitude / (width * width));
    vec3 colour = mix(vec3(0.72, 0.80, 1.00), vec3(1.00, 0.86, 0.70), core);
    float body;
    vec3 nebula;
    if (!seen) {
        // The averages of the detail below: clumps, grain and dust come to about 0.62, the veils to 0.1.
        body = band * 0.62;
        nebula = vec3(0.55, 0.50, 0.80) * (0.10 * exp(-latitude * latitude / 0.08));
    } else {
        // Three times round the noise image per turn of the band, so the seam at the far side meets itself; every
        // scale along it below is a whole multiple of that.
        vec2 uv = vec2(longitude * (3.0 / (2.0 * AETHER_PI)), latitude * 1.6);
        float clumps = aetherNoiseAt(uv + vec2(0.13, 0.41)).r;
        float grain = aetherNoiseAt(uv * 3.0 + vec2(0.7, 0.2)).b;
        float laneShift = (aetherNoiseAt(uv * 2.0 + vec2(0.05, 0.6)).g - 0.5) * 0.04;
        float lane = exp(-pow((latitude - 0.012 - laneShift) / (0.022 + 0.02 * core), 2.0));
        float dust = 1.0 - 0.7 * lane * smoothstep(0.3, 0.7, aetherNoiseAt(uv * 2.0 + vec2(0.5, 0.5)).g);
        body = band * (0.30 + 0.85 * clumps * clumps + 0.25 * grain) * dust;
        // Veils from the streaky channel, laid along the band.
        float veil = smoothstep(0.55, 0.9, aetherNoiseAt(vec2(uv.y * 2.0 + 0.3, uv.x)).a) *
                     exp(-latitude * latitude / 0.05);
        vec3 tint = mix(vec3(0.85, 0.32, 0.72), vec3(0.28, 0.72, 0.80), aetherNoiseAt(uv * 2.0 + vec2(0.8, 0.1)).r);
        nebula = tint * (0.35 * veil);
    }
    return colour * (body * (0.55 + 0.45 * core)) + nebula;
}

vec3 hookSkyEmission(vec3 direction, bool seen) {
    float milkyWay = clamp(float(AETHER_MILKY_WAY), 0.0, 2.0);
    if (milkyWay <= 0.0 || direction.y <= -0.02) { return vec3(0.0); }
    AetherTime t = aetherTime();
    float night = t.night + 0.15 * t.blue;
    if (night <= 0.001) { return vec3(0.0); }
    float moonUp = smoothstep(0.0, 0.3, radianteMoonDirection().y) * radianteMoonLit();
    float visible = night * (1.0 - 0.5 * moonUp) * (1.0 - radianteRain()) * smoothstep(-0.02, 0.25, direction.y);
    if (!seen) { visible *= clamp(float(AETHER_NIGHT_GLOW), 0.0, 4.0); }
    if (visible <= 0.001) { return vec3(0.0); }
    // Its brightest clumps about as bright again as the night sky behind them.
    return aetherMilkyWay(aetherCelestial(direction), seen) * (0.7 * milkyWay * visible * aetherSkyReference(true));
}

#endif
