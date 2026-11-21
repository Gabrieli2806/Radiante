package com.g2806.radiante.client.render;

import java.util.List;
import net.minecraft.client.renderer.chunk.RenderSectionRegion;
import net.minecraft.client.renderer.texture.OverlayTexture;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.client.resources.model.geometry.BakedQuad;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.level.LightLayer;
import net.minecraft.world.level.block.state.BlockState;
import org.joml.Vector3fc;

/**
 * Frost on windows: where it snows, glass and glass panes grow a ragged rim of frost in from their edges, whole
 * pixels of it (a sixteenth of a block), up to three deep. Each block of glass has its own rim; with Seamless Glass
 * the blocks join into one sheet and only the sheet's outer edges take it, the ones against a wall, a frame or the
 * open air, so a big window is frosted round its border and clear in the middle.
 *
 * <p>The frost comes and goes with the snow, as the snow on the ground does: while it snows it creeps in from the
 * edges a pixel at a time, and once the snow stops it goes back the way it came. Each pixel is laid down here with
 * the share of the frost at which it shows (its rank: the edge's pixels first, the deeper ones later), and the shaders
 * show it once the snow's wetness has grown that far (default.rahit, ALPHA_MODE_GLASS_FROST). Near a light (a torch,
 * a furnace, a campfire, a lantern) the frost stays thinner the brighter the light, and none forms in its full glow:
 * the block light in front of the glass, read at each pixel, sets how far the frost there can grow.
 *
 * <p>The frost is geometry, laid a hair over the glass when the section is meshed: the shaders know nothing of a
 * glass block's neighbours, which is what tells an edge of the sheet from a join inside it, nor of the light in front
 * of it, and the mesher has both at hand.
 */
final class GlassFrost {

    /** Faces narrower than this are edges and caps, not panes of glass to frost. */
    private static final float MIN_FACE = 4.0f / 16.0f;
    /** How far over the glass the frost lies, so it wins every ray against the face under it. */
    private static final float LIFT = 0.004f;
    private static final float PIXEL = 1.0f / 16.0f;
    private static final int FULL_BRIGHT = 0xF000F0;
    /** The share of an edge's pixels with at least one, two and three rows of frost, once it has all grown. */
    private static final float[] ROW_SHARE = {0.85f, 0.45f, 0.15f};
    /** Block light at which the frost starts to stay thinner, and at which none forms. */
    private static final float LIGHT_THINS = 5.0f;
    private static final float LIGHT_CLEARS = 13.0f;

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
                    Edge rim = new Edge(across, axes[along], axes[inward], lifted, min[along], max[along], edge,
                        side == 0 ? 1.0f : -1.0f, normal);
                    rim(writer, region, pos, rim, seed, bx, by, bz, u, v);
                }
            }
        }
        writer.albedoEmission(0.0f);
    }

    /** One edge of a face: in which plane it lies, along which axis it runs and into which the frost grows. */
    private record Edge(Direction.Axis across, Direction.Axis along, Direction.Axis inward, float plane, float from,
                        float to, float edge, float inwardSign, Direction normal) {
    }

    /** One edge's frost, a pixel at a time, each with the share of the frost it shows at. */
    private static void rim(PBRVertexWriter writer, RenderSectionRegion region, BlockPos pos, Edge rim, int seed,
        float bx, float by, float bz, float u, float v) {
        int pixels = Math.max(1, Math.round((rim.to() - rim.from()) / PIXEL));
        float[] p = new float[3];
        for (int i = 0; i < pixels; i++) {
            float h = hash(seed, i);
            int depth = h < ROW_SHARE[2] ? 3 : h < ROW_SHARE[1] ? 2 : h < ROW_SHARE[0] ? 1 : 0;
            float a0 = rim.from() + i * PIXEL;
            float a1 = Math.min(rim.to(), a0 + PIXEL);
            for (int row = 0; row < depth; row++) {
                float d0 = rim.edge() + rim.inwardSign() * row * PIXEL;
                float d1 = rim.edge() + rim.inwardSign() * (row + 1) * PIXEL;
                // How far the frost can grow at this pixel, by the light in front of it.
                p[rim.across().ordinal()] = rim.plane() + rim.normal().getAxisDirection().getStep() * 0.5f;
                p[rim.along().ordinal()] = (a0 + a1) * 0.5f;
                p[rim.inward().ordinal()] = (d0 + d1) * 0.5f;
                float light = blockLight(region, pos, p[0], p[1], p[2]);
                float reach = 1.0f - smoothstep(LIGHT_THINS, LIGHT_CLEARS, light);
                // The edge's pixels first, each row later than the one outside it, in no order along the edge.
                float rank = 0.05f + row * 0.28f + hash(seed ^ 0x5bd1e995, i * 4 + row) * 0.35f;
                if (reach < 0.02f || rank > reach) {
                    continue;
                }
                writer.albedoEmission(rank / reach);
                vertex(writer, rim, rim.plane(), a0, d0, bx, by, bz, u, v);
                vertex(writer, rim, rim.plane(), a1, d0, bx, by, bz, u, v);
                vertex(writer, rim, rim.plane(), a1, d1, bx, by, bz, u, v);
                vertex(writer, rim, rim.plane(), a0, d1, bx, by, bz, u, v);
            }
        }
    }

    /**
     * The block light at a point (block-relative) in front of the glass, blended between the four blocks round it in
     * the face's plane, so it fades across the glass towards a torch beside the window instead of in block steps.
     */
    private static float blockLight(RenderSectionRegion region, BlockPos pos, float px, float py, float pz) {
        float fx = px - 0.5f;
        float fy = py - 0.5f;
        float fz = pz - 0.5f;
        int x0 = (int) Math.floor(fx);
        int y0 = (int) Math.floor(fy);
        int z0 = (int) Math.floor(fz);
        float tx = fx - x0;
        float ty = fy - y0;
        float tz = fz - z0;
        float sum = 0.0f;
        BlockPos.MutableBlockPos at = new BlockPos.MutableBlockPos();
        for (int corner = 0; corner < 8; corner++) {
            int dx = corner & 1;
            int dy = corner >> 1 & 1;
            int dz = corner >> 2 & 1;
            float weight = (dx == 1 ? tx : 1.0f - tx) * (dy == 1 ? ty : 1.0f - ty) * (dz == 1 ? tz : 1.0f - tz);
            if (weight <= 0.0f) {
                continue;
            }
            at.set(pos.getX() + x0 + dx, pos.getY() + y0 + dy, pos.getZ() + z0 + dz);
            sum += weight * region.getBrightness(LightLayer.BLOCK, at);
        }
        return sum;
    }

    private static float smoothstep(float edge0, float edge1, float x) {
        float t = Math.clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    private static void vertex(PBRVertexWriter writer, Edge rim, float plane, float a, float d, float bx, float by,
        float bz, float u, float v) {
        float[] p = new float[3];
        p[rim.across().ordinal()] = plane;
        p[rim.along().ordinal()] = a;
        p[rim.inward().ordinal()] = d;
        Direction normal = rim.normal();
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
