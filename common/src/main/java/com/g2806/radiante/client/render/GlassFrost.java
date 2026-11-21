package com.g2806.radiante.client.render;

import java.util.List;
import net.minecraft.client.renderer.chunk.RenderSectionRegion;
import net.minecraft.client.renderer.texture.OverlayTexture;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.client.resources.model.geometry.BakedQuad;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.level.block.state.BlockState;
import org.joml.Vector3fc;

/**
 * Frost on windows: where it snows, glass and glass panes grow a ragged rim of frost in from their edges, whole
 * pixels of it (a sixteenth of a block), up to three deep. Each block of glass has its own rim; with Seamless Glass
 * the blocks join into one sheet and only the sheet's outer edges take it, the ones against a wall, a frame or the
 * open air, so a big window is frosted round its border and clear in the middle.
 *
 * <p>The frost is geometry, laid a hair over the glass when the section is meshed: the shaders know nothing of a
 * glass block's neighbours, which is what tells an edge of the sheet from a join inside it, and the mesher has them
 * at hand. The pixels are white, opaque squares coloured by the snow texture, so the tracer lights and shadows them
 * like anything else.
 */
final class GlassFrost {

    /** Faces narrower than this are edges and caps, not panes of glass to frost. */
    private static final float MIN_FACE = 4.0f / 16.0f;
    /** How far over the glass the frost lies, so it wins every ray against the face under it. */
    private static final float LIFT = 0.004f;
    private static final float PIXEL = 1.0f / 16.0f;
    private static final int FULL_BRIGHT = 0xF000F0;
    /** The share of an edge's pixels with at least one, two and three rows of frost. */
    private static final float[] ROW_SHARE = {0.85f, 0.45f, 0.15f};

    private GlassFrost() {
    }

    /**
     * @param quads the glass block's own faces, as its model drew them
     * @param joined whether Seamless Glass joins this kind of glass to its like next to it
     * @param pane whether the block is a pane
     * @param snow the snow block's sprite, whose middle texel colours the frost
     */
    static void emit(List<BakedQuad> quads, RenderSectionRegion region, BlockPos pos, float bx, float by, float bz,
        boolean joined, boolean pane, TextureAtlasSprite snow, PBRVertexWriter writer) {
        float u = (snow.getU0() + snow.getU1()) * 0.5f;
        float v = (snow.getV0() + snow.getV1()) * 0.5f;
        for (BakedQuad face : quads) {
            Direction normal = face.direction();
            Direction.Axis across = normal.getAxis();
            Direction.Axis first = across == Direction.Axis.X ? Direction.Axis.Z : Direction.Axis.X;
            Direction.Axis second = across == Direction.Axis.Y ? Direction.Axis.Z : Direction.Axis.Y;
            float[] min = {Float.MAX_VALUE, Float.MAX_VALUE};
            float[] max = {-Float.MAX_VALUE, -Float.MAX_VALUE};
            Vector3fc[] corners = {face.position0(), face.position1(), face.position2(), face.position3()};
            float plane = axisValue(corners[0], across);
            boolean flat = true;
            for (Vector3fc corner : corners) {
                flat &= Math.abs(axisValue(corner, across) - plane) < 1.0e-4f;
                min[0] = Math.min(min[0], axisValue(corner, first));
                max[0] = Math.max(max[0], axisValue(corner, first));
                min[1] = Math.min(min[1], axisValue(corner, second));
                max[1] = Math.max(max[1], axisValue(corner, second));
            }
            if (!flat || max[0] - min[0] < MIN_FACE || max[1] - min[1] < MIN_FACE) {
                continue;
            }
            float lifted = plane + LIFT * normal.getAxisDirection().getStep();
            Direction.Axis[] axes = {first, second};
            for (int along = 0; along < 2; along++) {
                int inward = 1 - along;
                for (int side = 0; side < 2; side++) {
                    // The edge at the low (side 0) or high (side 1) end of the inward axis, running along the other.
                    float edge = side == 0 ? min[inward] : max[inward];
                    boolean onBoundary = side == 0 ? edge < 1.0e-3f : edge > 1.0f - 1.0e-3f;
                    if (!onBoundary) {
                        // Inside the block (the side of a pane's post): not an edge of the glass.
                        continue;
                    }
                    Direction out = Direction.fromAxisAndDirection(axes[inward],
                        side == 0 ? Direction.AxisDirection.NEGATIVE : Direction.AxisDirection.POSITIVE);
                    if (joined && sameGlass(region.getBlockState(pos.relative(out)), pane)) {
                        continue;
                    }
                    int seed = (int) (pos.asLong() * 31L) ^ normal.ordinal() * 7919 ^ (along * 2 + side) * 104729;
                    rim(writer, across, axes[along], axes[inward], lifted, min[along], max[along], edge,
                        side == 0 ? 1.0f : -1.0f, seed, normal, bx, by, bz, u, v);
                }
            }
        }
    }

