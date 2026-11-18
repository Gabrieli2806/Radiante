#ifndef RADIANTE_UTIL_THIN_LENS_GLSL
#define RADIANTE_UTIL_THIN_LENS_GLSL

// Physical thin-lens camera aperture sampling (Photo Mode), binding-free.
// Maps a Cranley-Patterson rotated R2 2D sample onto either a concentric unit disk (blades < 3) or a regular
// polygon of `blades` sides, then aims the shifted view-space ray at the pinhole ray's intersection with the flat
// focal plane z = -focusDistance.

vec2 thinLensSampleDisk(vec2 u) {
    vec2 offset = 2.0 * u - 1.0;
    if (offset.x == 0.0 && offset.y == 0.0) {
        return vec2(0.0);
    }
    float r;
    float theta;
    if (abs(offset.x) > abs(offset.y)) {
        r = offset.x;
        theta = 0.78539816339 * (offset.y / offset.x);
    } else {
        r = offset.y;
        theta = 1.57079632679 - 0.78539816339 * (offset.x / offset.y);
    }
    return r * vec2(cos(theta), sin(theta));
}

vec2 thinLensSamplePolygon(vec2 u, float blades, float rotation) {
    float n = floor(max(blades, 3.0));
    float sector = min(floor(u.x * n), n - 1.0);
    float localU = clamp(u.x * n - sector, 0.0, 1.0);
    // Uniform over the isosceles sector triangle (0, v0, v1).
    float su = sqrt(clamp(u.y, 0.0, 1.0));
    float w0 = 1.0 - su;
    float w1 = su * (1.0 - localU);
    float w2 = su * localU;
    float stepAngle = 6.28318530718 / n;
    float a0 = rotation + sector * stepAngle;
    float a1 = a0 + stepAngle;
    return w1 * vec2(cos(a0), sin(a0)) + w2 * vec2(cos(a1), sin(a1)) + w0 * vec2(0.0);
}

void sampleThinLensViewRay(vec3 viewNearPos,
                           vec4 cameraLens,
                           uvec2 pixel,
                           uint seed,
                           uint frameCounter,
                           out vec3 viewOrigin,
                           out vec3 viewDir) {
    // Per-pixel Cranley-Patterson offset combined with the low-discrepancy R2 sequence across accumulated frames.
    uint h0 = xxhash32(uvec3(pixel, 0x9e3779b9u));
    uint h1 = xxhash32(uvec3(pixel.yx, 0x85ebca6bu));
    vec2 cp = vec2(float(h0) * (1.0 / 4294967296.0), float(h1) * (1.0 / 4294967296.0));
    float idx = float((frameCounter & 0xffffu) + 1u);
    vec2 r2 = fract(cp + vec2(0.7548776662 * idx, 0.5698402910 * idx));
    // Mix a tiny per-frame jitter if two launches share frameCounter.
    uint frameSalt = xxhash32(uvec3(pixel, seed ^ 0xc2b2ae35u));
    vec2 u = fract(r2 + vec2(float(frameSalt & 0xffffu), float(frameSalt >> 16u)) * (1.0 / 4294967296.0));

    vec2 aperturePoint = cameraLens.z >= 3.0 ? thinLensSamplePolygon(u, cameraLens.z, cameraLens.w)
                                             : thinLensSampleDisk(u);
    vec3 pinholeDir = normalize(viewNearPos);
    float focusDistance = max(cameraLens.y, 0.05);
    float tFocus = focusDistance / max(abs(pinholeDir.z), 1e-4);
    vec3 focalPoint = pinholeDir * tFocus;

    viewOrigin = vec3(aperturePoint * cameraLens.x, 0.0);
    viewDir = normalize(focalPoint - viewOrigin);
}

#endif
