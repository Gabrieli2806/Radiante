#ifndef VPT_PT_RESTIR_GI_GLSL
#define VPT_PT_RESTIR_GI_GLSL

// ReSTIR GI (Ouyang et al. 2021, "ReSTIR GI: Path Resampling for Real-Time Path Tracing") on the first bounce of the
// surface each pixel sees first, when that bounce took the diffuse lobe.
//
// The first bounce is traced as before (traceSecondary, SHaRC answering for the deeper bounces), and what it found is
// kept as a sample: the point it reached (x2), that point's normal, and the light leaving it towards the first hit
// (Lo). The light the bounce brought is the throughput of the first hit's diffuse lobe times Lo, and the throughput
// of a cosine-sampled diffuse lobe does not depend on the direction; so what the pixel needs from its bounce is the
// cosine-weighted average of Lo over the hemisphere, E = integral of Lo cos / pi. That is what the reservoirs
// estimate, with Lo cos as the target. Each pixel merges its new sample with last frame's reservoir of the same
// surface (found by reprojection) and with a few of its neighbours' (reconnected to this surface, with the Jacobian
// of the move), and shades with the winner: E ~ Lo(y) cos(y) / pi * W. The light of every pixel's bounce then comes
// from the best of some twenty samples, not one.
//
// Like the block lights' ReSTIR (common/block_light.glsl) it runs in the one world pass, reading only last frame's
// reservoirs; a neighbour's sample is checked for visibility from this surface before it is used.
//
// Reservoirs: three RGBA32F layers per frame, frames alternating (binding VPT_RESTIR_GI_BINDING):
//   0: x2 (relative to that frame's camera), W
//   1: Lo.rg (half), Lo.b and M (half), x2's normal (snorm8), age in frames
//   2: x1 (relative to that frame's camera), x1's normal (snorm8)
// A sample that left to the sky has a zero normal: its x2 is a point far along its direction.

#ifndef VPT_RESTIR_GI
#    define VPT_RESTIR_GI 0
#endif

#if VPT_RESTIR_GI != 0

#    ifndef VPT_RESTIR_GI_BINDING
#        define VPT_RESTIR_GI_BINDING 48
#    endif
layout(set = 5, binding = VPT_RESTIR_GI_BINDING, rgba32f) uniform image2DArray vptRestirGiImage;

// How many samples a reservoir may stand for. Higher is smoother and slower to follow a change of light.
#    define RESTIR_GI_MAX_M 20.0
// A neighbour's reservoir counts for this share of the cap: it is another surface, reconnected.
#    define RESTIR_GI_SPATIAL_M_SHARE 0.25
#    define RESTIR_GI_SPATIAL_SAMPLES 3
#    define RESTIR_GI_SPATIAL_RADIUS 16.0
// A sample is dropped after this many frames, so light that went out does not linger in the reservoirs.
#    define RESTIR_GI_MAX_AGE 30.0
// A reconnection that changes the solid angle by more than this factor either way is not taken: near contact the
// Jacobian is large and noisy, and the sample would be a firefly.
#    define RESTIR_GI_MAX_JACOBIAN 8.0
#    define RESTIR_GI_SKY_DISTANCE 4096.0

struct RestirGiSample {
    vec3 x2;
    vec3 n2; // zero: the sky
    vec3 Lo;
    float age;
};

struct RestirGiReservoir {
    RestirGiSample s;
    float wSum;
    float M;
    float W;
    float target;
};

/** The second hit's normal the first bounce reached; set by traceSecondary. */
vec3 g_restirGiSecondNormal = vec3(0.0);

float restirGiLuminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

RestirGiReservoir restirGiEmpty() {
    RestirGiReservoir r;
    r.s.x2 = vec3(0.0);
    r.s.n2 = vec3(0.0);
    r.s.Lo = vec3(0.0);
    r.s.age = 0.0;
    r.wSum = 0.0;
    r.M = 0.0;
    r.W = 0.0;
    r.target = 0.0;
    return r;
}

