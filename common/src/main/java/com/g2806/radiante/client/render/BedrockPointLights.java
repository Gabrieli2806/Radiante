package com.g2806.radiante.client.render;

import net.minecraft.core.Direction;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.CaveVines;
import net.minecraft.world.level.block.LanternBlock;
import net.minecraft.world.level.block.RedstoneTorchBlock;
import net.minecraft.world.level.block.WallTorchBlock;
import net.minecraft.world.level.block.state.BlockState;

/**
 * The lights Bedrock RTX adds of its own for a few small light blocks, on top of the glow of their textures: torches,
 * redstone torches, lanterns, soul lanterns and end rods, each a small sphere of light at its flame. Every other light
 * block lights the world through its glow alone. All of it is as Bedrock's light list holds it while the game runs
 * (calibrated to Bedrock RTX): where the light sits in the block, its colour, a strength of 2 / 25.5 for all of
 * them, and a radius of 0.23 for lanterns, 0.115 for the rest. The soul torch has none there.
 *
 * <p>Only the Bedrock RTX profiles have them (ChunkManager writes them when Bedrock's glow is cast, see
 * Pipeline.castsBedrockGlow); without that glow every glowing block's texture lights the world.
 */
final class BedrockPointLights {

    /**
     * A light's strength: 2 of 255 in Bedrock's light list, over 25.5. (The 0.3 its shader was taken to apply on top
     * is not there: side by side with Bedrock RTX, a torch lit the wall it hangs on about a third as brightly in
     * daylight, and a room lit by torches alone stayed at the eye's darkest limit, two to five times darker.)
     */
    static final float STRENGTH = 2.0f / 25.5f * 11.0f;

    /**
     * The strength each light is written with, and the held one's (HeldLight): STRENGTH and the share the glowing
     * texels of the flame were measured to add. Those no longer light the world where a point light does (they made
     * the light around a torch set down grainy, see ChunkBuildData::buildLights), so placed and held are one light.
     */
    static final float CAST_STRENGTH = STRENGTH * 1.08f;

    /*
     * Not Bedrock's: a torch set down lit a dark room about a quarter as much as the same torch held in the hand
     * (HeldLight), so walking in with one and putting it down made the room go dark. The 7 is the middle between the
     * two as they were (the held torch then lit a stone floor a third brighter than the 6 this had), and a held
     * point light block now casts this very light (HeldLight.pointLightOf).
     */
    private static final float RADIUS = 0.115f;
    private static final float LANTERN_RADIUS = 0.23f;

    private BedrockPointLights() {
    }

    /** Writes the light Bedrock gives this block, if any, at the block whose corner is (x, y, z). */
    static void emit(BlockState state, float x, float y, float z, PBRVertexWriter writer) {
        Block block = state.getBlock();
        int[] rgb = colourOf(block);
        if (rgb != null) {
            boolean lit = !state.hasProperty(RedstoneTorchBlock.LIT) || state.getValue(RedstoneTorchBlock.LIT);
            if (!lit) {
                return;
            }
            if (state.hasProperty(WallTorchBlock.FACING) && !(block instanceof LanternBlock)) {
                wallTorch(state, x, y, z, rgb[0], rgb[1], rgb[2], writer);
            } else if (block instanceof LanternBlock) {
                float height = state.getValue(LanternBlock.HANGING) ? 0.35f : 0.198f;
                writer.pointLight(x + 0.5f, y + height, z + 0.5f, rgb[0], rgb[1], rgb[2], CAST_STRENGTH, LANTERN_RADIUS);
            } else if (block == Blocks.BEACON) {
                writer.pointLight(x + 0.5f, y + 0.5f, z + 0.5f, rgb[0], rgb[1], rgb[2], CAST_STRENGTH, LANTERN_RADIUS);
            } else if (block == Blocks.END_ROD) {
                writer.pointLight(x + 0.5f, y + 0.5f, z + 0.5f, rgb[0], rgb[1], rgb[2], CAST_STRENGTH, RADIUS);
            } else {
                writer.pointLight(x + 0.5f, y + 0.561f, z + 0.5f, rgb[0], rgb[1], rgb[2], CAST_STRENGTH, RADIUS);
            }
        } else if (isFlame(block)) {
            // Not Bedrock's: fire, soul fire and campfires lit what is around them only through the glow of their
            // sprites, which the Nether's glow scale dims, so a flame next to lava looked dull and lit nothing. A warm
            // (cyan for the soul ones) light of its own, at the heart of the flame.
            if (state.hasProperty(net.minecraft.world.level.block.CampfireBlock.LIT)
                && !state.getValue(net.minecraft.world.level.block.CampfireBlock.LIT)) {
                return;
            }
            boolean soul = block == Blocks.SOUL_FIRE || block == Blocks.SOUL_CAMPFIRE;
            boolean campfire = block == Blocks.CAMPFIRE || block == Blocks.SOUL_CAMPFIRE;
            writer.pointLight(x + 0.5f, y + (campfire ? 0.45f : 0.35f), z + 0.5f, soul ? 0 : 255, soul ? 230 : 120,
                soul ? 255 : 40, CAST_STRENGTH * FLAME_STRENGTH * (campfire ? 1.2f : 1.0f), FLAME_RADIUS);
        } else if (isBerryVine(state)) {
            // Not Bedrock's: the berries of a glow berry vine are level 14 in vanilla and a pack lights only their few
            // texels, so a lush cave of them was dark. Each berried block gives a warm, soft light of its own.
            writer.pointLight(x + 0.5f, y + 0.4f, z + 0.5f, 255, 170, 70, CAST_STRENGTH * BERRY_STRENGTH, LANTERN_RADIUS);
        } else if (block == Blocks.FIREFLY_BUSH) {
            // Not Bedrock's: the bush glows only in a few specks and lit nothing around it. A soft, greenish-yellow
            // light of its own, a quarter of what a berried vine gives.
            writer.pointLight(x + 0.5f, y + 0.4f, z + 0.5f, 215, 255, 120, CAST_STRENGTH * BERRY_STRENGTH * 0.25f,
                LANTERN_RADIUS);
        }
    }

