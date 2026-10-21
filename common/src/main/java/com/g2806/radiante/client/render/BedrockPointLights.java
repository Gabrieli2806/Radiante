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
 * <p>Only the experimental Bedrock profile has them (ChunkManager writes them when Bedrock's glow is cast, see
 * Pipeline.castsBedrockGlow); the others light with every glowing block's texture.
 */
final class BedrockPointLights {

    /**
     * A light's strength: 2 of 255 in Bedrock's light list, over 25.5. (The 0.3 its shader was taken to apply on top
     * is not there: side by side with Bedrock RTX, a torch lit the wall it hangs on about a third as brightly in
     * daylight, and a room lit by torches alone stayed at the eye's darkest limit, two to five times darker.)
     */
    static final float STRENGTH = 2.0f / 25.5f;
    private static final float RADIUS = 0.115f;
    private static final float LANTERN_RADIUS = 0.23f;

    private BedrockPointLights() {
    }

    /** Writes the light Bedrock gives this block, if any, at the block whose corner is (x, y, z). */
    static void emit(BlockState state, float x, float y, float z, PBRVertexWriter writer) {
        Block block = state.getBlock();
        if (block == Blocks.TORCH) {
            writer.pointLight(x + 0.5f, y + 0.561f, z + 0.5f, 255, 127, 76, STRENGTH, RADIUS);
        } else if (block == Blocks.WALL_TORCH) {
            wallTorch(state, x, y, z, 255, 127, 76, writer);
        } else if (block == Blocks.REDSTONE_TORCH) {
            if (state.getValue(RedstoneTorchBlock.LIT)) {
                writer.pointLight(x + 0.5f, y + 0.561f, z + 0.5f, 255, 51, 25, STRENGTH, RADIUS);
            }
        } else if (block == Blocks.REDSTONE_WALL_TORCH) {
            if (state.getValue(RedstoneTorchBlock.LIT)) {
                wallTorch(state, x, y, z, 255, 51, 25, writer);
            }
        } else if (block == Blocks.LANTERN || block == Blocks.SOUL_LANTERN) {
            float height = state.getValue(LanternBlock.HANGING) ? 0.35f : 0.198f;
            boolean soul = block == Blocks.SOUL_LANTERN;
            writer.pointLight(x + 0.5f, y + height, z + 0.5f, soul ? 0 : 255, soul ? 255 : 127, soul ? 255 : 76,
                STRENGTH, LANTERN_RADIUS);
        } else if (block == Blocks.END_ROD) {
            writer.pointLight(x + 0.5f, y + 0.5f, z + 0.5f, 255, 255, 255, STRENGTH, RADIUS);
        } else if (isBerryVine(state)) {
            // Not Bedrock's: the berries of a glow berry vine are level 14 in vanilla and a pack lights only their few
            // texels, so a lush cave of them was dark. Each berried block gives a warm, soft light of its own.
            writer.pointLight(x + 0.5f, y + 0.4f, z + 0.5f, 255, 170, 70, STRENGTH * BERRY_STRENGTH, LANTERN_RADIUS);
        }
    }

    /** How much of a torch's strength a glow berry vine gives: gentle, there are many of them. */
    private static final float BERRY_STRENGTH = 1.2f;

    private static boolean isBerryVine(BlockState state) {
        Block block = state.getBlock();
        return (block == Blocks.CAVE_VINES || block == Blocks.CAVE_VINES_PLANT) && state.getValue(CaveVines.BERRIES);
    }

    /** Whether the block has one of these lights, in some state. */
    static boolean hasLight(Block block) {
        return block == Blocks.CAVE_VINES || block == Blocks.CAVE_VINES_PLANT || block == Blocks.TORCH || block == Blocks.WALL_TORCH || block == Blocks.REDSTONE_TORCH
            || block == Blocks.REDSTONE_WALL_TORCH || block == Blocks.LANTERN || block == Blocks.SOUL_LANTERN
            || block == Blocks.END_ROD;
    }

    /** A torch on a wall: its flame higher, and a little towards the wall it hangs on. */
    private static void wallTorch(BlockState state, float x, float y, float z, int r, int g, int b,
        PBRVertexWriter writer) {
        Direction facing = state.getValue(WallTorchBlock.FACING);
        writer.pointLight(x + 0.5f - facing.getStepX() * 0.273f, y + 0.774f, z + 0.5f - facing.getStepZ() * 0.273f,
            r, g, b, STRENGTH, RADIUS);
    }
}
