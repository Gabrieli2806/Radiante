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
