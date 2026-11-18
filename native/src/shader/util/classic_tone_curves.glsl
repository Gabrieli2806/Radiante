#ifndef CLASSIC_TONE_CURVES_GLSL
#define CLASSIC_TONE_CURVES_GLSL

// The classic tone curves (tone mapping methods 0 to 5), shared by the tone mapping pass, which applies them, and the
// exposure pass, which works out once a frame where the chosen one crosses HDR_KNEE for the HDR extension.

// Where HDR output's highlights leave the SDR curve: up to here the picture matches SDR exactly.
const float HDR_KNEE = 0.75;

// Khronos PBR Neutral: https://github.com/KhronosGroup/ToneMapping/tree/main/PBR_Neutral
vec3 pbrNeutral(vec3 color, float targetPeak) {
    float startCompression = 0.76;
    float desaturation = 0.01;

    float x = min(color.r, min(color.g, color.b));
    float offset = (x < 0.08) ? (x - 6.25 * x * x) : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;

    float d = max(targetPeak - startCompression, 1e-4);
    float newPeak = targetPeak - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * max(peak - newPeak, 0.0) + 1.0);
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

vec3 classicCurveFor(vec3 color, int method, float whitePoint) {
    switch (method) {
        case 1: return reinhard(color);
        case 2: return reinhardWhitePoint(color, whitePoint);
        case 3: return aces(color);
        case 4: return acesWhitePoint(color, whitePoint);
        case 5: return uncharted2(color, whitePoint);
        case 0:
        default: return pbrNeutral(color, 1.0);
    }
}

float classicCurveScalarFor(float x, int method, float whitePoint) {
    return classicCurveFor(vec3(max(x, 0.0)), method, whitePoint).r;
}

// The input at which the curve reaches HDR_KNEE, by bisection. The same for every pixel of a frame; it was searched
// for again in every highlight pixel of the tone mapping pass.
float classicCurveKnee(int method, float whitePoint) {
    float lo = 0.0;
    float hi = max(whitePoint, 16.0);
    while (classicCurveScalarFor(hi, method, whitePoint) < HDR_KNEE && hi < 4096.0) { hi *= 2.0; }
    for (int i = 0; i < 24; i++) {
        float mid = 0.5 * (lo + hi);
        if (classicCurveScalarFor(mid, method, whitePoint) < HDR_KNEE) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

#endif
