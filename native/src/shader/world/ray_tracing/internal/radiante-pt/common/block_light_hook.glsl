#ifndef VPT_BLOCK_LIGHT_HOOK_GLSL
#define VPT_BLOCK_LIGHT_HOOK_GLSL

// What hooks/block_light.glsl's kind says, and the hook itself. Every shader that calls hookBlockLight includes this
// file rather than the hook, so the constants stay radiante-pt's when a pack replaces the hook. Part of the pack
// interface (radiante-pt@1): values are only added.
const uint RADIANTE_BLOCK_LIGHT_LEVEL = 0u; // a block's light (VptPackedLight.radiance.w 0)
const uint RADIANTE_BLOCK_LIGHT_EXACT = 1u; // a Bedrock pack's own glow (radiance.w 1)
const uint RADIANTE_BLOCK_LIGHT_POINT = 2u; // a Bedrock RTX point light (radiance.w 2)
const uint RADIANTE_BLOCK_LIGHT_HELD = 3u;  // the light of what the player holds
const uint RADIANTE_BLOCK_LIGHT_GLOW = 4u;  // a glowing surface as a ray sees it

#include "hooks/block_light.glsl"

#endif
