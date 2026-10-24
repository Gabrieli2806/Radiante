#ifndef HEIGHT_MAP_GLSL
#define HEIGHT_MAP_GLSL

// Parallax occlusion against a LabPBR height map (the alpha channel of the normal map), traced in the plane of the
// face that was hit. The surface is a shallow layer below the face: height 1 is the face itself, height 0 the
// deepest point. "Depth" throughout is the distance below the face along -baseNormal, in world units.
//
// Two tracers:
//  - nearest: the height map as a field of square columns, one per texel (the vanilla pixel look). The ray walks
//    from texel to texel and can hit a column top or the vertical wall between two columns.
//  - bilinear: a smooth surface, marched in fixed steps and refined by bisection, with the normal taken from the
//    height gradient.

// Turned on around a primary ray's height map trace, so the trace reports where it leaves the tile below the
// surface and the hit shader decides what that edge is. All other traces (shadows, bounces) simply leave the height
// map there.
bool gParallaxDetectEdges = false;

#include "common/shared.hpp"
#include "sampling_helpers.glsl"

// Below this the layer is too thin to trace.
const float heightMapMinWorldDepth = 1e-5;
// How deep the layer is, as a fraction of the tile's world size.
const float heightMapDepthScaleInTexture = 0.25;
// Tolerance used for every depth and boundary comparison in the traces.
const float heightMapTraceBias = 2e-4;
// See shouldFallbackNearestSecondaryToBilinear.
const float heightMapNearestSecondaryBilinearFallbackDepthRate = 0.02;
const int heightMapNearestMaxSteps = 1024;
const int heightMapBilinearMaxSteps = 24;
const int heightMapBilinearBinarySteps = 5;

struct HeightMapHit {
    bool hit;
    bool sideWall; // hit a vertical wall rather than a top
    bool edgeWall; // that wall is where the tile ends
    float t;       // distance travelled along the ray
    vec2 uv;
    float depth;
    vec3 geometricNormal;
};

// Normalises dir, or returns fallback for a (near) zero vector.
vec3 normalizeF(vec3 dir, vec3 fallback) {
    float lenSq = dot(dir, dir);
    if (lenSq <= 1e-12) { return fallback; }
    return dir * inversesqrt(lenSq);
}

// How fast u and v change per unit travelled along dir: the least-squares solution of dir = a * dPdu + b * dPdv
// (normal equations of the 2x2 system). Zero when the two tangents are parallel.
vec2 directionToRateUv(vec3 dir, vec3 dPdu, vec3 dPdv) {
    float uu = dot(dPdu, dPdu);
    float uv = dot(dPdu, dPdv);
    float vv = dot(dPdv, dPdv);
    float gram = uu * vv - uv * uv;
    if (abs(gram) <= 1e-12) { return vec2(0.0); }

    float projU = dot(dPdu, dir);
    float projV = dot(dPdv, dir);
    return vec2((projU * vv - projV * uv) / gram, (projV * uu - projU * uv) / gram);
}

// Depth of the layer for a sprite: a quarter of its larger world-space side.
float heightMapMaxDepthWorld(vec2 minUV, vec2 maxUV, vec3 dPdu, vec3 dPdv) {
    vec2 span = abs(maxUV - minUV);
    if (span.x <= 1e-8 && span.y <= 1e-8) { return 0.0; }

    float worldSizeU = length(dPdu) * span.x;
    float worldSizeV = length(dPdv) * span.y;
    float worldSize = max(worldSizeU, worldSizeV);
    return worldSize * heightMapDepthScaleInTexture;
}

float edgeDepthWithBand(float edgeBaseDepth, float minEdgeDepth) {
    return max(edgeBaseDepth, minEdgeDepth);
}

