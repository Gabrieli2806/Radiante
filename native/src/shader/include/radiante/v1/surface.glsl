#ifndef RADIANTE_V1_SURFACE_GLSL
#define RADIANTE_V1_SURFACE_GLSL

// What kind of surface a ray hit. Every vertex carries a word of flags saying what it belongs to: water, glass,
// leaves... (hookSurface gets it as surfaceFlags). The engine sets them when it builds the geometry (gpu/vertex.hpp),
// and a pack reads them only through these functions: a flag may move to another bit in a later Radiante, and its
// function moves with it. Part of radiante/v1, where functions are only ever added.

// The surface of water.
bool radianteIsWater(uint surfaceFlags) {
    return (surfaceFlags & (1u << 17u)) != 0u;
}

// The surface of lava.
bool radianteIsLava(uint surfaceFlags) {
    return (surfaceFlags & (1u << 28u)) != 0u;
}

// Glass blocks and panes, whose see-through texels are clear glass rather than holes.
bool radianteIsGlass(uint surfaceFlags) {
    return (surfaceFlags & (1u << 20u)) != 0u;
}

// Glass panes.
bool radianteIsPane(uint surfaceFlags) {
    return (surfaceFlags & (1u << 22u)) != 0u;
}

// Whether the top or the bottom end of a glass pane faces air.
bool radiantePaneTopOpen(uint surfaceFlags) {
    return (surfaceFlags & (1u << 23u)) != 0u;
}

bool radiantePaneBottomOpen(uint surfaceFlags) {
    return (surfaceFlags & (1u << 24u)) != 0u;
}

// Leaves of every tree.
bool radianteIsLeaves(uint surfaceFlags) {
    return (surfaceFlags & (1u << 30u)) != 0u;
}

// Small plants: grass, flowers, bushes.
bool radianteIsPlant(uint surfaceFlags) {
    return (surfaceFlags & (1u << 21u)) != 0u;
}

// Other thin things that let light through: vines, roots and hanging moss, lily pads, sugar cane, kelp, bamboo,
// cobwebs, and every particle.
bool radianteIsThin(uint surfaceFlags) {
    return (surfaceFlags & (1u << 27u)) != 0u;
}

// Ice, packed ice, blue ice and snow.
bool radianteIsFrost(uint surfaceFlags) {
    return (surfaceFlags & (1u << 6u)) != 0u;
}

// Ice, slime and honey blocks, which Bedrock RTX draws see-through.
bool radianteIsBlend(uint surfaceFlags) {
    return (surfaceFlags & (1u << 25u)) != 0u;
}

// A layer drawn just off a face of the same block, such as the side of a grass block.
bool radianteIsLayer(uint surfaceFlags) {
    return (surfaceFlags & (1u << 29u)) != 0u;
}

// Rain and snow sheets.
bool radianteIsRain(uint surfaceFlags) {
    return (surfaceFlags & (1u << 18u)) != 0u && (surfaceFlags & (1u << 27u)) == 0u;
}

// A drop of a splash of water: the particles rain throws up where it lands, a fish's, a boat's. Radiante PT sees
// through it, bent as through a small lens.
bool radianteIsSplash(uint surfaceFlags) {
    return (surfaceFlags & (1u << 18u)) != 0u && (surfaceFlags & (1u << 27u)) != 0u;
}

// What the player holds, seen in first person.
bool radianteIsHeld(uint surfaceFlags) {
    return (surfaceFlags & (1u << 19u)) != 0u;
}

// The held item the held light stands for (a torch in the hand): its light already reaches the scene through it.
bool radianteIsHeldLight(uint surfaceFlags) {
    return (surfaceFlags & (1u << 7u)) != 0u;
}

// A light block in a state that gives no light (unlit redstone ore), though it draws the textures that glow when lit.
bool radianteIsUnlit(uint surfaceFlags) {
    return (surfaceFlags & (1u << 26u)) != 0u;
}

// A glowing surface that is also one of the lights of its chunk section (powered redstone dust).
bool radianteIsGlowLight(uint surfaceFlags) {
    return (surfaceFlags & (1u << 31u)) != 0u;
}

#endif
