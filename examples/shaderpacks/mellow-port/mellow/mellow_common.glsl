#ifndef MELLOW_COMMON_GLSL
#define MELLOW_COMMON_GLSL

// Mellow port: Mellow's time of day and its colours, shared by the hooks. Ported from Mellow by TheCMK (MIT,
// LICENSE-MELLOW): the default colour scheme (COLOR_SCHEME 1) in shaders/lib/colors.glsl, get_sky_color and
// get_sun_color in shaders/global/lighting/light_colors.vsh, get_sun_glare in shaders/global/atmosphere/sky.glsl and
// the dayStrength, nightStrength, sunriseStrength and sunsetStrength uniforms in shaders/shaders.properties.
//
// The weather and biome come from radiante-pt's common/pack_world.glsl: radianteThunder() is Iris's thunderStrength,
// radianteBiomePrecipitation() Mellow's precipitationSmooth, radianteBiomeIs() its biome == BIOME_... tests.
//
// Mellow's colours are in its own units (it multiplies the picture by EXPOSURE 2.3 before an ACES curve); Radiante's
// sky is physical radiance on an automatic exposure. The hooks take Mellow's colours for their hue and gradient and
// scale them to Radiante's brightness, so the exposure and the light the sky casts stay Radiante's.

#include "common/pack_world.glsl"

#define MELLOW_PI 3.14159265
#define MELLOW_ISOTROPIC_PHASE (0.25 / MELLOW_PI)

float mellowLuminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Mellow's to_linear: its colours are written in display terms and decoded with a 2.2 power.
vec3 mellowToLinear(vec3 c) {
    return pow(c, vec3(2.2));
}

float mellowLinstep(float a, float b, float x) {
    return clamp((x - a) / (b - a), 0.0, 1.0);
}

// Mellow's phase function (xlf_phase in shaders/lib/util.glsl).
float mellowPhase(float angle, const float g) {
    float g2 = g * g;
    float denom = (1.0 + g2 - 2.0 * g * angle);
    float result = 1.5 * ((1.0 - g2) / (2.0 + g2)) * ((1.0 + angle * angle) / denom) + g * angle;
    return result / (4.0 * MELLOW_PI);
}

struct MellowTime {
    float day;
    float night;
    float sunrise;
    float sunset;
};

// Mellow's four weights from the sun's height. Mellow takes them from worldTime: |sin(timeAngle * 2 pi)| is the sun's
// height, day below 12000 ticks and night above, and the twilight is a sunset from 8000 to 16000 ticks, a sunrise
// otherwise. Here the height is the sun direction's y and the sunset is the twilight with the sun in the west (-x),
// the same split, so it follows Radiante's sun whatever the clock says.
MellowTime mellowTime(vec3 sunDir) {
    MellowTime t;
    float height = sunDir.y;
    t.day = height > 0.0 ? min(height * 2.0, 1.0) : 0.0;
    t.night = height < 0.0 ? min(-height * 2.0, 1.0) : 0.0;
    float twilight = max(1.0 - t.day - t.night, 0.0);
    t.sunset = sunDir.x < 0.0 ? twilight : 0.0;
    t.sunrise = twilight - t.sunset;
    return t;
}

// Mellow's apply_saturation (shaders/lib/tonemap.glsl).
vec3 mellowSaturation(vec3 c, float amount) {
    return mix(vec3(mellowLuminance(c)), c, amount);
}

// Mellow's weather on its sky (get_sky_color): RAIN_SKY_DESATURATION 0.5 and RAIN_SKY_DARKENING 0.33 at full rain,
// and in thunder a further 0.66 off the saturation and 0.5 off the brightness (the IS_IRIS part of get_sky_color in
// shaders/global/lighting/light_colors.vsh).
const float MELLOW_RAIN_SKY_DESATURATION = 0.5;
const float MELLOW_RAIN_SKY_DARKENING = 0.33;

float mellowRainSkyDarken() {
    return (1.0 - skyUBO.rainGradient * MELLOW_RAIN_SKY_DARKENING) * (1.0 - radianteThunder() * 0.5);
}

float mellowRainSkyDesaturate() {
    return (1.0 - skyUBO.rainGradient * MELLOW_RAIN_SKY_DESATURATION) * (1.0 - radianteThunder() * 0.66);
}

// The sun's colour in rain and thunder (get_sun_color in light_colors.vsh): greyed by half in rain and a further half
// in thunder, darkened by half in rain and by three quarters in thunder.
float mellowSunDesaturate() {
    return (1.0 - skyUBO.rainGradient * 0.5) * (1.0 - radianteThunder() * 0.5);
}

float mellowSunDarken() {
    return (1.0 - skyUBO.rainGradient * 0.5) * (1.0 - radianteThunder() * 0.75);
}

// SKY_TOP and SKY_GROUND (get_sky_color), the default scheme's values, greyed and darkened by the rain as Mellow does.
vec3 mellowSkyTop(MellowTime t) {
    vec3 top = mellowToLinear(vec3(0.0, 0.3, 0.5)) * t.sunrise + mellowToLinear(vec3(0.17, 0.35, 0.7)) * t.day +
               mellowToLinear(vec3(0.0, 0.15, 0.25)) * t.sunset + mellowToLinear(vec3(0.0, 0.025, 0.075)) * t.night;
    return mellowSaturation(top, mellowRainSkyDesaturate()) * mellowRainSkyDarken();
}