/** The target at the surface (x1, n1) for sample s: Lo's luminance times the cosine there, 0 below the surface. */
float restirGiTarget(vec3 x1, vec3 n1, RestirGiSample s) {
    vec3 d = s.x2 - x1;
    float len = length(d);
    if (len < 1e-4) { return 0.0; }
    return restirGiLuminance(s.Lo) * max(dot(n1, d / len), 0.0);
}

/**
 * The Jacobian of moving sample s from the surface point it was found from (from) to x1: the ratio of the solid
 * angles x2 covers seen from the two. 1 for the sky, which is as far from both.
 */
float restirGiJacobian(vec3 from, vec3 x1, RestirGiSample s) {
    if (dot(s.n2, s.n2) < 0.5) { return 1.0; }
    vec3 toNew = x1 - s.x2;
    vec3 toOld = from - s.x2;
    float newDist2 = max(dot(toNew, toNew), 1e-6);
    float oldDist2 = max(dot(toOld, toOld), 1e-6);
    float cosNew = abs(dot(s.n2, toNew * inversesqrt(newDist2)));
    float cosOld = abs(dot(s.n2, toOld * inversesqrt(oldDist2)));
    if (cosOld < 1e-4) { return 0.0; }
    return (cosNew / cosOld) * (oldDist2 / newDist2);
}

void restirGiStore(ivec2 pixel, uint half_, RestirGiReservoir r, vec3 x1, vec3 n1) {
    int base = int(half_ * 3u);
    imageStore(vptRestirGiImage, ivec3(pixel, base), vec4(r.s.x2, r.W));
    imageStore(vptRestirGiImage, ivec3(pixel, base + 1),
               vec4(uintBitsToFloat(packHalf2x16(r.s.Lo.rg)), uintBitsToFloat(packHalf2x16(vec2(r.s.Lo.b, r.M))),
                    uintBitsToFloat(packSnorm4x8(vec4(r.s.n2, 0.0))), r.s.age));
    imageStore(vptRestirGiImage, ivec3(pixel, base + 2),
               vec4(x1, uintBitsToFloat(packSnorm4x8(vec4(n1, 1.0)))));
}

/** Last frame's reservoir at pixel, moved to this frame's camera; false when there is none. */
bool restirGiLoad(ivec2 pixel, uint half_, vec3 cameraDelta, out RestirGiReservoir r, out vec3 x1, out vec3 n1) {
    int base = int(half_ * 3u);
    vec4 a = imageLoad(vptRestirGiImage, ivec3(pixel, base));
    vec4 b = imageLoad(vptRestirGiImage, ivec3(pixel, base + 1));
    vec4 c = imageLoad(vptRestirGiImage, ivec3(pixel, base + 2));
    r = restirGiEmpty();
    vec4 n1w = unpackSnorm4x8(floatBitsToUint(c.w));
    n1 = n1w.xyz;
    // Positions are relative to the camera of the frame that stored them: this frame's camera moved by cameraDelta.
    x1 = c.xyz - cameraDelta;
    if (n1w.w < 0.5) { return false; }
    vec2 rg = unpackHalf2x16(floatBitsToUint(b.x));
    vec2 bM = unpackHalf2x16(floatBitsToUint(b.y));
    r.s.x2 = a.xyz - cameraDelta;
    r.s.n2 = unpackSnorm4x8(floatBitsToUint(b.z)).xyz;
    r.s.Lo = vec3(rg, bM.x);
    r.s.age = b.w;
    r.W = a.w;
    r.M = bM.y;
    if (!(r.M > 0.0) || !(r.W >= 0.0) || isinf(r.W) || isnan(r.W) || any(isnan(r.s.Lo)) || any(isinf(r.s.Lo))) {
        return false;
    }
    return true;
}

