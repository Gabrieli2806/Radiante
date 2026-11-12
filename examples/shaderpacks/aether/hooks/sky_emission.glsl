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
// Radiante's own noise (the public radiante/v1 header), for the aurora: no texture of the pack's is needed for it.
#include <radiante/v1/noise.glsl>

// The aurora: curtains of light over the snow. Only where the camera stands in a snowy biome (radianteBiomeSnowy,
// eased, so it fades in across the border), at night, behind the rain and under the pack's clouds. Each curtain is a
// sheet standing round the horizon: its foot at a height that wanders with the compass direction, bright and green
// there and fading upwards into violet, folded so that it is dense in some directions and gone in others, with fine
// rays running straight up it that shimmer. Two sheets, one behind the other, drift slowly on the pack's clock. It is
// light as well as a picture: the snow under it takes a faint green. The rays that gather that light (seen false)
// get the sheets' smooth average, with no folds or rays, as the milky way does.

// One sheet: its light along dir, 0 to about 1, and how far up it the ray is (0 at the foot, 1 at the top). round is
// the compass direction as a point on the unit circle: the noise is read round that circle, never along an angle,
// which jumps where it starts over and cut the sheet off along a straight line up the sky.
float aetherAuroraSheet(vec3 dir, vec2 round, float time, float seed, out float up) {
    // The foot's height over the horizon wanders with the direction and slowly with time.
    float foot = 0.10 + 0.16 * radianteNoise(vec3(round * 0.7 + seed, time * 0.008));
    float height = 0.30 + 0.25 * radianteNoise(vec3(round * 0.45 + seed * 3.0, 5.0));
    up = (dir.y - foot) / height;
    if (up <= 0.0 || up >= 1.6) { return 0.0; }
    // Bright just over the foot, a long fade above it.
    float profile = smoothstep(0.0, 0.06, up) * exp(-up * 2.6);
    // Where round the horizon the sheet is: folds, with whole stretches empty.
    float folds = radianteNoise(vec3(round * 1.6 + seed * 7.0, time * 0.015));
    float presence = smoothstep(0.38, 0.75, folds);
    // The rays: fine, upright (they depend on the direction alone), flickering.
    float rays = 0.45 + 0.55 * radianteNoise(vec3(round * 22.0 + seed * 11.0, time * 0.5));
    return profile * presence * rays;
}

vec3 aetherAurora(vec3 dir, bool seen) {
    if (dir.y <= 0.04) { return vec3(0.0); }
    const vec3 foot = vec3(0.16, 1.00, 0.42);
    const vec3 top = vec3(0.62, 0.26, 0.95);
    if (!seen) {
        // The average of the two sheets over every direction, about a sixth of their brightest.
        float band = smoothstep(0.08, 0.25, dir.y) * (1.0 - smoothstep(0.45, 0.85, dir.y));
        return mix(foot, top, smoothstep(0.2, 0.7, dir.y)) * (0.16 * band);
    }
    float across = length(dir.xz);
    vec2 round = across > 1e-4 ? dir.xz / across : vec2(1.0, 0.0);
    float time = aetherSeconds();
    vec3 light = vec3(0.0);
    float up;
    float near = aetherAuroraSheet(dir, round, time, 1.7, up);
    light += mix(foot, top, smoothstep(0.15, 0.9, up)) * near;
    // The second sheet a third of a turn round from the first.
    vec2 turned = vec2(round.x * -0.5 - round.y * 0.866, round.x * 0.866 + round.y * -0.5);
    float far = aetherAuroraSheet(dir, turned, time * 0.7, 9.2, up);
    light += mix(foot, top, smoothstep(0.05, 0.7, up)) * (0.6 * far);
    return light;
}

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
    float aurora = clamp(float(AETHER_AURORA), 0.0, 2.0) * radianteBiomeSnowy();
    if ((milkyWay <= 0.0 && aurora <= 0.0) || direction.y <= -0.02) { return vec3(0.0); }
    AetherTime t = aetherTime();
    float night = t.night + 0.15 * t.blue;
    if (night <= 0.001) { return vec3(0.0); }
    float moonUp = smoothstep(0.0, 0.3, radianteMoonDirection().y) * radianteMoonLit();
    float clear = night * (1.0 - radianteRain());
    float visible = clear * (1.0 - 0.5 * moonUp) * smoothstep(-0.02, 0.25, direction.y);
    if (!seen) { visible *= clamp(float(AETHER_NIGHT_GLOW), 0.0, 4.0); }
    float reference = aetherSkyReference(true);
    vec3 glow = vec3(0.0);
    // Its brightest clumps about as bright again as the night sky behind them.
    if (milkyWay > 0.0 && visible > 0.001) {
        glow += aetherMilkyWay(aetherCelestial(direction), seen) * (0.7 * milkyWay * visible * reference);
    }
    // The aurora's curtains some twenty times the night sky at their feet: the brightest thing in a moonless night
    // after the moon itself, as it is.
    if (aurora > 0.001 && clear > 0.001) { glow += aetherAurora(direction, seen) * (22.0 * aurora * clear * reference); }
    return glow;
}

#endif
