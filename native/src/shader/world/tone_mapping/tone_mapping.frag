#version 460

// Tone mapping: the HDR radiance exposed (plus bloom), then either one of the classic curves with the grade, or the
// Bedrock-style tone curve and grade (method 6), into the displayable image.

layout(set = 0, binding = 0) uniform sampler2D hdrImage;
// The bloom pyramid's result at half size; the HDR image again while bloom is off (bloomIntensity is 0 then).
layout(set = 0, binding = 3) uniform sampler2D bloomImage;

layout(set = 0, binding = 2) readonly buffer ExposureBuffer {
    float exposure;
    float avgLogLum;
    float padding0;
    float padding1;
}
expData;

// The Bedrock-style tone curve built by bedrock_curve.comp (method 6 only).
layout(std430, set = 0, binding = 4) readonly buffer BedrockCurveBuffer {
    uint histogram[256];
    float curve[256];
}
bedrock;

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
}
pc;

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 fragColor;

const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

// ---- The classic curves (method 0 to 5).

// Khronos PBR Neutral: https://github.com/KhronosGroup/ToneMapping/tree/main/PBR_Neutral
vec3 pbrNeutral(vec3 color) {
    float startCompression = 0.76;
    float desaturation = 0.01;

    float x = min(color.r, min(color.g, color.b));
    float offset = (x < 0.08) ? (x - 6.25 * x * x) : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;

    float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, newPeak * vec3(1.0), g);
}

// The rest as collected at https://64.github.io/tonemapping/
vec3 reinhard(vec3 color) {
    return color / (1.0 + color);
}

vec3 reinhardWhitePoint(vec3 color, float whitePoint) {
    float whiteSquared = max(whitePoint * whitePoint, 1e-6);
    return (color * (1.0 + color / whiteSquared)) / (1.0 + color);
}

// Narkowicz's fit of the ACES curve, unclamped.
vec3 acesCurve(vec3 color) {
    vec3 numerator = color * (2.51 * color + 0.03);
    vec3 denominator = color * (2.43 * color + 0.59) + 0.14;
    return numerator / max(denominator, vec3(1e-6));
}

vec3 aces(vec3 color) {
    return clamp(acesCurve(color), 0.0, 1.0);
}

vec3 acesWhitePoint(vec3 color, float whitePoint) {
    float whiteScale = 1.0 / max(acesCurve(vec3(whitePoint)).r, 1e-6);
    return clamp(acesCurve(color) * whiteScale, 0.0, 1.0);
}

vec3 uncharted2Curve(vec3 x) {
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 uncharted2(vec3 color, float whitePoint) {
    vec3 mapped = uncharted2Curve(color);
    float whiteScale = 1.0 / max(uncharted2Curve(vec3(whitePoint)).r, 1e-6);
    return mapped * whiteScale;
}

vec3 classicCurve(vec3 color) {
    switch (pc.toneMappingMethod) {
        case 1: return reinhard(color);
        case 2: return reinhardWhitePoint(color, pc.whitePoint);
        case 3: return aces(color);
        case 4: return acesWhitePoint(color, pc.whitePoint);
        case 5: return uncharted2(color, pc.whitePoint);
        case 0:
        default: return pbrNeutral(color);
    }
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
// encoding.
vec3 bedrockToneMap(vec3 color) {
    float luminance = dot(color, LUMA);
    vec3 mapped = luminance > 0.0 ? color * (exp2(bedrockCurveAt(log2(max(luminance, exp2(-24.0))))) / luminance)
                                  : vec3(0.0);

    if (pc.filmicSaturation != 0.0) {
        float mean = dot(mapped, vec3(1.0 / 3.0));
        float response = ((1486.4 - 1489.7 * mean) * mean - 3.3) / ((0.15 * mean + 944.2) * mean + 1.0);
        mapped = mean + (1.0 + pc.filmicSaturation * max(response - 1.0, 0.0)) * (mapped - mean);
    }

    // The Nether (exposure.comp flags it): a lava sea or a lit wall of red rock has its red channel far over white
    // once the eye is open, and clipped channel by channel it went a flat pale peach with no texture left. The
    // brightest channel is brought down to the white point instead, keeping the hue: bright orange, darker where
    // the glow is stronger, and the pattern stays.
    if (expData.padding0 > 0.5) {
        float peak = max(mapped.r, max(mapped.g, mapped.b));
        mapped *= NETHER_HUE_HOLD / max(peak, NETHER_HUE_HOLD);
    }

    vec3 graded = mapped * vec3(pc.balanceR, pc.balanceG, pc.balanceB);
    graded = clamp((graded - 0.18) * pc.contrast + 0.18, 0.0, 1.0);
    float gradedLuma = dot(graded, LUMA);
    graded = max(gradedLuma + (graded - gradedLuma) * max(pc.saturation, 0.0), vec3(0.0));
    graded = pow(graded, vec3(pc.gamma));
    graded = mapped + BEDROCK_TONE_MAPPING_INTENSITY * (graded - mapped);
    return clamp(encodeSrgb(graded), 0.0, 1.0);
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

    // Capped again after the exposure, which can lift a large value past what a float divides cleanly.
    vec3 exposed = clamp(hdr * max(currentExposure(), 0.0), vec3(0.0), vec3(1.0e6));
    if (pc.bloomIntensity > 0.0) { exposed += max(texture(bloomImage, texCoord).rgb, vec3(0.0)) * pc.bloomIntensity; }

    if (pc.toneMappingMethod == 6) {
        fragColor = vec4(bedrockToneMap(exposed), 1.0);
        return;
    }

    vec3 mapped = max(classicCurve(bedrockGrade(exposed)), vec3(0.0));
    mapped = max(withSaturation(mapped, max(pc.saturation, 0.0)), vec3(0.0));
    mapped = pow(mapped, vec3(1.0 / max(pc.gamma, 1e-3)));
    if (pc.clampOutput != 0) mapped = clamp(mapped, vec3(0.0), vec3(1.0));
    fragColor = vec4(mapped, 1.0);
}