// Whether the ray, at edgeUV and edgeDepth, leaves the tile through one of its sides below the surface; if so,
// edgeNormal is that side's outward-facing wall normal (towards the ray).
bool isEdge(float edgeDepth,
            float edgeBaseDepth,
            float minEdgeDepth,
            vec2 edgeUV,
            vec2 minUV,
            vec2 maxUV,
            vec2 rateUV,
            vec3 dPdu,
            vec3 dPdv,
            vec3 baseNormal,
            out vec3 edgeNormal) {
    edgeNormal = baseNormal;
    // Such a wall stands right against the block's neighbouring face (or the next block), so neither the sun nor a
    // bounce can light it. It showed up as a black band along every block edge at grazing angles and, as a shadow
    // blocker, darkened the strip next to it too. Unless asked for, the trace just leaves the height map there like
    // at any other exit.
    if (!gParallaxDetectEdges) { return false; }
    float bandDepth = edgeDepthWithBand(edgeBaseDepth, minEdgeDepth);
    if (edgeDepth < -heightMapTraceBias) { return false; }
    if (edgeDepth > bandDepth + heightMapTraceBias) { return false; }

    // Which side the ray is crossing, from its direction of travel.
    if (rateUV.x > 0.0 && edgeUV.x >= maxUV.x - heightMapTraceBias) {
        edgeNormal = normalizeF(-dPdu, baseNormal);
        return true;
    }
    if (rateUV.x < 0.0 && edgeUV.x <= minUV.x + heightMapTraceBias) {
        edgeNormal = normalizeF(dPdu, baseNormal);
        return true;
    }
    if (rateUV.y > 0.0 && edgeUV.y >= maxUV.y - heightMapTraceBias) {
        edgeNormal = normalizeF(-dPdv, baseNormal);
        return true;
    }
    if (rateUV.y < 0.0 && edgeUV.y <= minUV.y + heightMapTraceBias) {
        edgeNormal = normalizeF(dPdv, baseNormal);
        return true;
    }
    return false;
}

// The shallowest an edge wall is allowed to be: two texels' worth of layer depth (never below the trace bias).
float edgeMinDepth(vec2 minUV, vec2 maxUV, ivec2 size, float maxDepth) {
    vec2 spanTexels = abs(maxUV - minUV) * vec2(size);
    float resolution = max(max(spanTexels.x, spanTexels.y), 1.0);
    return max(2.0 * maxDepth / resolution, heightMapTraceBias);
}

// A third of a texel, in UV.
vec2 heightMapEdgeBandUv(ivec2 size) {
    vec2 sizeAtLeastOne = max(vec2(size), vec2(1.0));
    return 1.0 / (3.0 * sizeAtLeastOne);
}

// Normal of the wall (or corner of two walls) the ray ran into after stepping in u and/or v: facing back against
// the direction of travel.
vec3 wallNormal(bool steppedU, bool steppedV, vec2 rateUV, vec3 dPdu, vec3 dPdv, vec3 baseNormal) {
    vec3 facing = vec3(0.0);
    if (steppedU) { facing += (rateUV.x > 0.0 ? -1.0 : 1.0) * dPdu; }
    if (steppedV) { facing += (rateUV.y > 0.0 ? -1.0 : 1.0) * dPdv; }
    return normalizeF(facing, baseNormal);
}

// The next column has the same top as this one and the ray is at or below it: it lands on that top.
bool shouldContinueTexelTop(float currentDepth, float enteredDepth, float rayDepth) {
    return abs(enteredDepth - currentDepth) <= heightMapTraceBias && rayDepth >= enteredDepth - heightMapTraceBias;
}

// The two columns differ in height and the ray crosses between them within the step: it hits the wall between.
bool enteredTexelWall(float currentDepth, float enteredDepth, float rayDepth) {
    float shallow = min(currentDepth, enteredDepth);
    float deep = max(currentDepth, enteredDepth);
    return deep > shallow + heightMapTraceBias && rayDepth >= shallow - heightMapTraceBias &&
           rayDepth <= deep + heightMapTraceBias;
}

// Texel boundaries left to cross along one axis before the tile ends.
int heightMapRemainingSteps(int texelCoord, int minCoord, int maxCoord, float rate) {
    if (rate > 1e-9) { return max(maxCoord - texelCoord, 0); }
    if (rate < -1e-9) { return max(texelCoord - minCoord, 0); }
    return 0;
}

