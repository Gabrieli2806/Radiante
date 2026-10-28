#ifndef MELLOW_COMMON_GLSL
#define MELLOW_COMMON_GLSL

// Mellow port: Mellow's time of day and its colours, shared by the hooks. Ported from Mellow by TheCMK (MIT,
// LICENSE-MELLOW): the default colour scheme (COLOR_SCHEME 1) in shaders/lib/colors.glsl, get_sky_color and
// get_sun_color in shaders/global/lighting/light_colors.vsh, get_sun_glare in shaders/global/atmosphere/sky.glsl and
// the dayStrength, nightStrength, sunriseStrength and sunsetStrength uniforms in shaders/shaders.properties.
//
// Mellow's colours are in its own units (it multiplies the picture by EXPOSURE 2.3 before an ACES curve); Radiante's
// sky is physical radiance on an automatic exposure. The hooks take Mellow's colours for their hue and gradient and
// scale them to Radiante's brightness, so the exposure and the light the sky casts stay Radiante's.

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

// SKY_TOP and SKY_GROUND (get_sky_color), the default scheme's values.
vec3 mellowSkyTop(MellowTime t) {
    return mellowToLinear(vec3(0.0, 0.3, 0.5)) * t.sunrise + mellowToLinear(vec3(0.17, 0.35, 0.7)) * t.day +
           mellowToLinear(vec3(0.0, 0.15, 0.25)) * t.sunset + mellowToLinear(vec3(0.0, 0.025, 0.075)) * t.night;
}

vec3 mellowSkyGround(MellowTime t) {
    return mellowToLinear(vec3(0.5, 0.4, 0.38)) * t.sunrise + mellowToLinear(vec3(0.5, 0.55, 0.6)) * t.day +
           mellowToLinear(vec3(0.3, 0.25, 0.25)) * t.sunset + mellowToLinear(vec3(0.15, 0.2, 0.25)) * t.night;
}

// SUN_DIRECT (get_sun_color): the sun's colour by day, the moon's (dimmed by its phase) by night, faded out round
// the horizon. Mellow's rain desaturation is left to Radiante, which dims the sun in rain itself.
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
    return direct * smoothstep(0.0, 0.2, abs(sunDir.y));
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
    return mellowToLinear(vec3(0.8, 0.45, 0.0)) * side * side * visibility;
}

// get_sky: SKY_TOP overhead, SKY_GROUND (and the glare) at and below the horizon, with Mellow's fogify falloff.
vec3 mellowSky(MellowTime t, vec3 dir, vec3 sunDir) {
    float upDot = dir.y + 0.1;
    float x = max(upDot, 0.0);
    float fogify = 0.03 / (x * x + 0.03);
    vec3 glare = mellowSunGlare(t, dot(dir, sunDir));
    return mix(mellowSkyTop(t), mellowSkyGround(t) + glare, fogify);
}

#endif
