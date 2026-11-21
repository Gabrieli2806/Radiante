package com.g2806.radiante.client.render;

import net.minecraft.client.renderer.texture.OverlayTexture;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.CampfireBlock;
import net.minecraft.world.level.block.state.BlockState;

/**
 * The shimmer of hot air over lava, fire and lit campfires. Each has hot air standing over it, meshed with its section
 * as the walls and the lid of a box: over a lake of lava only the walls round its shore and one lid, since the faces
 * between two blocks of lava are left out. Only the camera's own view meets them, and goes on through each bent a
 * little, by an amount that wanders in quarter-block patches (pt_shade_default.glsl, ALPHA_MODE_HEAT); light, shadows
 * and reflections pass as if they were not there. A view across the lake goes in through one wall and out through
 * another, or the lid: two crossings however wide the lake.
 */
final class HeatHaze {

    /** Where the hot air starts and ends over the block's floor: over lava's surface, and round a flame. */
    static final float LAVA_FROM = 0.9f;
    static final float LAVA_TO = 3.0f;
    static final float FIRE_FROM = 0.1f;
    static final float FIRE_TO = 2.4f;
    private static final int FULL_BRIGHT = 0xF000F0;

    private HeatHaze() {
    }

    /** Fire of either kind and a campfire that is lit. */
    static boolean burns(BlockState state) {
        return state.is(Blocks.FIRE) || state.is(Blocks.SOUL_FIRE)
            || (state.getBlock() instanceof CampfireBlock && state.getValue(CampfireBlock.LIT));
    }

    /**
     * @param walls which of the four walls stand (north, south, west, east): those with no hot block beyond them
     */
    static void emit(PBRVertexWriter writer, float bx, float by, float bz, float from, float to, boolean[] walls) {
        // Its own alpha mode and none of the block's flags: it is air.
        writer.alphaMode(PBRVertexWriter.ALPHA_MODE_HEAT).water(false).lava(false).glass(false)
            .glassPane(false, false, false).blend(false).plant(false).leaves(false);
        float y0 = by + from;
        float y1 = by + to;
        quad(writer, bx, y1, bz, bx, y1, bz + 1.0f, bx + 1.0f, y1, bz + 1.0f, bx + 1.0f, y1, bz, 0.0f, 1.0f, 0.0f);
        if (walls[0]) {
            quad(writer, bx, y0, bz, bx + 1.0f, y0, bz, bx + 1.0f, y1, bz, bx, y1, bz, 0.0f, 0.0f, -1.0f);
        }
        if (walls[1]) {
            quad(writer, bx, y0, bz + 1.0f, bx + 1.0f, y0, bz + 1.0f, bx + 1.0f, y1, bz + 1.0f, bx, y1, bz + 1.0f,
                0.0f, 0.0f, 1.0f);
        }
        if (walls[2]) {
            quad(writer, bx, y0, bz, bx, y0, bz + 1.0f, bx, y1, bz + 1.0f, bx, y1, bz, -1.0f, 0.0f, 0.0f);
        }
        if (walls[3]) {
            quad(writer, bx + 1.0f, y0, bz, bx + 1.0f, y0, bz + 1.0f, bx + 1.0f, y1, bz + 1.0f, bx + 1.0f, y1, bz,
                1.0f, 0.0f, 0.0f);
        }
    }

    private static void quad(PBRVertexWriter writer, float x0, float y0, float z0, float x1, float y1, float z1,
        float x2, float y2, float z2, float x3, float y3, float z3, float nx, float ny, float nz) {
        vertex(writer, x0, y0, z0, nx, ny, nz);
        vertex(writer, x1, y1, z1, nx, ny, nz);
        vertex(writer, x2, y2, z2, nx, ny, nz);
        vertex(writer, x3, y3, z3, nx, ny, nz);
    }

    private static void vertex(PBRVertexWriter writer, float x, float y, float z, float nx, float ny, float nz) {
        writer.addVertex(x, y, z, -1, 0.0f, 0.0f, OverlayTexture.NO_OVERLAY, FULL_BRIGHT, nx, ny, nz);
    }
}