vec3 mellowSkyGround(MellowTime t) {
    return (mellowToLinear(vec3(0.5, 0.4, 0.38)) * t.sunrise + mellowToLinear(vec3(0.5, 0.55, 0.6)) * t.day +
            mellowToLinear(vec3(0.3, 0.25, 0.25)) * t.sunset + mellowToLinear(vec3(0.15, 0.2, 0.25)) * t.night) *
           mellowRainSkyDarken();
}

// SUN_DIRECT (get_sun_color): the sun's colour by day, the moon's (dimmed by its phase) by night, faded out round
// the horizon, and greyed and halved in rain as Mellow does. Used for the clouds; the world's own sunlight is
// Radiante's, which dims the sun in rain itself (hooks/direct_light.glsl takes only the hue).
vec3 mellowSunDirect(MellowTime t, vec3 sunDir) {
    vec3 direct;
    if (sunDir.y > 0.0) {
        direct = mellowToLinear(vec3(0.8, 0.6, 0.4)) * t.sunrise + mellowToLinear(vec3(1.2, 1.1, 1.0)) * t.day +
                 mellowToLinear(vec3(0.8, 0.5, 0.2)) * t.sunset;
    } else {
        direct = mellowToLinear(vec3(0.4, 0.4, 0.5));
        // MOON_PHASE_INFLUENCE 0.3: a new moon at 70% of a full one.
        direct *= cos(float(skyUBO.moonPhase % 8u) / 8.0 * 2.0 * MELLOW_PI) * 0.15 + 0.85;
    }
    direct *= smoothstep(0.0, 0.2, abs(sunDir.y));
    return mellowSaturation(direct, mellowSunDesaturate()) * mellowSunDarken();
}

// The sun's colour alone, for tinting: SUN_DIRECT before the fade round the horizon (which only dims it).
vec3 mellowSunHue(MellowTime t, bool moon) {
    if (moon) return mellowToLinear(vec3(0.4, 0.4, 0.5));
    vec3 c = mellowToLinear(vec3(0.8, 0.6, 0.4)) * t.sunrise + mellowToLinear(vec3(1.2, 1.1, 1.0)) * t.day +
             mellowToLinear(vec3(0.8, 0.5, 0.2)) * t.sunset;
    return c / max(t.sunrise + t.day + t.sunset, 1e-4);
}

// get_sun_glare: the orange glow on the sun's side of the sky at sunrise and sunset. cosSun is the cosine between
// the view and the sun.
vec3 mellowSunGlare(MellowTime t, float cosSun) {
    float visibility = t.sunset + t.sunrise;
    visibility *= visibility;
    float side = cosSun * 0.5 + 0.5;
    // get_sun_glare (shaders/global/atmosphere/sky.glsl): RAIN_SKY_DARKENING in rain, three quarters off in thunder.
    float darken = (1.0 - skyUBO.rainGradient * MELLOW_RAIN_SKY_DARKENING) * (1.0 - radianteThunder() * 0.75);
    return mellowToLinear(vec3(0.8, 0.45, 0.0)) * side * side * visibility * darken;
}

// get_sky: SKY_TOP overhead, SKY_GROUND (and the glare) at and below the horizon, with Mellow's fogify falloff.
vec3 mellowSky(MellowTime t, vec3 dir, vec3 sunDir) {
    float upDot = dir.y + 0.1;
    float x = max(upDot, 0.0);
    float fogify = 0.03 / (x * x + 0.03);
    vec3 glare = mellowSunGlare(t, dot(dir, sunDir));
    return mix(mellowSkyTop(t), mellowSkyGround(t) + glare, fogify);
}

// How much brighter Radiante's sky is than Mellow's, from the zenith and the horizon a quarter turn from the sun (away
// from its glow): Mellow's colours times this keep Radiante's sky brightness and exposure through the day and night,
// with Mellow's hue and its gradient from the horizon up. rainy measures Radiante's sky as it is after the rain has
// greyed it (world_miss_eval_impl.glsl), for what is drawn over that: the clouds and the stars.
float mellowSkyScale(MellowTime t, vec3 sunDir, bool rainy) {
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 side = cross(up, sunDir);
    side = dot(side.xz, side.xz) > 1e-6 ? normalize(vec3(side.x, 0.05, side.z)) : vec3(1.0, 0.05, 0.0);
    float radiante = mellowLuminance(textureLod(skyFull, up, 0.0).rgb) +
                     mellowLuminance(textureLod(skyFull, side, 0.0).rgb);
    if (rainy) {
        float rainyGrey = 0.1 * smoothstep(-0.3, 0.3, sunDir.y);
        radiante = mix(radiante, 2.0 * rainyGrey, skyUBO.rainGradient);
    }
    float mellow = mellowLuminance(mellowSky(t, up, sunDir)) + mellowLuminance(mellowSky(t, side, sunDir));
    return radiante / max(mellow, 1e-6);
}

// Mellow's random (shaders/lib/util.glsl).
float mellowRandom(vec2 coords) {
    return fract(sin(dot(coords, vec2(12.9898, 78.233))) * 43758.5453);
}

#endif
