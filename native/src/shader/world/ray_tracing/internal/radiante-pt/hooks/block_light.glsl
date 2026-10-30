#ifndef RADIANTE_HOOK_BLOCK_LIGHT_GLSL
#define RADIANTE_HOOK_BLOCK_LIGHT_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// The colour and strength of the light of blocks: torches, lanterns, glowstone, lava, soul fire... Called with the
// light a block casts on the surfaces around it and on the fog, with the light of what the player holds, and with
// the glow of a glowing surface where a ray sees it, so a pack can make torches warmer or soul fire bluer and have
// the flame, the room it lights and the haze round it agree.
//   radiance the light's colour times its strength, linear (scale it to dim or brighten, tint it to recolour)
//   kind     what it is, one of (common/block_light_hook.glsl):
//              RADIANTE_BLOCK_LIGHT_LEVEL  a block's light, from the glowing texels of its texture
//              RADIANTE_BLOCK_LIGHT_EXACT  the glow a Bedrock pack's own material gives (Bedrock RTX profile)
//              RADIANTE_BLOCK_LIGHT_POINT  one of Bedrock RTX's point lights at a small light block (same profile)
//              RADIANTE_BLOCK_LIGHT_HELD   what the player holds in either hand
//              RADIANTE_BLOCK_LIGHT_GLOW   a glowing surface as a ray sees it (the flame, the lamp's face)
// There is no block id: the lights are cut out of glowing textures. The colour tells them apart (a torch's light is
// orange, soul fire's cyan, a redstone torch's red). skyUBO and worldUBO are available.
void hookBlockLight(inout vec3 radiance, uint kind) {}

// Only in this default file: radiante-pt then skips the calls, so its shaders are the same as before the hook
// existed. A pack's file must not define it.
#define RADIANTE_HOOK_BLOCK_LIGHT_DEFAULT 1

#endif
