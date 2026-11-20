#ifndef VPT_CELESTIAL_GLSL
#define VPT_CELESTIAL_GLSL

// Normalizes a direction uploaded from the Java side. A zero-length or NaN/Inf direction would poison every
// sun-lit sample, so it is swapped for the given fallback instead.
vec3 celestialNormalize(vec3 direction, vec3 fallback) {
    float lengthSquared = dot(direction, direction);
    if (lengthSquared <= 1e-8 || any(isnan(direction)) || any(isinf(direction))) { return fallback; }
    return direction * inversesqrt(lengthSquared);
}

// Direction towards the sun (straight up when unknown). The sun path settings (tilt, rotation) are applied before
// upload.
vec3 celestialSunDirection() {
    return celestialNormalize(skyUBO.sunDirection, vec3(0.0, 1.0, 0.0));
}

// The radiance of an overcast sky (rain, snow) before the horizon and a storm shape it: 0.1 by day, and by night
// OVERCAST_NIGHT_RADIANCE at the default Night Brightness (35), about half the light a clear night's moon and sky
// give the ground. It went to nothing at night before: in rain or snow the only light left was the lamps', and the
// ground a few blocks from a lantern was black at the brightest the eye opens to, where a clear night showed it all.
#ifndef OVERCAST_NIGHT_RADIANCE
#    define OVERCAST_NIGHT_RADIANCE 0.001
#endif
float celestialOvercastLevel() {
    float night = OVERCAST_NIGHT_RADIANCE * max(worldUBO.moonBrightness, 0.0) / 0.35;
    return mix(night, 0.1, smoothstep(-0.3, 0.3, celestialSunDirection().y));
}

// ---- The sky of each weather. Rain, a thunderstorm and snow shared one flat grey, the same from the zenith to the
// horizon and from noon to dusk, and the land under it took that grey: the three looked alike and none had a mood.
// Each has its own sky now, in two forms: the one that lights the world, which keeps about the light the grey gave,
// and the one the camera sees (a view ray, or its mirror image), darker than its light as a sheet of cloud is, so it
// reads as a sky and not as a white card.
//   rain    a slate blue, lighter and greyer towards the horizon, and a cool light on the land;
//   storm   near indigo overhead over a pale, slightly green horizon, and less light;
//   snow    a bright, pale blue-white, nearly even: the high-key sky of a snowfall (skyUBO.biomeClimate.z, how snowy
//           the camera's biome is, eased across a border).
// All three carry a brighter patch where the sun or the moon stands behind the cloud (which gives the land a soft side
// to its light, with no hard shadows), and at dawn and dusk a muted warm band along the horizon under it. (Banks of
// blocky cloud drawn into it read as squares stamped on the sky, not as weather, and were taken out.)
const vec3 OVERCAST_RAIN_ZENITH = vec3(0.31, 0.37, 0.49);
const vec3 OVERCAST_RAIN_HORIZON = vec3(0.56, 0.63, 0.70);
const vec3 OVERCAST_STORM_ZENITH = vec3(0.13, 0.16, 0.24);
const vec3 OVERCAST_STORM_HORIZON = vec3(0.36, 0.42, 0.41);
const vec3 OVERCAST_SNOW_ZENITH = vec3(0.80, 0.85, 0.93);
const vec3 OVERCAST_SNOW_HORIZON = vec3(0.96, 0.97, 0.99);
// The light of each on the land, against the grey's: its tint and its strength.
const vec3 OVERCAST_RAIN_LIGHT = vec3(0.90, 0.98, 1.12);
const vec3 OVERCAST_STORM_LIGHT = vec3(0.50, 0.57, 0.70);
const vec3 OVERCAST_SNOW_LIGHT = vec3(1.04, 1.07, 1.12);
// The warm band of dawn and dusk under the cloud, and the tint of an overcast night.
const vec3 OVERCAST_DUSK = vec3(1.0, 0.62, 0.36);
const vec3 OVERCAST_NIGHT_TINT = vec3(0.62, 0.80, 1.25);

/** The overcast sky in a direction: as the camera sees it (seen), or as it lights the world. */
vec3 celestialOvercastSky(vec3 dir, bool seen) {
    float level = celestialOvercastLevel();
    float storm = clamp(skyUBO.thunderGradient, 0.0, 1.0);
    float snow = clamp(skyUBO.biomeClimate.z, 0.0, 1.0);
    vec3 sun = celestialSunDirection();
    float day = smoothstep(-0.3, 0.3, sun.y);
    float up = clamp(dir.y, 0.0, 1.0);
    float toHorizon = pow(1.0 - up, 3.0);
    // Where the sun (the moon by night) stands behind the cloud.
    vec3 body = sun.y < 0.0 ? -sun : sun;
    float glow = pow(max(dot(dir, body), 0.0), 3.0) * (1.0 - 0.6 * storm);
    // Dawn and dusk: a band along the horizon, most under the sun.
    float dusk = smoothstep(-0.2, 0.0, sun.y) * (1.0 - smoothstep(0.08, 0.35, sun.y)) * (1.0 - 0.7 * storm);
    vec2 flatDir = dir.xz / max(length(dir.xz), 1e-4);
    vec2 flatSun = sun.xz / max(length(sun.xz), 1e-4);
    float duskBand = dusk * toHorizon * (0.35 + 0.65 * pow(max(dot(flatDir, flatSun), 0.0), 2.0));
    vec3 nightTint = mix(OVERCAST_NIGHT_TINT, vec3(1.0), day);
    if (!seen) {
        vec3 light = mix(mix(OVERCAST_RAIN_LIGHT, OVERCAST_SNOW_LIGHT, snow), OVERCAST_STORM_LIGHT * mix(1.0, 1.25, snow),
                         storm);
        light *= mix(1.2, 0.8, up) * (0.9 + 0.7 * glow);
        return level * nightTint * (light + OVERCAST_DUSK * duskBand * 0.6);
    }
    vec3 zenith = mix(mix(OVERCAST_RAIN_ZENITH, OVERCAST_SNOW_ZENITH, snow),
                      mix(OVERCAST_STORM_ZENITH, OVERCAST_SNOW_ZENITH * 0.55, snow), storm);
    vec3 horizon = mix(mix(OVERCAST_RAIN_HORIZON, OVERCAST_SNOW_HORIZON, snow),
                       mix(OVERCAST_STORM_HORIZON, OVERCAST_SNOW_HORIZON * 0.72, snow), storm);
    vec3 sky = mix(zenith, horizon, toHorizon);
    sky *= 1.0 + 0.45 * glow;
    return level * nightTint * (sky + OVERCAST_DUSK * duskBand * 0.9);
}

