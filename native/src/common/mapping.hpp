// Type names for the structs in shared.hpp, which both C++ and GLSL compile. Each T_ name stands for the same
// GLSL type on both sides, so a struct is written once and the two languages cannot drift apart. glm's types have
// the same size and alignment as the matching C scalars, which keeps the C++ layout predictable; where C++ and std140
// would still disagree (a vec3 followed by a vec4, an array of scalars), shared.hpp pads by hand.
#ifndef PRISM_COMMON_MAPPING_HPP
#define PRISM_COMMON_MAPPING_HPP

#ifdef __cplusplus

#    include <cstddef>
#    include <cstdint>
#    include <glm/glm.hpp>

#    define T_FLOAT float
#    define T_INT int32_t
#    define T_UINT uint32_t
#    define T_BOOL bool
#    define T_VEC2 glm::vec2
#    define T_VEC3 glm::vec3
#    define T_VEC4 glm::vec4
#    define T_IVEC2 glm::ivec2
#    define T_IVEC4 glm::ivec4
#    define T_DVEC4 glm::dvec4
#    define T_MAT4 glm::mat4

#else

// WorldUBO carries the camera position as a dvec4 so far-from-origin worlds keep their precision.
#    extension GL_EXT_shader_explicit_arithmetic_types_float64 : require

#    define T_FLOAT float
#    define T_INT int
#    define T_UINT uint
#    define T_BOOL bool
#    define T_VEC2 vec2
#    define T_VEC3 vec3
#    define T_VEC4 vec4
#    define T_IVEC2 ivec2
#    define T_IVEC4 ivec4
#    define T_DVEC4 dvec4
#    define T_MAT4 mat4

// Instance mask bits of the top-level acceleration structure: rays pick which kinds of geometry they can hit. They
// have to match the masks the C++ side gives each instance when it builds the acceleration structure.
#    define WORLD_MASK 1
#    define PLAYER_MASK 2
// Nothing ever used the fishing bobber's bit, so it went to the glowing effect (GLOW_OUTLINE_MASK). Left at 0, a mask
// that still names the bobber selects nothing extra.
#    define FISHING_BOBBER_MASK 0
// Copies of entities with the glowing effect, which only the outline rays look for.
#    define GLOW_OUTLINE_MASK 4
#    define HAND_MASK 8
#    define WEATHER_MASK 16
#    define PARTICLE_MASK 32
#    define CLOUD_MASK 64
#    define BOAT_WATER_MASK 128

// What a ShadowRay is asking, stored in its pad0 so the any-hit and miss shaders can tell the kinds apart. 0 is a
// plain shadow ray towards the sun or moon.
// Looks for the glow outline copies instead of for shadows.
#    define GLOW_OUTLINE_QUERY 1u
// Visibility towards a sampled block light: a miss means "unblocked", not sunlight.
#    define BLOCK_LIGHT_QUERY 2u
// Bedrock original profile: direct sun or moon visibility for an opaque surface that is already under water.
#    define UNDERWATER_CELESTIAL_QUERY 3u
// Whether the open sky is in sight that way: a miss means "unblocked", like a block light query, but what the
// see-through surfaces on the way take from the light is not asked for.
#    define SKY_VISIBILITY_QUERY 4u

#endif

#endif
