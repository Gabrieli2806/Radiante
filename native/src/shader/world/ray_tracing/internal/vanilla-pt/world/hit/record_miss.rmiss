#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

// Misses 1, 2 and 3 (world, hand, world_no_volumetric; the names in configs.json are kept): the main-path ray hit
// nothing. Only that is written down, as a distance of INF_DISTANCE; the ray generation shader then evaluates the
// miss itself (ptShadeRecorded, pt/pt_trace.glsl) by the miss index it traced with: the sky for the world misses
// (pt/pt_miss.glsl, with the volumetric clouds for miss 1 only), and for the hand's miss just the end of the path.
// So the three misses share this one shader.
//
// No hit can come back at INF_DISTANCE (65504): every main-path trace ends far short of it, at the world's trace
// distance (2304 blocks or the render distance, if farther) at most.

#include "common/shared.hpp"
#include "pt/pt_payload.glsl"

layout(location = 0) rayPayloadInEXT PrismHit prismHit;

void main() {
    prismHit.hitT = INF_DISTANCE;
}