// A ray rising out of the layer at an extremely flat angle crosses many columns per unit of height and resolves
// poorly as columns; callers trace such secondary rays with the bilinear surface instead.
bool shouldFallbackNearestSecondaryToBilinear(vec3 worldDir, vec3 baseNormal) {
    float descent = dot(worldDir, -baseNormal);
    return descent < 0.0 && -descent <= heightMapNearestSecondaryBilinearFallbackDepthRate;
}

// Depth of a column's top. Texels outside the tile count as being at the face (depth 0).
float sampleHeightDepthNearest(
    sampler2D tex, ivec2 texel, ivec2 atlasTexelMin, ivec2 atlasTexelMax, vec2 uv, float maxDepth) {
    if (texel.x < atlasTexelMin.x || texel.x > atlasTexelMax.x || texel.y < atlasTexelMin.y ||
        texel.y > atlasTexelMax.y) {
        return 0.0;
    }
    return (1.0 - clamp(sampleTexture(tex, texel, 0, false).w, 0.0, 1.0)) * maxDepth;
}

// The UV lies within one texel of the tile's border.
bool isEdgeUV(vec2 uv, vec2 minUV, vec2 maxUV, ivec2 size) {
    if (size.x <= 0 || size.y <= 0) { return false; }

    vec2 rectLo = min(minUV, maxUV);
    vec2 rectHi = max(minUV, maxUV);
    vec2 texelUv = 1.0 / vec2(size);
    return uv.x <= rectLo.x + texelUv.x || uv.x >= rectHi.x - texelUv.x || uv.y <= rectLo.y + texelUv.y ||
           uv.y >= rectHi.y - texelUv.y;
}

vec2 heightMapMinUV(vec2 minUV, vec2 maxUV, ivec2 size) {
    return min(minUV, maxUV);
}

vec2 heightMapMaxUV(vec2 minUV, vec2 maxUV, ivec2 size) {
    return max(minUV, maxUV);
}

// The tile rect pulled in by a third of a texel (or by half the tile, if that is smaller).
vec2 heightMapInnerMinUV(vec2 minUV, vec2 maxUV, ivec2 size) {
    vec2 rectLo = min(minUV, maxUV);
    vec2 rectHi = max(minUV, maxUV);
    vec2 inset = min(heightMapEdgeBandUv(size), 0.5 * max(rectHi - rectLo, vec2(0.0)));
    return rectLo + inset;
}

vec2 heightMapInnerMaxUV(vec2 minUV, vec2 maxUV, ivec2 size) {
    vec2 rectLo = min(minUV, maxUV);
    vec2 rectHi = max(minUV, maxUV);
    vec2 inset = min(heightMapEdgeBandUv(size), 0.5 * max(rectHi - rectLo, vec2(0.0)));
    return rectHi - inset;
}

bool isUvWithinHeightMap(vec2 uv, vec2 minUV, vec2 maxUV) {
    return uv.x >= minUV.x && uv.x <= maxUV.x && uv.y >= minUV.y && uv.y <= maxUV.y;
}

// Height (0..1) at a UV, read inside the tile only. An empty texture reads as flat (1).
float sampleHeight(sampler2D tex, vec2 uv, vec2 minUV, vec2 maxUV, int lodLevel, uint samplingMode) {
    ivec2 extent = textureSize(tex, lodLevel);
    if (extent.x <= 0 || extent.y <= 0) { return 1.0; }

    vec2 insideUv = clampUvToRect(uv, minUV, maxUV, extent);
    vec4 texel = samplingMode == 0u ? sampleNearest(tex, insideUv, lodLevel, false) :
                                      sampleBilinear(tex, insideUv, lodLevel, false);
    return clamp(texel.w, 0.0, 1.0);
}

