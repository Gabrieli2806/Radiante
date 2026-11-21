#version 460
#extension GL_GOOGLE_include_directive : require

#include "classic_tone_curves.glsl"

// Tone mapping: the HDR radiance exposed (plus bloom), then either one of the classic curves with the grade, or the
// Bedrock-style tone curve and grade (method 6), into the displayable image. When HDR output is active (pc.hdrPeak > 1),
// colours up to the highlight knee match SDR bit for bit and highlights extend smoothly to the display's peak.

layout(set = 0, binding = 0) uniform sampler2D hdrImage;
// The bloom pyramid's result at half size; the HDR image again while bloom is off (bloomIntensity is 0 then).
layout(set = 0, binding = 3) uniform sampler2D bloomImage;

layout(set = 0, binding = 2) readonly buffer ExposureBuffer {
    float exposure;
    float avgLogLum;
    float padding0;
    float padding1; // the classic curve's input at HDR_KNEE (exposure.comp)
}
expData;

// The Bedrock-style tone curve built by bedrock_curve.comp (method 6 only).
layout(std430, set = 0, binding = 4) readonly buffer BedrockCurveBuffer {
    uint histogram[256];
    float curve[256];
}
bedrock;

// Local tone mapping (local_tone.comp): where it leaves the brightness as it is, and how far it moves the rest.
layout(std430, set = 0, binding = 6) readonly buffer LocalToneBuffer {
    float anchor;
    float highlights;
    float shadows;
    float started;
}
localTone;
// The guided filter's a and b, at an eighth of the picture's size (local_tone.comp, pass 3).
layout(set = 0, binding = 7) uniform sampler2D localToneImage;
// The falling weather the camera saw (the shader pack's weather layer, at its render size): rgb the light it adds, a
// how much of the picture behind it it hides. Laid over here, after the denoiser and the upscaler, so rain and snow
// show as they are in this frame instead of smearing through the history those keep. One clear texel without it.
layout(set = 0, binding = 8) uniform sampler2D weatherLayer;

// ToneMappingModulePushConstant (tone_mapping_module.hpp).
layout(push_constant) uniform PushConstant {
    float log2Min;
    float log2Max;
    float epsilon;
    float lowPercent;
    float highPercent;
    float middleGrey;
    float dt;
    float speedUp;
    float speedDown;
    float minExposure;
    float maxExposure;
    float manualExposure;
    float exposureBias;
    float whitePoint;
    float saturation;
    int toneMappingMethod;
    int autoExposure;
    int clampOutput;
    int exposureMeteringMode;
    float centerMeteringPercent;
    float adaptation;
    // The Bedrock-style grade. At bloom 0, contrast 1, shadow contrast 0, gamma 2.2 and balance 1 the classic curves'
    // output is what it was before the grade existed.
    float bloomIntensity;
    float contrast;
    float shadowContrast;
    float shadowContrastEnd;
    float gamma;
    float balanceR;
    float balanceG;
    float balanceB;
    float filmicSaturation; // Bedrock's filmic saturation correction, 0 off
    float hdrPeak;          // display peak in paper whites (1.0 in SDR)
    float localToneMapping; // Local Tone Mapping's strength; 0 is off, and local_tone.comp did not run
}
pc;

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 fragColor;

const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);
// ---- The classic curves (method 0 to 5): util/classic_tone_curves.glsl.

vec3 classicCurve(vec3 color) {
    return classicCurveFor(color, pc.toneMappingMethod, pc.whitePoint);
}

float classicCurveScalar(float x) {
    return classicCurve(vec3(max(x, 0.0))).r;
}

// Extends methods 1..5 to `targetPeak` (> 1) with C1 continuity at HDR_KNEE, matching SDR exactly below HDR_KNEE.
float extendClassicScalar(float x, float xKnee, float slopeKnee, float targetPeak) {
    float sdr = classicCurveScalar(x);
    if (sdr <= HDR_KNEE || x <= xKnee) { return sdr; }
    float room = max(targetPeak - HDR_KNEE, 1e-4);
    float dx = x - xKnee;
    return HDR_KNEE + (room * slopeKnee * dx) / (room + slopeKnee * dx);
}

