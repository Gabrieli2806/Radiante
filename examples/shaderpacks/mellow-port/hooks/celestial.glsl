#ifndef RADIANTE_HOOK_CELESTIAL_GLSL
#define RADIANTE_HOOK_CELESTIAL_GLSL

// Mellow port: Mellow's round sun and moon (its ROUND_SUN setting). Ported from Mellow by TheCMK (MIT, LICENSE-MELLOW):
// round_sun in shaders/global/atmosphere/sky.glsl, which Mellow draws in the sky in place of vanilla's square sprites
// (sun=off and moon=off in shaders.properties when ROUND_SUN is set). Its shape and colours are Mellow's: a small disc
// brightest at its centre (1 - smoothstep over (1 - cos) / 2 up to 0.0015, about 4.4 degrees across the radius), a
// smaller hard-edged moon, a yellow sun and a pale blue moon. Their brightness is Radiante's (sunLight, moonLight),
// so the exposure and reflections keep working; rain hides them as radiante-pt hides its own. Off, as in Mellow,
// vanilla's sprites stay.
#ifndef MELLOW_ROUND_SUN
#    define MELLOW_ROUND_SUN 0
#endif

// Mellow's SUN_COLOR and MOON_COLOR, as hues of luminance 1.
vec3 mellowDiscHue(vec3 colour) {
    return colour / dot(colour, vec3(0.2126, 0.7152, 0.0722));
}

void hookSunDisc(inout vec3 radiance, vec3 direction, vec3 sunDirection, vec3 sunLight) {
#if MELLOW_ROUND_SUN
    // round_sun: Dist = dot(view, sun) * 0.5 + 0.5, so 1 - Dist is (1 - cos) / 2, 0 at the sun's centre.
    float fromCentre = 1.0 - (dot(direction, sunDirection) * 0.5 + 0.5);
    float disc = 1.0 - smoothstep(0.0, 0.0015, fromCentre);
    radiance = sunLight * mellowDiscHue(vec3(5.0, 3.5, 0.8)) * disc;
#endif
}

void hookMoonDisc(inout vec3 radiance, vec3 direction, vec3 moonDirection, vec3 moonLight) {
#if MELLOW_ROUND_SUN
    // round_sun's moon: smoothstep(0.9995, 1, 1 - Dist), measured from the sun's opposite point; here from the moon
    // itself, whose path in Radiante can lean apart from the sun's.
    float towards = 1.0 - (1.0 - dot(direction, moonDirection)) * 0.5;
    float disc = smoothstep(0.9995, 1.0, towards);
    radiance = moonLight * mellowDiscHue(vec3(1.0, 1.5, 2.5)) * disc;
#endif
}

#endif