/** Merges reservoir q (found at surface point from) into r at the surface (x1, n1). */
bool restirGiMerge(inout RestirGiReservoir r, RestirGiReservoir q, vec3 from, vec3 x1, vec3 n1, inout uint seed) {
    float target = restirGiTarget(x1, n1, q.s);
    float jacobian = restirGiJacobian(from, x1, q.s);
    if (jacobian > RESTIR_GI_MAX_JACOBIAN || jacobian < 1.0 / RESTIR_GI_MAX_JACOBIAN) { target = 0.0; }
    float weight = target * q.W * q.M * jacobian;
    r.M += q.M;
    if (!(weight > 0.0)) { return false; }
    r.wSum += weight;
    if (rand(seed) * r.wSum < weight) {
        r.s = q.s;
        r.target = target;
        return true;
    }
    return false;
}

/** Whether x2 can be seen from origin (the first hit, lifted off its surface). */
bool restirGiVisible(vec3 origin, vec3 x2, bool sky) {
    vec3 d = x2 - origin;
    float len = length(d);
    if (len < 1e-3) { return true; }
    shadowRay.radiance = vec3(0.0);
    shadowRay.throughput = vec3(1.0);
    shadowRay.insideBoat = 0u;
    shadowRay.pad0 = BLOCK_LIGHT_QUERY;
    // Stops a little short of a surface sample, so the surface itself does not count as in the way.
    traceRayEXT(topLevelAS, VPT_SHADOW_RAY_FLAGS, WORLD_MASK, 0, 0, 0, origin, 0.0001, d / len,
                sky ? len : max(len - 0.02, 0.0002), 1);
    shadowRay.pad0 = 0u;
    return dot(shadowRay.radiance * shadowRay.throughput, vec3(1.0)) > 0.0;
}

/**
 * ReSTIR GI for the pixel's first diffuse bounce. In: the first hit (x1 and its normal, relative to this frame's
 * camera), where the bounce left from (origin, lifted off the surface), the bounce's direction and the throughput it
 * carried (throughput), what it brought back (bounceRadiance: everything traceSecondary added), and whether and how
 * far it hit. Returns the bounce's light to use instead; hitDistance becomes the winner's distance (0: the sky).
 */