vec3 classicCurveHdr(vec3 color, float targetPeak) {
    if (pc.toneMappingMethod < 1 || pc.toneMappingMethod > 5) {
        return pbrNeutral(color, targetPeak);
    }
    vec3 sdr = classicCurve(color);
    if (max(sdr.r, max(sdr.g, sdr.b)) <= HDR_KNEE) { return sdr; }

    // Where the curve reaches the knee: worked out once a frame by the exposure pass (classicCurveKnee).
    float xKnee = expData.padding1;
    float slopeKnee = max((classicCurveScalar(xKnee + 1e-3) - classicCurveScalar(max(xKnee - 1e-3, 0.0))) * 500.0, 1e-4);
    return vec3(extendClassicScalar(color.r, xKnee, slopeKnee, targetPeak),
                extendClassicScalar(color.g, xKnee, slopeKnee, targetPeak),
                extendClassicScalar(color.b, xKnee, slopeKnee, targetPeak));
}

// The controls of Bedrock's tone mapping material (colour balance, contrast, shadow contrast and its end), applied
// ahead of the classic curves. What each does is inferred from its name and approximate; all are neutral at their
// defaults.
vec3 bedrockGrade(vec3 color) {
    color *= vec3(pc.balanceR, pc.balanceG, pc.balanceB);
    if (pc.contrast != 1.0) {
        // Contrast about middle grey, in linear light.
        color = pc.middleGrey * pow(max(color, vec3(0.0)) / pc.middleGrey, vec3(pc.contrast));
    }
    if (pc.shadowContrast != 0.0 && pc.shadowContrastEnd > 0.0) {
        // Deepens (positive) or lifts (negative) what is darker than shadowContrastEnd, continuously at the end.
        float luma = dot(color, LUMA);
        color *= pow(clamp(luma / pc.shadowContrastEnd, 1e-3, 1.0), pc.shadowContrast);
    }
    return color;
}

vec3 withSaturation(vec3 color, float saturation) {
    float luma = dot(color, LUMA);
    return mix(vec3(luma), color, saturation);
}

// ---- The Bedrock-style mapper (method 6).

// The material's intensity: how far the graded colour is taken from the mapped one, Bedrock RTX's own value (calibrated to Bedrock RTX). It shows only where the mapped colour leaves the 0 to 1 range.
const float BEDROCK_TONE_MAPPING_INTENSITY = 0.9;
// The brightest channel the Nether's picture reaches (linear, before the sRGB encoding): 0.85 is 0.93 as shown.
const float NETHER_HUE_HOLD = 0.85;
const float NETHER_SHOULDER_KNEE = 0.3;

// The displayed log2 luminance for an input log2 luminance: the curve read with linear filtering, its 256 entries
// covering -24 .. +4.
float bedrockCurveAt(float log2Luminance) {
    float position = clamp((log2Luminance + 24.0) / 28.0 * 256.0 - 0.5, 0.0, 255.0);
    uint lower = uint(position);
    uint upper = min(lower + 1u, 255u);
    return mix(bedrock.curve[lower], bedrock.curve[upper], fract(position));
}

