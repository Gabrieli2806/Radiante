#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// Closest hit of the default group, the four text groups and every entity layer: the default surface
// shading (pt/pt_shade_default.glsl), with height-map parallax where the enable_parallax attribute turns it on.
// It only records the hit (record_hit.glsl); the ray generation shader shades it by this kind.
#define PT_HIT_KIND PT_KIND_DEFAULT
#include "record_hit.glsl"