float sampleHeightDepth(
    sampler2D tex, vec2 uv, vec2 minUV, vec2 maxUV, int lodLevel, uint samplingMode, float maxDepth) {
    float height = sampleHeight(tex, uv, minUV, maxUV, lodLevel, samplingMode);
    return (1.0 - height) * maxDepth;
}

// Where between two march samples the ray crossed the tile's border: the earliest crossing of any of the four
// sides, interpolated in t. If it crossed none, the later sample.
float heightMapBoundaryHitT(float tPrev,
                            float tCurr,
                            vec2 uvPrev,
                            vec2 uvCurr,
                            vec2 boundsMin,
                            vec2 boundsMax) {
    float fraction = 1.0;
    bool leftTile = false;

    if (uvCurr.x < boundsMin.x && uvPrev.x >= boundsMin.x) {
        fraction = min(fraction, (boundsMin.x - uvPrev.x) / (uvCurr.x - uvPrev.x));
        leftTile = true;
    }
    if (uvCurr.x > boundsMax.x && uvPrev.x <= boundsMax.x) {
        fraction = min(fraction, (boundsMax.x - uvPrev.x) / (uvCurr.x - uvPrev.x));
        leftTile = true;
    }
    if (uvCurr.y < boundsMin.y && uvPrev.y >= boundsMin.y) {
        fraction = min(fraction, (boundsMin.y - uvPrev.y) / (uvCurr.y - uvPrev.y));
        leftTile = true;
    }
    if (uvCurr.y > boundsMax.y && uvPrev.y <= boundsMax.y) {
        fraction = min(fraction, (boundsMax.y - uvPrev.y) / (uvCurr.y - uvPrev.y));
        leftTile = true;
    }

    return leftTile ? mix(tPrev, tCurr, clamp(fraction, 0.0, 1.0)) : tCurr;
}

// Normal of the smooth height surface at a UV, from forward differences one texel along u and v. Flipped if needed
// so it faces viewDir.
vec3 sampleNormal(sampler2D tex,
                  vec2 uv,
                  vec2 minUV,
                  vec2 maxUV,
                  vec3 dPdu,
                  vec3 dPdv,
                  vec3 fallbackNormal,
                  int lodLevel,
                  uint samplingMode,
                  float maxDepth,
                  vec3 viewDir) {
    ivec2 extent = textureSize(tex, lodLevel);
    if (extent.x <= 0 || extent.y <= 0 || maxDepth <= heightMapMinWorldDepth) { return fallbackNormal; }

    vec2 texelUv = 1.0 / vec2(extent);
    float here = sampleHeightDepth(tex, uv, minUV, maxUV, lodLevel, samplingMode, maxDepth);
    float alongU = sampleHeightDepth(tex, uv + vec2(texelUv.x, 0.0), minUV, maxUV, lodLevel, samplingMode, maxDepth);
    float alongV = sampleHeightDepth(tex, uv + vec2(0.0, texelUv.y), minUV, maxUV, lodLevel, samplingMode, maxDepth);

    // Tangents of the displaced surface: the face tangents tilted by the depth slope.
    vec3 tangentU = dPdu - fallbackNormal * ((alongU - here) / texelUv.x);
    vec3 tangentV = dPdv - fallbackNormal * ((alongV - here) / texelUv.y);
    vec3 surfaceNormal = normalizeF(cross(tangentU, tangentV), fallbackNormal);
    if (dot(surfaceNormal, viewDir) < 0.0) { surfaceNormal = -surfaceNormal; }
    return surfaceNormal;
}

// Fills every field of a hit record at once.
void heightMapRecordHit(
    inout HeightMapHit hit, bool found, bool sideWall, bool edgeWall, float t, vec2 uv, float depth, vec3 normal) {
    hit.hit = found;
    hit.sideWall = sideWall;
    hit.edgeWall = edgeWall;
    hit.t = t;
    hit.uv = uv;
    hit.depth = depth;
    hit.geometricNormal = normal;
}

