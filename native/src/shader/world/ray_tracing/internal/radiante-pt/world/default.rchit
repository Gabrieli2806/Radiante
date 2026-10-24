#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// Closest hit of the default group, the four text groups and every entity layer: the shared default shading with
// height-map parallax allowed (it still follows the enable_parallax attribute).
#define PT_PARALLAX_ALLOWED 1
// The shading fills its hit context from this stage's built-ins (see pt/pt_hit_context.glsl).
#define PT_IN_HIT_STAGE
#include "pt/pt_shade_default.glsl"