    /**
     * The colour (0..255) of the light this block has, or null. Shared with HeldLight, which gives the same item in
     * the hand this very light, so a torch lights a cave alike held or set down.
     */
    static int[] colourOf(Block block) {
        // Asked for every block of every chunk built: worked out once per block.
        return COLOURS.computeIfAbsent(block, BedrockPointLights::colourFor).orElse(null);
    }

    private static final java.util.Map<Block, java.util.Optional<int[]>> COLOURS =
        java.util.Collections.synchronizedMap(new java.util.IdentityHashMap<>());

    private static java.util.Optional<int[]> colourFor(Block block) {
        return java.util.Optional.ofNullable(colourOf0(block));
    }

    private static int[] colourOf0(Block block) {
        // The beacon too: its core's glowing texels as light list candidates were tiny, bright sources that the ground
        // far around picked one pixel in many, which left it grainy. One point light at the core lights it cleanly.
        if (block == Blocks.BEACON) {
            return new int[] {200, 255, 255};
        }
        if (block == Blocks.END_ROD) {
            return new int[] {255, 255, 255};
        }
        // Every torch and lantern, by its kind rather than a list of blocks: the copper torch and the copper lanterns
        // (all their weathered and waxed forms) were left out and lit only by their flame, which the light list no
        // longer takes for blocks with a point light, so some torches cast nothing. Not Bedrock's for the soul and
        // copper ones, which have no light of their own there.
        if (!(block instanceof net.minecraft.world.level.block.BaseTorchBlock) && !(block instanceof LanternBlock)) {
            return null;
        }
        String name = net.minecraft.core.registries.BuiltInRegistries.BLOCK.getKey(block).getPath();
        if (name.contains("redstone")) {
            return new int[] {255, 51, 25};
        }
        if (name.contains("soul")) {
            return new int[] {0, 255, 255};
        }
        if (name.contains("copper")) {
            return new int[] {120, 255, 140};
        }
        return new int[] {255, 127, 76};
    }
    /** How much of a torch's strength a glow berry vine gives: gentle, there are many of them. */
    private static final float BERRY_STRENGTH = 0.6f;

    /** A flame's share of a torch's strength, and its radius: fire is a bigger, lower flame than a torch's. */
    private static final float FLAME_STRENGTH = 0.9f;
    private static final float FLAME_RADIUS = 0.2f;

    private static boolean isFlame(Block block) {
        return block == Blocks.FIRE || block == Blocks.SOUL_FIRE || block == Blocks.CAMPFIRE
            || block == Blocks.SOUL_CAMPFIRE;
    }

    private static boolean isBerryVine(BlockState state) {
        Block block = state.getBlock();
        return (block == Blocks.CAVE_VINES || block == Blocks.CAVE_VINES_PLANT) && state.getValue(CaveVines.BERRIES);
    }

    /** Whether the block has one of these lights, in some state. */
    static boolean hasLight(Block block) {
        return block == Blocks.CAVE_VINES || block == Blocks.CAVE_VINES_PLANT || block == Blocks.FIREFLY_BUSH
            || isFlame(block) || colourOf(block) != null;
    }

    /** A torch on a wall: its flame higher, and a little towards the wall it hangs on. */
    private static void wallTorch(BlockState state, float x, float y, float z, int r, int g, int b,
        PBRVertexWriter writer) {
        Direction facing = state.getValue(WallTorchBlock.FACING);
        writer.pointLight(x + 0.5f - facing.getStepX() * 0.273f, y + 0.774f, z + 0.5f - facing.getStepZ() * 0.273f,
            r, g, b, CAST_STRENGTH, RADIUS);
    }
}
