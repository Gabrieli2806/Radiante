#ifndef AETHER_SKY_GLSL
#define AETHER_SKY_GLSL

// Aether: the painted sky's colours and the sky's brightness, for the overworld miss shader's hooks (sky, sun and
// moon, stars, the night sky's glow, clouds). Reads radiante-pt's sky cube (skyFull) and the ray's payload (mainRay),
// which only the miss shader has, so only those hooks include it.
#include "aether/aether_common.glsl"

// True for the sky the camera sees: its own ray, the sky through glass or water, and what a window or calm water
// mirrors (a view chain, RAY_VIEW_BOUNCE_MARK); false for the rays that bounced off the world to gather its light.
// Only smooth things are drawn differently for the two (the sky's hue, the glow round the sun); the stars and the
// clouds' detail go to the camera alone, through the hooks' own tests.
bool aetherSeen() {
    return rayBounce(mainRay) == 0u || rayCameraPath(mainRay) || mainRay.pad0 == RAY_VIEW_BOUNCE_MARK;
}

// How bright Radiante PT's sky is, as luminance: the zenith and the horizon a quarter turn from the sun (away from
// its glow), averaged. What the pack adds over the sky (clouds, stars, the milky way) is scaled by this, so it keeps
// its place against the sky by day, at dusk and by night. afterRain: the sky as radiante-pt leaves it after the rain
// has greyed it (world_miss_eval_impl.glsl), for what is drawn over that.
float aetherSkyReference(bool afterRain) {
    vec3 sun = radianteSunDirection();
    vec3 side = cross(vec3(0.0, 1.0, 0.0), sun);
    side = dot(side.xz, side.xz) > 1e-6 ? normalize(vec3(side.x, 0.08, side.z)) : vec3(0.996, 0.08, 0.0);
    float zenith = aetherLuma(textureLod(skyFull, vec3(0.0, 1.0, 0.0), 0.0).rgb);
    float horizon = aetherLuma(textureLod(skyFull, side, 0.0).rgb);
    float sky = 0.5 * (zenith + horizon);
    if (afterRain) { sky = mix(sky, 0.1 * smoothstep(-0.3, 0.3, sun.y), radianteRain()); }
    return max(sky, 0.0);
}

// The painted sky's colour along dir: only its hue and its gradient are used, its brightness is Radiante PT's. Linear
// colours of moderate saturation: Radiante's tone curve keeps a saturated blue, and a deep linear blue came out
// electric on screen (the Mellow port's lesson).
vec3 aetherSkyPaint(vec3 dir, vec3 sun, AetherTime t) {
    float up = clamp(dir.y, 0.0, 1.0);
    // 1 at the horizon (and below it), falling off over the lower third of the sky.
    float horizon = exp(-up * 4.5);
    vec2 across = dir.xz;
    vec2 sunAcross = sun.xz;
    float facing = dot(across, across) > 1e-6 && dot(sunAcross, sunAcross) > 1e-6 ?
                       dot(normalize(across), normalize(sunAcross)) : 0.0;
    // 1 on the sun's side of the sky, 0 opposite it.
    float towards = facing * 0.5 + 0.5;
    float away = 1.0 - towards;
    float nearSun = max(dot(dir, sun), 0.0);

    // Day: a clear blue overhead, deeper than the horizon but not electric, and a luminous pastel horizon; warm white
    // round the sun.
    vec3 day = mix(vec3(0.34, 0.52, 1.00), vec3(0.78, 0.87, 1.00), horizon);
    day = mix(day, vec3(1.00, 0.96, 0.90), pow(nearSun, 8.0) * 0.6);

    // Golden hour: a periwinkle vault, an amber (evening) or rose-gold (morning) horizon on the sun's side and a
    // lavender one opposite, a gold glow round the low sun.
    vec3 goldToward = mix(vec3(1.00, 0.66, 0.42), vec3(1.00, 0.63, 0.52), t.dawn);
    vec3 goldHorizon = mix(vec3(0.78, 0.70, 0.84), goldToward, towards * towards);
    vec3 golden = mix(vec3(0.40, 0.47, 0.86), goldHorizon, horizon);
    golden = mix(golden, vec3(1.00, 0.56, 0.26), pow(nearSun, 6.0) * 0.7 * (0.4 + 0.6 * horizon));

    // Blue hour: an indigo vault, a rose horizon where the sun went down, and opposite it the Belt of Venus, a pink
    // band just over the earth's blue shadow.
    vec3 blueToward = mix(vec3(0.95, 0.52, 0.45), vec3(0.88, 0.55, 0.64), t.dawn);
    vec3 blueHorizon = mix(vec3(0.40, 0.45, 0.74), blueToward, towards * towards * towards);
    vec3 blue = mix(vec3(0.17, 0.22, 0.58), blueHorizon, horizon);
    float belt = smoothstep(0.03, 0.10, dir.y) * (1.0 - smoothstep(0.14, 0.30, dir.y));
    float earthShadow = 1.0 - smoothstep(0.0, 0.07, dir.y);
    blue = mix(blue, vec3(0.86, 0.58, 0.72), belt * away * away * 0.7);
    blue = mix(blue, vec3(0.30, 0.36, 0.64), earthShadow * away * away * 0.6);

    // Night: navy overhead, a little lighter and greyer at the horizon.
    vec3 night = mix(vec3(0.10, 0.14, 0.34), vec3(0.20, 0.24, 0.38), horizon);

    return day * t.day + golden * t.golden + blue * t.blue + night * t.night;
}

// How the seen sky's brightness is reshaped against Radiante PT's in that direction: a brighter horizon and a
// slightly deeper zenith by day and at golden hour, as in a painted sky; nothing at night.
float aetherSkyLift(vec3 dir, AetherTime t) {
    float horizon = exp(-clamp(dir.y, 0.0, 1.0) * 4.5);
    return 1.0 + (t.day + t.golden) * (0.20 * horizon - 0.10 * (1.0 - horizon));
}

// The colour light takes from the sun or moon at this hour, luminance about 1 (for the clouds).
vec3 aetherSunHue(AetherTime t, bool moon) {
    if (moon) { return vec3(0.62, 0.72, 1.00) / aetherLuma(vec3(0.62, 0.72, 1.00)); }
    vec3 golden = mix(vec3(1.00, 0.62, 0.32), vec3(1.00, 0.60, 0.46), t.dawn);
    vec3 hue = t.day * vec3(1.00, 0.96, 0.90) + t.golden * golden + (t.blue + t.night) * vec3(0.95, 0.48, 0.55);
    return hue / max(aetherLuma(hue), 1e-4);
}

// The vault's colour at this hour, luminance about 1: what lights the clouds from above and the haze round them.
vec3 aetherAmbientHue(AetherTime t) {
    vec3 hue = t.day * vec3(0.58, 0.70, 1.00) + t.golden * vec3(0.66, 0.60, 0.80) + t.blue * vec3(0.38, 0.40, 0.72) +
               t.night * vec3(0.26, 0.31, 0.52);
    return hue / max(aetherLuma(hue), 1e-4);
}

#endif