bool heightMapOutsideTile(ivec2 texel, ivec2 atlasTexelMin, ivec2 atlasTexelMax) {
    return any(lessThan(texel, atlasTexelMin)) || any(greaterThan(texel, atlasTexelMax));
}

// Column tracer (see the top of the file). The ray starts at uv, depth inside the tile; hit is filled when it lands
// on a column top or runs into a column wall (or, when edge detection is on, the tile's side) within maxDistance.
bool traceNearestHeightMap(sampler2D tex,
                           vec2 minUV,
                           vec2 maxUV,
                           vec2 uv,
                           float depth,
                           vec3 worldDir,
                           vec3 dPdu,
                           vec3 dPdv,
                           vec3 baseNormal,
                           float maxDepth,
                           float maxDistance,
                           out HeightMapHit hit) {
    heightMapRecordHit(hit, false, false, false, INF_DISTANCE, uv, depth, baseNormal);

    if (maxDepth <= heightMapMinWorldDepth) { return false; }

    ivec2 size = textureSize(tex, 0);
    if (size.x <= 0 || size.y <= 0) { return false; }

    vec2 boundsMin = heightMapMinUV(minUV, maxUV, size);
    vec2 boundsMax = heightMapMaxUV(minUV, maxUV, size);
    if (!isUvWithinHeightMap(uv, boundsMin, boundsMax)) { return false; }
    float minEdgeDepth = edgeMinDepth(minUV, maxUV, size, maxDepth);

    vec2 rateUV = directionToRateUv(worldDir, dPdu, dPdv);
    float depthRate = dot(worldDir, -baseNormal); // > 0: going down into the layer

    // Tile edges sit on whole texels, but atlas coordinates carry float error. A max edge computed a hair past its
    // texel boundary and rounded up took in the first texel of the next sprite, whose heights then stood up as a
    // one-texel wall: a black line along every block seam, visible only when looking towards that edge. Rounding
    // to the nearest boundary avoids it.
    ivec2 atlasTexelMin = clampTexelCoord(ivec2(round(min(minUV, maxUV) * vec2(size))), size);
    ivec2 atlasTexelMax = clampTexelCoord(ivec2(round(max(minUV, maxUV) * vec2(size))) - ivec2(1), size);
    ivec2 texel = clampTexelCoord(ivec2(floor(uv * vec2(size))), size);
    if (heightMapOutsideTile(texel, atlasTexelMin, atlasTexelMax)) { return false; }

    // Enough iterations to cross the tile on both axes, plus a few for corner and top cases.
    int stepsLeftU = heightMapRemainingSteps(texel.x, atlasTexelMin.x, atlasTexelMax.x, rateUV.x);
    int stepsLeftV = heightMapRemainingSteps(texel.y, atlasTexelMin.y, atlasTexelMax.y, rateUV.y);
    int stepBudget = min(heightMapNearestMaxSteps, stepsLeftU + stepsLeftV + 4);

    float travelled = 0.0;
    vec2 rayUv = uv;
    float rayDepth = depth;

    int iteration = 0;
    while (iteration < stepBudget) {
        if (travelled > maxDistance) { break; }
        if (rayDepth < -heightMapTraceBias) { break; } // came back out above the face
        if (heightMapOutsideTile(texel, atlasTexelMin, atlasTexelMax)) { break; }

        // Already at or below this column's top: hit it where we stand.
        float columnTop = sampleHeightDepthNearest(tex, texel, atlasTexelMin, atlasTexelMax, rayUv, maxDepth);
        if (rayDepth >= columnTop - heightMapTraceBias) {
            heightMapRecordHit(hit, true, false, false, travelled, rayUv, max(rayDepth, columnTop), baseNormal);
            return true;
        }

        // Distances to the three things the ray can meet inside this column: its top, a texel boundary in u or v,
        // or the face above (when rising).
        float toTop = INF_DISTANCE;
        if (depthRate > 1e-6 && columnTop > rayDepth) { toTop = (columnTop - rayDepth) / depthRate; }

        float toU = INF_DISTANCE;
        float toV = INF_DISTANCE;
        bool movesU = false;
        bool movesV = false;

        if (rateUV.x > 1e-9) {
            float border = (float(texel.x + 1)) / float(size.x);
            toU = (border - rayUv.x) / rateUV.x;
            movesU = true;
        } else if (rateUV.x < -1e-9) {
            float border = float(texel.x) / float(size.x);
            toU = (border - rayUv.x) / rateUV.x;
            movesU = true;
        }

        if (rateUV.y > 1e-9) {
            float border = (float(texel.y + 1)) / float(size.y);
            toV = (border - rayUv.y) / rateUV.y;
            movesV = true;
        } else if (rateUV.y < -1e-9) {
            float border = float(texel.y) / float(size.y);
            toV = (border - rayUv.y) / rateUV.y;
            movesV = true;
        }

        float toFace = INF_DISTANCE;
        if (depthRate < -1e-6) { toFace = -rayDepth / depthRate; }

        float toBorder = min(toU, toV);
        if (toTop <= min(toBorder, toFace) + heightMapTraceBias && toTop < INF_DISTANCE) {
            travelled += max(toTop, 0.0);
            heightMapRecordHit(hit, true, false, false, travelled, rayUv + rateUV * max(toTop, 0.0), columnTop,
                               baseNormal);
            return true;
        }

        if (toFace <= toBorder + heightMapTraceBias) { break; } // leaves through the face
        if (toBorder == INF_DISTANCE) { break; }                // not moving across the tile at all

        float advance = max(toBorder, 0.0);
        travelled += advance;
        if (travelled > maxDistance) { break; }

        vec2 borderUv = rayUv + rateUV * advance;
        float borderDepth = rayDepth + depthRate * advance;

        // Crossing exactly through a corner: look at all three columns around it.
        bool throughCorner = movesU && movesV && abs(toU - toV) <= heightMapTraceBias;
        if (throughCorner) {
            ivec2 stepSign = ivec2(rateUV.x > 0.0 ? 1 : -1, rateUV.y > 0.0 ? 1 : -1);
            ivec2 besideU = texel + ivec2(stepSign.x, 0);
            ivec2 besideV = texel + ivec2(0, stepSign.y);
            ivec2 diagonal = texel + ivec2(stepSign.x, stepSign.y);
            bool okU = all(greaterThanEqual(besideU, atlasTexelMin)) && all(lessThanEqual(besideU, atlasTexelMax));
            bool okV = all(greaterThanEqual(besideV, atlasTexelMin)) && all(lessThanEqual(besideV, atlasTexelMax));
            bool okD = all(greaterThanEqual(diagonal, atlasTexelMin)) && all(lessThanEqual(diagonal, atlasTexelMax));

            if (okU && okV && okD) {
                float topU = sampleHeightDepthNearest(tex, besideU, atlasTexelMin, atlasTexelMax, borderUv, maxDepth);
                float topV = sampleHeightDepthNearest(tex, besideV, atlasTexelMin, atlasTexelMax, borderUv, maxDepth);
                float topD = sampleHeightDepthNearest(tex, diagonal, atlasTexelMin, atlasTexelMax, borderUv, maxDepth);
                // A wall across u separates this column from the u neighbour (or the v neighbour from the
                // diagonal); likewise for v.
                bool wallAcrossU = enteredTexelWall(columnTop, topU, borderDepth) ||
                                   enteredTexelWall(topV, topD, borderDepth);
                bool wallAcrossV = enteredTexelWall(columnTop, topV, borderDepth) ||
                                   enteredTexelWall(topU, topD, borderDepth);

                if (wallAcrossU || wallAcrossV) {
                    heightMapRecordHit(hit, true, true, false, travelled, borderUv, borderDepth,
                                       wallNormal(wallAcrossU, wallAcrossV, rateUV, dPdu, dPdv, baseNormal));
                    return true;
                }

                texel = diagonal;
                rayUv = borderUv;
                rayDepth = borderDepth;
                ++iteration;
                continue;
            }
        }

        // Otherwise step across one axis: the nearer border, or on a tie the axis the ray moves faster along.
        bool crossU = false;
        if (toU <= toV + heightMapTraceBias) {
            if (toU < toV - heightMapTraceBias) {
                crossU = true;
            } else {
                crossU = abs(rateUV.x) >= abs(rateUV.y);
            }
        }

        ivec2 nextTexel = crossU ? texel + ivec2(rateUV.x > 0.0 ? 1 : -1, 0) : texel + ivec2(0, rateUV.y > 0.0 ? 1 : -1);
        if (heightMapOutsideTile(nextTexel, atlasTexelMin, atlasTexelMax)) {
            // Reached the tile's side.
            vec3 sideNormal;
            if (isEdge(borderDepth, columnTop, minEdgeDepth, borderUv, boundsMin, boundsMax, rateUV, dPdu, dPdv,
                       baseNormal, sideNormal)) {
                heightMapRecordHit(hit, true, true, true, travelled, clamp(borderUv, boundsMin, boundsMax),
                                   borderDepth, sideNormal);
                return true;
            }
            break;
        }

        float nextTop = sampleHeightDepthNearest(tex, nextTexel, atlasTexelMin, atlasTexelMax, borderUv, maxDepth);
        if (enteredTexelWall(columnTop, nextTop, borderDepth)) {
            heightMapRecordHit(hit, true, true, false, travelled, borderUv, borderDepth,
                               wallNormal(crossU, !crossU, rateUV, dPdu, dPdv, baseNormal));
            return true;
        }
        if (shouldContinueTexelTop(columnTop, nextTop, borderDepth)) {
            heightMapRecordHit(hit, true, false, false, travelled, borderUv, columnTop, baseNormal);
            return true;
        }

        texel = nextTexel;
        rayUv = borderUv;
        rayDepth = borderDepth;
        ++iteration;
    }

    return false;
}