vec3 encodeSrgb(vec3 linear) {
    vec3 low = linear * 12.92;
    vec3 high = 1.055 * pow(max(linear, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, lessThan(linear, vec3(0.0031308)));
}

// Bedrock's tone mapping material in its order: the luminance through the tone curve with the hue kept, the filmic
// saturation correction, then the grade (colour balance, contrast about 0.18, clamp, saturation, gamma) and the sRGB
// encoding. When hdrPeak > 1, highlights past the knee extend smoothly to hdrPeak while midtones and shadows stay
// identical to SDR.
vec3 bedrockToneMap(vec3 color, float hdrPeak) {
    bool hdr = hdrPeak > 1.0001;
    float luminance = dot(color, LUMA);
    float logLum = log2(max(luminance, exp2(-24.0)));
    float curveLogLum = bedrockCurveAt(logLum);
    if (hdr && logLum > 2.0) {
        // Past 2 stops above unit exposure the SDR histogram bins compress towards white and clip at +4 stops; keep at
        // least the highlight budget's natural slope (~0.38 stops/stop) so lamps, lava and the sun separate in HDR.
        float extended = bedrockCurveAt(2.0) + (logLum - 2.0) * 0.38;
        curveLogLum = max(curveLogLum, extended);
    }
    vec3 mapped = luminance > 0.0 ? color * (exp2(curveLogLum) / luminance) : vec3(0.0);

    if (pc.filmicSaturation != 0.0) {
        float mean = dot(mapped, vec3(1.0 / 3.0));
        float response = ((1486.4 - 1489.7 * mean) * mean - 3.3) / ((0.15 * mean + 944.2) * mean + 1.0);
        mapped = mean + (1.0 + pc.filmicSaturation * max(response - 1.0, 0.0)) * (mapped - mean);
    }

    // A strongly coloured highlight (lava, fire, a red wall under a lava sea's light) has one channel far over white
    // once the eye is open. Clipped channel by channel it went pale: red pinned at 1 and the others lifted, the
    // pattern lost. Its brightest channel is brought down with a soft shoulder instead, the ratio of the channels
    // kept, so it stays orange and keeps its texture. Weighted by chroma: white and grey are left to the clamp, as
    // Bedrock does. The Nether (exposure.comp flags it) starts the shoulder early, as its eye is open the furthest.
    {
        float peak = max(mapped.r, max(mapped.g, mapped.b));
        float low = min(mapped.r, min(mapped.g, mapped.b));
        float chroma = peak > 0.0 ? clamp((peak - low) / peak * 1.25 - 0.25, 0.0, 1.0) : 0.0;
        bool nether = expData.padding0 > 0.5;
        float knee = nether ? NETHER_SHOULDER_KNEE : 0.7;
        float ceiling = (nether ? NETHER_HUE_HOLD : 1.0) * (hdr ? hdrPeak : 1.0);
        if (peak > knee && chroma > 0.0) {
            float room = max(ceiling - knee, 1e-4);
            float shouldered = knee + room * tanh((peak - knee) / room);
            mapped *= mix(1.0, shouldered / peak, chroma);
        }
    }

    vec3 graded = mapped * vec3(pc.balanceR, pc.balanceG, pc.balanceB);
    graded = max((graded - 0.18) * pc.contrast + 0.18, vec3(0.0));
    if (!hdr) { graded = min(graded, vec3(1.0)); }
    float gradedLuma = dot(graded, LUMA);
    graded = max(gradedLuma + (graded - gradedLuma) * max(pc.saturation, 0.0), vec3(0.0));
    graded = pow(graded, vec3(pc.gamma));
    graded = mapped + BEDROCK_TONE_MAPPING_INTENSITY * (graded - mapped);
    if (!hdr) {
        return clamp(encodeSrgb(graded), 0.0, 1.0);
    }

    graded = max(graded, vec3(0.0));
    float top = max(graded.r, max(graded.g, graded.b));
    if (top > HDR_KNEE) {
        float room = hdrPeak - HDR_KNEE;
        float newTop = HDR_KNEE + (room * (top - HDR_KNEE)) / (room + (top - HDR_KNEE));
        graded *= newTop / top;
    }
    vec3 srgb = encodeSrgb(min(graded, vec3(1.0)));
    vec3 above = pow(max(graded, vec3(1.0)), vec3(1.0 / 2.2));
    return mix(srgb, above, greaterThan(graded, vec3(1.0)));
}

// ---- Local tone mapping.

// Must match local_tone.comp.
const float LOCAL_TONE_MIN_LUMINANCE = 1.0 / 16384.0;
// Within this many stops of the anchor the brightness is left nearly as it is: the midtones keep their contrast, and
// only what is far brighter or far darker than the rest of the picture is moved.
const float LOCAL_TONE_KNEE = 1.0;
// Highlights are brought down from where their surroundings, exposed, pass this (log2 of the exposed luminance): white.
const float LOCAL_TONE_WHITE = 0.0;
// How much of the lifting of the dark parts the Nether keeps: its caverns are meant to be deep, and lifted like a
// cave in the Overworld, the rock far from the lava came up to the same flat red as the rock beside it.
const float NETHER_LOCAL_TONE_SHADOWS = 0.3;

// What the radiance at this pixel is multiplied by: the brightness of its surroundings (the guided filter's base) is
// brought back where it would go past white once exposed, and lifted where it is far darker than the anchor; the
// pixel's own detail against that base kept. Highlights were measured from the anchor, the picture's mean, so at night
// or in a storm, with the picture's mean low, a room lit by torches stood stops above it and was darkened as if it
// were the sky through a window: interiors went dark, darker than the ground a held torch lit, which fills the view.
float localToneGain(vec3 radiance, float exposure) {
    vec2 ab = texture(localToneImage, texCoord).rg;
    float l = log2(max(dot(radiance, LUMA), LOCAL_TONE_MIN_LUMINANCE));
    float base = ab.x * l + ab.y;
    float overWhite = base + log2(max(exposure, 1e-12)) - LOCAL_TONE_WHITE;
    float offset = clamp(overWhite > 0.0 ? overWhite : min(base - localTone.anchor, 0.0), -24.0, 24.0);
    float beyond = offset - LOCAL_TONE_KNEE * tanh(clamp(offset / LOCAL_TONE_KNEE, -10.0, 10.0));
    float strength = offset > 0.0 ? localTone.highlights
                                  : localTone.shadows * (expData.padding0 > 0.5 ? NETHER_LOCAL_TONE_SHADOWS : 1.0);
    float stops = clamp(-strength * beyond, -4.0, 2.0);
    return isnan(stops) ? 1.0 : exp2(stops);
}

// ----

float currentExposure() {
    float exposure = (pc.autoExposure != 0) ? expData.exposure : pc.manualExposure;
    if (isnan(exposure) || isinf(exposure) || exposure <= 0.0) { exposure = max(pc.manualExposure, 1e-6); }
    return exposure * exp2(pc.exposureBias);
}

void main() {
    vec3 hdr = texture(hdrImage, texCoord).rgb;
    // What reaches here may be not a number or infinite: a half float image overflows at 65504, and an upscaler can
    // pass such a pixel on. The curves divide by the luminance, and infinity over infinity came out black, a black
    // dot exactly where the picture is brightest. Not a number becomes black, too bright becomes white.
    hdr = vec3(isnan(hdr.r) ? 0.0 : min(hdr.r, 60000.0), isnan(hdr.g) ? 0.0 : min(hdr.g, 60000.0),
               isnan(hdr.b) ? 0.0 : min(hdr.b, 60000.0));
    vec4 weather = texture(weatherLayer, texCoord);
    if (!any(isnan(weather))) {
        hdr = hdr * (1.0 - clamp(weather.a, 0.0, 1.0)) + clamp(weather.rgb, vec3(0.0), vec3(60000.0));
    }

    // Capped again after the exposure, which can lift a large value past what a float divides cleanly.
    float exposure = max(currentExposure(), 0.0);
    vec3 exposed = clamp(hdr * exposure, vec3(0.0), vec3(1.0e6));
    if (pc.bloomIntensity > 0.0) { exposed += max(texture(bloomImage, texCoord).rgb, vec3(0.0)) * pc.bloomIntensity; }
    if (pc.localToneMapping > 0.0) { exposed *= localToneGain(hdr, exposure); }

    float hdrPeak = max(pc.hdrPeak, 1.0);
    if (pc.toneMappingMethod == 6) {
        fragColor = vec4(bedrockToneMap(exposed, hdrPeak), 1.0);
        return;
    }

    bool isHdr = hdrPeak > 1.0001;
    float gammaVal = max(pc.gamma, 1e-3);
    float classicPeak = isHdr ? pow(hdrPeak, gammaVal / 2.2) : 1.0;
    vec3 graded = bedrockGrade(exposed);
    vec3 mapped = max(isHdr ? classicCurveHdr(graded, classicPeak) : classicCurve(graded), vec3(0.0));
    mapped = max(withSaturation(mapped, max(pc.saturation, 0.0)), vec3(0.0));
    mapped = pow(mapped, vec3(1.0 / gammaVal));
    if (pc.clampOutput != 0) {
        mapped = clamp(mapped, vec3(0.0), vec3(isHdr ? pow(hdrPeak, 1.0 / 2.2) : 1.0));
    }
    // In SDR the output went to an 8-bit image, which held nothing over 1.0 whatever Clamp Output said. Now that it
    // is a float image, values over 1.0 reached depth of field, motion blur and a pack's grade, and the grade's
    // headroom restore boosted what the pack meant to darken.
    if (!isHdr) { mapped = min(mapped, vec3(1.0)); }
    fragColor = vec4(mapped, 1.0);
}
