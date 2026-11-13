package com.g2806.radiante.client.render;

import com.g2806.radiante.client.option.Options;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.client.Minecraft;

/**
 * How much of an atlas's detail the ray tracer keeps (the Texture Detail setting). The tracer has copies of its own of
 * an atlas: the colours, and the specular and normal maps stitched to match (PbrAtlases), each the size of Minecraft's
 * atlas. With a pack of 128 pixels a block or more those run to a gigabyte and over, and once the game's video memory
 * runs out the driver moves it to and fro over the bus: every pass of the frame slows down, not only the ones reading
 * textures (a 128 px pack took the frame from 4.6 to 31 ms of GPU time on a 12 GB card with other programs open).
 *
 * <p>Leaving out an atlas's finest mip levels keeps every copy's layout (the tracer reads them by normalised
 * coordinates) at a half or a quarter of the size a side. Minecraft's own atlas, which the menus and the hand draw
 * from, keeps its detail. Automatic keeps up to 128 pixels a block, the finest the opacity micromaps of leaves and
 * grass describe (past it every ray through them asks the any-hit shader), unless the copies of all the atlases would
 * take more than a fifth of the video memory the driver gives the game (VK_EXT_memory_budget: what other programs use
 * is taken off); then it halves the detail, the block atlas's first, until they fit, never under 16 pixels a block.
 * With a 128 px pack halved, the frame cost what it does with Minecraft's own textures (4.2 against 4.4 ms).
 */
public final class TracedTextureDetail {

    /** Options.tracedTextureDetail: automatic, full, half and quarter. */
    public static final int AUTO = 0;
    public static final int FULL = 1;
    public static final int HALF = 2;
    public static final int QUARTER = 3;

    /** The copies an atlas has in the tracer: the colours, the specular and the normal maps. */
    private static final int COPIES = 3;
    /** The share of the video memory budget the copies may take before Automatic lowers their detail. */
    private static final double BUDGET_SHARE = 0.2;
    /** The coarsest detail Automatic and the manual settings go down to, in texels a block. */
    private static final int MIN_TEXELS = 16;
    /** The finest detail Automatic keeps, in texels a block: opacity_micromaps.cpp's deepest level, 2^7. */
    private static final int AUTO_MAX_TEXELS = 128;

    /** By the texture id of an atlas's colours: the mip levels its copies leave out. */
    private static final Map<Integer, Integer> DROPPED = new ConcurrentHashMap<>();
    private static int appliedSetting = -1;
    /** The copies of the atlases mirrored since the block atlas last was, in MB: one reload's. */
    private static long reloadMegabytes;

    private TracedTextureDetail() {
    }

    /**
     * Picks the levels to leave out for an atlas about to be mirrored, remembers them for its specular and normal
     * maps, and logs the choice.
     *
     * @param typicalSprite the most common sprite width of the atlas, its pack's pixels a block
     */
    public static int choose(int textureId, String atlas, int width, int height, int mipLevels, int typicalSprite) {
        appliedSetting = Options.tracedTextureDetail;
        // The block atlas comes first in a reload; the others share what it leaves of the budget.
        if (atlas.endsWith("atlas/blocks.png")) {
            reloadMegabytes = 0;
        }
        int floor = 0;
        while (floor < mipLevels - 1 && typicalSprite >> (floor + 1) >= MIN_TEXELS) {
            floor++;
        }
        long budgetMb = 0;
        int dropped;
        switch (Options.tracedTextureDetail) {
            case FULL -> dropped = 0;
            case HALF -> dropped = Math.min(1, floor);
            case QUARTER -> dropped = Math.min(2, floor);
            default -> {
                long[] stats = com.g2806.radiante.client.pipeline.Pipeline.sceneStats();
                budgetMb = stats != null && stats.length > 3 ? stats[3] : 0;
                dropped = 0;
                while (dropped < floor && typicalSprite >> dropped > AUTO_MAX_TEXELS) {
                    dropped++;
                }
                while (budgetMb > 0 && dropped < floor
                    && reloadMegabytes + megabytes(width, height, dropped) > budgetMb * BUDGET_SHARE) {
                    dropped++;
                }
            }
        }
        DROPPED.put(textureId, dropped);
        reloadMegabytes += megabytes(width, height, dropped);
        if (dropped > 0 || megabytes(width, height, 0) >= 256) {
            RadianteRenderer.LOGGER.info("Texture detail for {} ({} px a block): {}x{} traced at {}x{}, about {} MB in "
                    + "{} copies{}", atlas, typicalSprite, width, height, Math.max(1, width >> dropped),
                Math.max(1, height >> dropped), megabytes(width, height, dropped), COPIES,
                budgetMb > 0 ? String.format(Locale.ROOT, " (automatic, %d MB video memory budget)", budgetMb) : "");
        }
        return dropped;
    }

    /** The levels the copies of the atlas whose colours have this texture id leave out; 0 for any other texture. */
    public static int droppedFor(int albedoTextureId) {
        return DROPPED.getOrDefault(albedoTextureId, 0);
    }

    /** All the atlas's copies with their mip levels, in MB. */
    private static long megabytes(int width, int height, int dropped) {
        long texels = (long) Math.max(1, width >> dropped) * Math.max(1, height >> dropped);
        return COPIES * texels * 4L * 4L / 3L >> 20;
    }

    /** Reloads the resource packs once the setting changed, so the atlases are mirrored again at the new detail. */
    public static void reloadIfChanged(Minecraft minecraft) {
        if (appliedSetting >= 0 && appliedSetting != Options.tracedTextureDetail) {
            appliedSetting = Options.tracedTextureDetail;
            minecraft.reloadResourcePacks();
        }
    }
}
