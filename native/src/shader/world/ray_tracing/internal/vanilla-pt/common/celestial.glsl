#ifndef VPT_CELESTIAL_GLSL
#define VPT_CELESTIAL_GLSL

// Normalizes a direction uploaded from the Java side. A zero-length or NaN/Inf direction would poison every
// sun-lit sample, so it is swapped for the given fallback instead.
vec3 celestialNormalize(vec3 direction, vec3 fallback) {
    float lengthSquared = dot(direction, direction);
    if (lengthSquared <= 1e-8 || any(isnan(direction)) || any(isinf(direction))) { return fallback; }
    return direction * inversesqrt(lengthSquared);
}

// Direction towards the sun (straight up when unknown). The sun-path tilt option is applied before upload.
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

// The moon always sits opposite the sun.
vec3 celestialMoonDirection() {
    return -celestialSunDirection();
}

#endif
