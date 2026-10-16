#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// Closest hit of the patch that hides the water inside a boat (ptShadeWaterMask, pt/pt_shade_special.glsl).
// It only records the hit (record_hit.glsl); the ray generation shader shades it by this kind.
#define PT_HIT_KIND PT_KIND_WATER_MASK
#include "record_hit.glsl"