    /** One edge's frost: for each row, a quad for every run of pixels that reach that deep. */
    private static void rim(PBRVertexWriter writer, Direction.Axis across, Direction.Axis along, Direction.Axis inward,
        float plane, float from, float to, float edge, float inwardSign, int seed, Direction normal, float bx,
        float by, float bz, float u, float v) {
        int pixels = Math.max(1, Math.round((to - from) / PIXEL));
        int[] depth = new int[pixels];
        for (int i = 0; i < pixels; i++) {
            float h = hash(seed, i);
            depth[i] = h < ROW_SHARE[2] ? 3 : h < ROW_SHARE[1] ? 2 : h < ROW_SHARE[0] ? 1 : 0;
        }
        for (int row = 0; row < 3; row++) {
            int start = -1;
            for (int i = 0; i <= pixels; i++) {
                boolean frosted = i < pixels && depth[i] > row;
                if (frosted && start < 0) {
                    start = i;
                } else if (!frosted && start >= 0) {
                    float a0 = from + start * PIXEL;
                    float a1 = Math.min(to, from + i * PIXEL);
                    float d0 = edge + inwardSign * row * PIXEL;
                    float d1 = edge + inwardSign * (row + 1) * PIXEL;
                    vertex(writer, across, along, inward, plane, a0, d0, normal, bx, by, bz, u, v);
                    vertex(writer, across, along, inward, plane, a1, d0, normal, bx, by, bz, u, v);
                    vertex(writer, across, along, inward, plane, a1, d1, normal, bx, by, bz, u, v);
                    vertex(writer, across, along, inward, plane, a0, d1, normal, bx, by, bz, u, v);
                    start = -1;
                }
            }
        }
    }

    private static void vertex(PBRVertexWriter writer, Direction.Axis across, Direction.Axis along,
        Direction.Axis inward, float plane, float a, float d, Direction normal, float bx, float by, float bz, float u,
        float v) {
        float[] p = new float[3];
        p[across.ordinal()] = plane;
        p[along.ordinal()] = a;
        p[inward.ordinal()] = d;
        writer.addVertex(p[0] + bx, p[1] + by, p[2] + bz, -1, u, v, OverlayTexture.NO_OVERLAY, FULL_BRIGHT,
            normal.getStepX(), normal.getStepY(), normal.getStepZ());
    }

    /** Glass of the same sort next door: a block for a block, a pane for a pane, whatever the colour. */
    private static boolean sameGlass(BlockState neighbour, boolean pane) {
        return BlockKinds.isGlass(neighbour) && BlockKinds.isGlassPane(neighbour) == pane;
    }

    /** A number from 0 to 1 for a pixel of an edge, the same every time the section is meshed. */
    private static float hash(int seed, int index) {
        int h = seed ^ index * 0x9E3779B9;
        h ^= h >>> 16;
        h *= 0x85EBCA6B;
        h ^= h >>> 13;
        h *= 0xC2B2AE35;
        h ^= h >>> 16;
        return (h & 0xFFFFFF) / (float) 0x1000000;
    }

    private static float axisValue(Vector3fc p, Direction.Axis axis) {
        return switch (axis) {
            case X -> p.x();
            case Y -> p.y();
            case Z -> p.z();
        };
    }
}