// Smooth-surface tracer (see the top of the file). Marches in steps of about half a texel (or half a texel's worth
// of layer depth), and once the ray is under the surface bisects back to the crossing.
bool traceBilinearHeightMap(sampler2D tex,
                            vec2 minUV,
                            vec2 maxUV,
                            vec2 uv,
                            float depth,
                            vec3 worldDir,
                            vec3 dPdu,
                            vec3 dPdv,
                            vec3 baseNormal,
                            float maxDepth,
                            float maxDistance,
                            out HeightMapHit hit) {
    heightMapRecordHit(hit, false, false, false, INF_DISTANCE, uv, depth, baseNormal);

    if (maxDepth <= heightMapMinWorldDepth) { return false; }

    ivec2 size = textureSize(tex, 0);
    if (size.x <= 0 || size.y <= 0) { return false; }

    vec2 boundsMin = heightMapMinUV(minUV, maxUV, size);
    vec2 boundsMax = heightMapMaxUV(minUV, maxUV, size);
    if (!isUvWithinHeightMap(uv, boundsMin, boundsMax)) { return false; }
    float minEdgeDepth = edgeMinDepth(minUV, maxUV, size, maxDepth);

    vec2 rateUV = directionToRateUv(worldDir, dPdu, dPdv);
    float depthRate = dot(worldDir, -baseNormal);
    float texelsPerUnit = max(abs(rateUV.x) * float(size.x), abs(rateUV.y) * float(size.y));
    float depthTexelsPerUnit = abs(depthRate) / max(maxDepth, heightMapMinWorldDepth) * float(max(size.x, size.y));
    float stride = 0.5 / max(max(texelsPerUnit, depthTexelsPerUnit), 1e-4);

    float tBehind = 0.0;
    vec2 uvBehind = uv;
    float surfaceAtStart = sampleHeightDepth(tex, uvBehind, minUV, maxUV, 0, 1u, maxDepth);
    // Already under the surface where the ray enters.
    if (depth - surfaceAtStart >= -heightMapTraceBias) {
        hit.hit = true;
        hit.t = 0.0;
        hit.uv = uvBehind;
        hit.depth = surfaceAtStart;
        hit.geometricNormal =
            sampleNormal(tex, uvBehind, minUV, maxUV, dPdu, dPdv, baseNormal, 0, 1u, maxDepth, -worldDir);
        return true;
    }

    for (int march = 0; march < heightMapBilinearMaxSteps; ++march) {
        float tAhead = min(tBehind + stride, maxDistance);
        vec2 uvAhead = uv + rateUV * tAhead;
        float depthAhead = depth + depthRate * tAhead;
        if (!isUvWithinHeightMap(uvAhead, boundsMin, boundsMax)) {
            // Left the tile during this step: maybe through its side, below the surface.
            float tSide = heightMapBoundaryHitT(tBehind, tAhead, uvBehind, uvAhead, boundsMin, boundsMax);
            vec2 uvSide = clamp(uv + rateUV * tSide, boundsMin, boundsMax);
            float depthSide = depth + depthRate * tSide;
            vec3 sideNormal;
            // The surface depth at the start is used here (it is not updated while marching).
            float surfaceAtSide = max(surfaceAtStart, sampleHeightDepth(tex, uvSide, minUV, maxUV, 0, 1u, maxDepth));
            if (isEdge(depthSide, surfaceAtSide, minEdgeDepth, uvSide, boundsMin, boundsMax, rateUV, dPdu, dPdv,
                       baseNormal, sideNormal)) {
                heightMapRecordHit(hit, true, true, true, tSide, uvSide, depthSide, sideNormal);
                return true;
            }
            break;
        }
        if (depthAhead < -heightMapTraceBias) { break; } // rose out above the face

        float surfaceAhead = sampleHeightDepth(tex, uvAhead, minUV, maxUV, 0, 1u, maxDepth);
        if (depthAhead - surfaceAhead >= -heightMapTraceBias) {
            // The crossing lies between tBehind and tAhead: bisect.
            float above = tBehind;
            float below = tAhead;
            for (int halving = 0; halving < heightMapBilinearBinarySteps; ++halving) {
                float tMid = 0.5 * (above + below);
                vec2 uvMid = uv + rateUV * tMid;
                float depthMid = depth + depthRate * tMid;
                float surfaceMid = sampleHeightDepth(tex, uvMid, minUV, maxUV, 0, 1u, maxDepth);
                if (depthMid >= surfaceMid) {
                    below = tMid;
                } else {
                    above = tMid;
                }
            }

            float tFound = below;
            vec2 uvFound = uv + rateUV * tFound;
            float surfaceFound = sampleHeightDepth(tex, uvFound, minUV, maxUV, 0, 1u, maxDepth);
            heightMapRecordHit(hit, true, false, false, tFound, uvFound, surfaceFound,
                               sampleNormal(tex, uvFound, minUV, maxUV, dPdu, dPdv, baseNormal, 0, 1u, maxDepth,
                                            -worldDir));
            return true;
        }

        if (tAhead >= maxDistance - 1e-6) { break; }
        tBehind = tAhead;
        uvBehind = uvAhead;
    }

    return false;
}

// Traces the height map with the tracer matching the texture's sampling mode (0 = columns, otherwise smooth).
bool traceHeightMap(sampler2D tex,
                    vec2 minUV,
                    vec2 maxUV,
                    vec2 uv,
                    float depth,
                    vec3 worldDir,
                    vec3 dPdu,
                    vec3 dPdv,
                    vec3 baseNormal,
                    float maxDepth,
                    float maxDistance,
                    uint samplingMode,
                    out HeightMapHit hit) {
    if (samplingMode == 0u) {
        return traceNearestHeightMap(tex, minUV, maxUV, uv, depth, worldDir, dPdu, dPdv, baseNormal, maxDepth,
                                     maxDistance, hit);
    }

    return traceBilinearHeightMap(tex, minUV, maxUV, uv, depth, worldDir, dPdu, dPdv, baseNormal, maxDepth,
                                  maxDistance, hit);
}

#endif
