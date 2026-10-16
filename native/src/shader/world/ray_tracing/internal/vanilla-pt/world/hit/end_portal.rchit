#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// Closest hit of the End portal's surface (ptShadeEndPortal, pt/pt_shade_special.glsl).
// It only records the hit (record_hit.glsl); the ray generation shader shades it by this kind.
#define PT_HIT_KIND PT_KIND_END_PORTAL
#include "record_hit.glsl"