// The overcast sky's colour at the horizon as the camera sees it: what the distance fades into (the haze, the fog).
vec3 celestialOvercastRadiance() {
    vec3 sun = celestialSunDirection();
    vec2 flatSun = sun.xz / max(length(sun.xz), 1e-4);
    // Across the sun's bearing, so the band of dawn and dusk does not tint the whole distance.
    return celestialOvercastSky(normalize(vec3(-flatSun.y, 0.02, flatSun.x)), true);
}

// End sky flash (Minecraft 26.x): a violet glare that shows up roughly every 30 s, lasts 5 to 19 s on a sine
// envelope and comes from a new random direction each time. In the End the uploaded sunDirection is that flash
// direction scaled by its strength (zero with no flash), so the flash lights and shadows the islands like a sun.
const vec3 END_FLASH_RADIANCE = vec3(1.2, 0.7, 1.8);

// Flash strength in [0, 1]: zero outside the End (sky type 2) and whenever the uploaded length is not finite.
float endFlashIntensity() {
    if (worldUBO.skyType != 2) { return 0.0; }
    float strength = length(skyUBO.sunDirection);
    if (isnan(strength) || isinf(strength)) { return 0.0; }
    return clamp(strength, 0.0, 1.0);
}

// Lightning in a thunderstorm (Overworld): while the sky flashes, the bolt lights the world like a sun from its own
// direction, with shadows, through the sun's shadow ray, and the whole overcast sky glows with it. The Java side
// (LightningFlash) uploads its strength in rainWetness.z, 0..1 (0 with the "Hide Lightning Flashes" accessibility
// option), and the bolt's bearing from the camera in rainWetness.w (radians, from +x towards +z).
#ifndef VPT_LIGHTNING_FLASH
#    define VPT_LIGHTNING_FLASH 1.0
#endif
// The flash's light at full strength, a cold blue; the sun's default is 8. The bolt's own glow lights the land
// around it already, and with the eye opened up for the dark, a brighter flash turned a storm by night white.
const vec3 LIGHTNING_RADIANCE = vec3(0.45, 0.55, 0.8);
// How high the flash's light comes from: the bolt stands a long way off, lighting the land from above its horizon.
const float LIGHTNING_ELEVATION = 0.75;

float lightningFlash() {
    if (worldUBO.skyType != 1) { return 0.0; }
    float strength = skyUBO.rainWetness.z;
    if (isnan(strength) || isinf(strength)) { return 0.0; }
    return clamp(strength, 0.0, 1.0) * max(VPT_LIGHTNING_FLASH, 0.0);
}

vec3 lightningDirection() {
    float bearing = skyUBO.rainWetness.w;
    return normalize(vec3(cos(bearing), LIGHTNING_ELEVATION, sin(bearing)));
}

// Direction towards the moon. Its path can lean and turn apart from the sun's (the Moon Path settings), so it is
// uploaded on its own; it keeps the hour opposite the sun's, so it is up exactly while the sun is down. Opposite the
// sun when unknown, as it is with the moon following the sun's path.
vec3 celestialMoonDirection() {
    return celestialNormalize(skyUBO.moonDirection.xyz, -celestialSunDirection());
}

// How much of the moon's face its phase shows lit: 1 at the full moon (phase 0), a half at the quarters, 0 at the
// new moon (phase 4). The moon's light follows it, so a new moon leaves the night to the stars and the sky.
float celestialMoonLitFraction() {
    return 0.5 + 0.5 * cos(float(skyUBO.moonPhase) * 0.78539816); // a phase is an eighth of a turn, pi/4
}

// The body that lights the world: the sun while it is above the horizon, the moon otherwise. The sun's height alone
// picks it, since the moon is up exactly while the sun is down.
vec3 celestialLightDirection() {
    vec3 sun = celestialSunDirection();
    return sun.y < 0.0 ? celestialMoonDirection() : sun;
}

// A point on a sun or moon sprite turned around the sprite's centre by the Sun/Moon Rotation setting; rotation is
// (cos, sin) of the angle, (1, 0) leaving it as vanilla draws it.
vec2 celestialSpriteRotate(vec2 p, vec2 rotation) {
    // An unset rotation (all zero) would shrink the sprite to its centre texel across the whole square.
    if (dot(rotation, rotation) < 0.5) { return p; }
    return vec2(rotation.x * p.x - rotation.y * p.y, rotation.y * p.x + rotation.x * p.y);
}

#endif
