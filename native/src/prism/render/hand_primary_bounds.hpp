#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <span>
#include <cmath>
#include <limits>

// NDC envelope of this frame's hand triangles. Geometry crossing the eye plane has an unbounded projection:
// retaining the original full-screen trace is safer than clipping it against a guessed hand near plane.
namespace hand_primary {
// Matches HAND_MASK in common/mapping.hpp and EntityManager.RAY_TRACING_HAND.
inline constexpr unsigned mask = 0x08u;
inline glm::vec4 fullScreen() { return {-2.0f, -2.0f, 2.0f, 2.0f}; }
inline bool finite(glm::vec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

// All triangle interiors project inside their vertices' rectangle while homogeneous w has one sign.
// Per-vertex interval padding avoids the very loose box formed by combining the left and right hands.
inline glm::vec4 projectPoints(std::span<const glm::vec3> positions, const glm::mat4 &objectToView,
                               const glm::mat4 &projection, float handFovScale) {
    if (positions.empty() || !std::isfinite(handFovScale)) return fullScreen();
    const double scale = handFovScale > 0.0f && std::abs(handFovScale - 1.0f) > 1e-4f ? handFovScale : 1.0f;
    if (projection[0][3] != 0.0f || projection[1][3] != 0.0f || projection[3][3] != 0.0f ||
        projection[2][3] == 0.0f || objectToView[0][3] != 0.0f || objectToView[1][3] != 0.0f ||
        objectToView[2][3] != 0.0f || objectToView[3][3] != 1.0f) return fullScreen();
    const glm::dmat4 viewMatrix(objectToView), proj(projection);
    glm::dvec4 nearPoint = glm::inverse(proj) * glm::dvec4(0,0,0,1);
    if (!std::isfinite(nearPoint.w) || std::abs(nearPoint.w) < 1e-8) return fullScreen();
    nearPoint /= nearPoint.w;
    const double forwardW = (proj * nearPoint).w;
    if (!std::isfinite(forwardW) || std::abs(forwardW) < 1e-8) return fullScreen();
    const double forwardSign = forwardW > 0.0 ? 1.0 : -1.0;
    glm::dvec2 lower(std::numeric_limits<double>::infinity()), upper(-std::numeric_limits<double>::infinity());
    for (const glm::vec3 &position : positions) {
        if (!finite(position)) return fullScreen();
        const glm::dvec3 pad = glm::abs(glm::dvec3(position)) * 1e-5 + glm::dvec3(1e-4);
        glm::dvec4 view = viewMatrix * glm::dvec4(position,1);
        glm::dvec3 viewError(0);
        for (int row=0;row<3;++row) for (int col=0;col<3;++col)
            viewError[row] += std::abs(viewMatrix[col][row]) * pad[col];
        view.x /= scale; view.y /= scale;
        viewError.x /= scale; viewError.y /= scale;
        const glm::dvec4 clip = proj * view;
        glm::dvec4 clipError(0);
        for (int row=0;row<4;++row) for (int col=0;col<3;++col)
            clipError[row] += std::abs(proj[col][row]) * viewError[col];
        const double safeW = clip.w * forwardSign - clipError.w;
        if (!(safeW > 1e-4) || !std::isfinite(safeW)) return fullScreen();
        const glm::dvec2 ndc = glm::dvec2(clip) / clip.w;
        // |(x+dx)/(w+dw) - x/w| <= (|dx| + |x/w| |dw|) / (|w|-|dw|).
        const glm::dvec2 error = (glm::dvec2(clipError) + glm::abs(ndc)*clipError.w) / safeW + glm::dvec2(1e-5);
        if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y) || !std::isfinite(error.x) || !std::isfinite(error.y))
            return fullScreen();
        lower = glm::min(lower,ndc-error);
        upper = glm::max(upper,ndc+error);
    }
    return {glm::vec2(lower),glm::vec2(upper)};
}

inline glm::vec4 project(glm::vec3 lo, glm::vec3 hi, const glm::mat4 &objectToView,
                         const glm::mat4 &projection, float handFovScale) {
    if (!finite(lo) || !finite(hi) || glm::any(glm::greaterThan(lo,hi))) return fullScreen();
    std::array<glm::vec3,8> corners;
    for (unsigned i=0;i<8;++i) corners[i] = {(i&1)?hi.x:lo.x,(i&2)?hi.y:lo.y,(i&4)?hi.z:lo.z};
    return projectPoints(corners,objectToView,projection,handFovScale);
}
} // namespace hand_primary
