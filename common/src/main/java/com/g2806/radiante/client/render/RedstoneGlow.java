package com.g2806.radiante.client.render;

import java.nio.ByteBuffer;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.client.renderer.texture.SpriteContents;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.RedstoneWireBlock;
import net.minecraft.world.level.block.state.BlockState;

/**
 * The glow of powered redstone dust, by its power level, as Bedrock RTX shows it. Dust carries no light level in
 * Minecraft and draws the same greyscale sprites at every power, tinted by vanilla's power colour, so the emission
 * worked out from textures could only make it glow at every power or at none. Its tinted quads instead carry their
 * glow per vertex (PBRVertexWriter.glowLight): seen as their power colour times {@link #seenGlow}, and cast into the
 * section's light list (native emission.cpp) as their power colour times {@link #castLight}. Dust at power 0 neither
 * glows nor casts, and every dust quad is marked unlit, so a pack's own emission map for the dust sprites (a Bedrock
 * pack's MER) does not make it glow whatever its power: the power decides.
 */
public final class RedstoneGlow {

    /**
     * The glow seen at power 15, on the scale of {@link Glow}: a little dimmer than a level 15 block, so the dust reads
     * as glowing red without the white bloom of lava.
     */
    static final float SEEN_GLOW = Glow.ofLevel(9);
    /**
     * Share of the full seen glow that any power above 0 keeps: with the glow linear in power, dust at power 1 to 3
     * was too dim to tell from unpowered dust, which vanilla's darker colour for weak dust makes dimmer still.
     */
    private static final float SEEN_FLOOR = 0.3f;
    /**
     * The light cast per unit of covered area at power 15 (radiance on the light-level scale of EmissionTiles, kind 0).
     * A straight line's sprite covers about a twelfth of its quad, so it casts about 4, a little more than one face
     * of a level 15 block's texel average: under half of a redstone torch (level 7 on five faces, about 11), enough
     * to light the floor around the wire red at night without a long line outshining the torch that powers it. At a
     * third of this the light on the floor could not be told from none.
     */
    static final float CAST_PER_COVERAGE = 48.0f;
    /** The covered share assumed for a sprite whose pixels cannot be read (a straight half line of vanilla's). */
    private static final float DEFAULT_COVERAGE = 0.08f;

    /** The share of each dust sprite its texels cover (mean alpha), worked out once per sprite of an atlas. */
    private static final Map<TextureAtlasSprite, Float> COVERAGE = new ConcurrentHashMap<>();

    private RedstoneGlow() {
    }

    /** Forgets the sprites of the last atlas; a reload makes new ones. */
    public static void invalidate() {
        COVERAGE.clear();
    }

    /** The power of this redstone dust, 0 to 15, or -1 when it is not dust. */
    static int power(BlockState state) {
        return state.is(Blocks.REDSTONE_WIRE) ? state.getValue(RedstoneWireBlock.POWER) : -1;
    }

    /**
     * Whether this quad of a dust block is the dust itself rather than the overlay drawn over it. Since Minecraft 26
     * the dust draws a sprite of its own per power, already coloured, and no quad of it is tinted any more: tested on
     * the tint, no quad glowed. The tint still marks it where a pack's model keeps vanilla's older tinted dust.
     */
    static boolean isDust(net.minecraft.client.resources.model.geometry.BakedQuad quad) {
        return quad.materialInfo().isTinted()
            || !quad.materialInfo().sprite().contents().name().getPath().endsWith("_overlay");
    }

    /** The glow seen on dust of this power, before its colour: none at 0, then from the floor up to SEEN_GLOW. */
    static float seenGlow(int power) {
        if (power <= 0) {
            return 0.0f;
        }
        return SEEN_GLOW * (SEEN_FLOOR + (1.0f - SEEN_FLOOR) * power / 15.0f);
    }

    /**
     * The light a dust quad of this power drawing this sprite casts, before its colour: linear in power, as vanilla's
     * signal strength and the RTX Remix redstone glow have it, so a long weak line lights little; and in proportion to
     * how much of the quad the sprite covers, so a junction of five quads casts by the dust it shows, not five times
     * a line's light.
     */
    static float castLight(int power, TextureAtlasSprite sprite) {
        if (power <= 0) {
            return 0.0f;
        }
        return CAST_PER_COVERAGE * coverage(sprite) * power / 15.0f;
    }

    private static float coverage(TextureAtlasSprite sprite) {
        Float known = COVERAGE.get(sprite);
        if (known != null) {
            return known;
        }
        float measured = measureCoverage(sprite);
        COVERAGE.put(sprite, measured);
        return measured;
    }

    private static float measureCoverage(TextureAtlasSprite sprite) {
        SpriteContents contents = sprite.contents();
        ByteBuffer[] mips = ((SpriteContentsAccess) contents).radiante$mipImages();
        if (mips.length == 0 || mips[0] == null) {
            return DEFAULT_COVERAGE;
        }
        ByteBuffer pixels = mips[0];
        int width = contents.width();
        int height = contents.height();
        int rowPixels = Math.max(1, ((SpriteContentsAccess) contents).radiante$mipWidth(0));
        long alpha = 0;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int index = (y * rowPixels + x) * 4 + 3;
                if (index < pixels.capacity()) {
                    alpha += pixels.get(index) & 0xFF;
                }
            }
        }
        return width * height == 0 ? DEFAULT_COVERAGE : alpha / (255.0f * width * height);
    }
}