vec3 restirGiResolve(ivec2 pixel,
                     ivec2 size,
                     vec3 x1,
                     vec3 n1,
                     vec3 origin,
                     vec3 direction,
                     vec3 throughput,
                     vec3 bounceRadiance,
                     bool hit,
                     inout float hitDistance,
                     vec3 cameraDelta,
                     mat4 prevViewProj,
                     inout uint seed) {
    uint writeHalf = worldUBO.frameCounter & 1u;
    uint readHalf = writeHalf ^ 1u;

    // 1. This frame's sample. Lo is what came back divided by the throughput that carried it.
    RestirGiSample fresh;
    fresh.Lo = max(bounceRadiance, vec3(0.0)) / max(throughput, vec3(1e-6));
    fresh.Lo = mix(fresh.Lo, vec3(0.0), lessThanEqual(throughput, vec3(1e-6)));
    fresh.x2 = hit ? origin + direction * hitDistance : x1 + direction * RESTIR_GI_SKY_DISTANCE;
    fresh.n2 = hit ? g_restirGiSecondNormal : vec3(0.0);
    if (hit && dot(fresh.n2, fresh.n2) < 0.25) { fresh.n2 = -direction; }
    if (hit) { fresh.n2 = normalize(fresh.n2); }
    fresh.age = 0.0;

    RestirGiReservoir r = restirGiEmpty();
    // The source pdf of a cosine-sampled bounce is cos / pi, so its weight is target / pdf = pi * luminance(Lo).
    float freshTarget = restirGiTarget(x1, n1, fresh);
    r.s = fresh;
    r.target = freshTarget;
    r.wSum = freshTarget > 0.0 ? PI * restirGiLuminance(fresh.Lo) : 0.0;
    r.M = 1.0;

    // 2. Temporal reuse: last frame's reservoir of this surface.
    vec4 previousClip = prevViewProj * vec4(x1 + cameraDelta, 1.0);
    bool havePrevious = previousClip.w > 1e-4;
    vec2 previousPixel = havePrevious ? (previousClip.xy / previousClip.w * 0.5 + 0.5) * vec2(size) : vec2(-1.0);
    float x1Distance = length(x1);
    if (havePrevious) {
        ivec2 p = ivec2(floor(previousPixel));
        if (all(greaterThanEqual(p, ivec2(0))) && all(lessThan(p, size))) {
            RestirGiReservoir q;
            vec3 qx1, qn1;
            if (restirGiLoad(p, readHalf, cameraDelta, q, qx1, qn1) && dot(qn1, n1) > 0.9 &&
                length(qx1 - x1) < 0.05 * x1Distance + 0.05 && q.s.age + 1.0 < RESTIR_GI_MAX_AGE) {
                q.M = min(q.M, RESTIR_GI_MAX_M);
                q.s.age += 1.0;
                restirGiMerge(r, q, qx1, x1, n1, seed);
            }
        }
    }
    RestirGiReservoir temporal = r;

    // 3. Spatial reuse: a few of last frame's reservoirs around the same place, on (about) the same plane.
    bool fromNeighbour = false;
    if (havePrevious) {
        for (int i = 0; i < RESTIR_GI_SPATIAL_SAMPLES; i++) {
            float angle = rand(seed) * 2.0 * PI;
            float radius = sqrt(rand(seed)) * RESTIR_GI_SPATIAL_RADIUS;
            ivec2 p = ivec2(floor(previousPixel + vec2(cos(angle), sin(angle)) * radius));
            if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, size))) { continue; }
            RestirGiReservoir q;
            vec3 qx1, qn1;
            if (!restirGiLoad(p, readHalf, cameraDelta, q, qx1, qn1)) { continue; }
            if (dot(qn1, n1) < 0.9 || abs(dot(qx1 - x1, n1)) > 0.05 * x1Distance + 0.05) { continue; }
            if (q.s.age + 1.0 >= RESTIR_GI_MAX_AGE) { continue; }
            q.M = min(q.M, RESTIR_GI_MAX_M * RESTIR_GI_SPATIAL_M_SHARE);
            q.s.age += 1.0;
            if (restirGiMerge(r, q, qx1, x1, n1, seed)) {
                fromNeighbour = true;
            }
        }
    }

    // A neighbour's sample may be hidden from this surface: then the reservoir before the neighbours stands.
    if (fromNeighbour && !restirGiVisible(origin, r.s.x2, dot(r.s.n2, r.s.n2) < 0.5)) { r = temporal; }

    r.W = r.target > 0.0 ? r.wSum / (r.M * r.target) : 0.0;
    r.M = min(r.M, RESTIR_GI_MAX_M);
    restirGiStore(pixel, writeHalf, r, x1, n1);

    if (r.target <= 0.0) {
        hitDistance = 0.0;
        return vec3(0.0);
    }
    vec3 toSample = r.s.x2 - x1;
    float distance = length(toSample);
    float cosine = max(dot(n1, toSample / max(distance, 1e-6)), 0.0);
    hitDistance = dot(r.s.n2, r.s.n2) < 0.5 ? 0.0 : distance;
    // E ~ Lo cos / pi * W, carried by the diffuse lobe's throughput.
    return throughput * r.s.Lo * (cosine / PI) * r.W;
}

/** No reservoir this frame for this pixel (it saw the sky, glass, water, or took a specular bounce). */
void restirGiClear(ivec2 pixel) {
    int base = int((worldUBO.frameCounter & 1u) * 3u);
    imageStore(vptRestirGiImage, ivec3(pixel, base + 2), vec4(0.0));
}

#endif // VPT_RESTIR_GI != 0

#endif // VPT_PT_RESTIR_GI_GLSL
