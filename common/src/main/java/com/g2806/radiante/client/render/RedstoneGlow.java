package com.g2806.radiante.client.render;

import java.nio.ByteBuffer;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.client.renderer.texture.SpriteContents;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.RedStoneWireBlock;
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
    static final float SEEN_GLOW = Glow.ofLevel(6);
    /**
     * Share of the full seen glow that power 1 keeps, so the faintest powered dust still reads as lit. Kept small:
     * at 0.3, with the glow's red saturating once tone mapped, a line fading from 15 to 1 looked the same from end to
     * end.
     */
    private static final float SEEN_FLOOR = 0.04f;
    /**
     * The light cast per unit of covered area at power 15 (radiance on the light-level scale of EmissionTiles, kind 0).
     * A straight line's sprite covers about a twelfth of its quad, so at power 15 it casts about 1.5 in its red: a
     * faint red on the floor next to the wire at night, well under a redstone torch (level 7 on five faces, about
     * 11). Dust is a hint of glow, not a lamp: at 48 a lit line read as a light source of its own.
     */
    static final float CAST_PER_COVERAGE = 18.0f;
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
        return state.is(Blocks.REDSTONE_WIRE) ? state.getValue(RedStoneWireBlock.POWER) : -1;
    }

    /**
     * Whether this quad of a dust block is the dust itself rather than the overlay drawn over it. This version's dust
     * is one greyscale sprite tinted by power, and the overlay is the only quad left untinted. A pack's model may give
     * the dust a sprite per power, already coloured, as Minecraft 26.3 does: so the sprite's name is what rules it out.
     */
    static boolean isDust(net.minecraft.client.resources.model.geometry.BakedQuad quad) {
        return quad.materialInfo().isTinted()
            || !quad.materialInfo().sprite().contents().name().getPath().endsWith("_overlay");
    }

    /**
     * The colour of the light dust of this power casts, 0xRRGGBB: vanilla's colour for the power (as the older
     * RedStoneWireBlock.getColorForPower worked it out), from dark red at power 1 to bright red at 15.
     */
    static int castColor(int power) {
        float f = power / 15.0f;
        float r = f * 0.6f + (f > 0.0f ? 0.4f : 0.3f);
        float g = Math.max(0.0f, Math.min(1.0f, f * f * 0.7f - 0.5f));
        float b = Math.max(0.0f, Math.min(1.0f, f * f * 0.6f - 0.7f));
        return Math.round(r * 255.0f) << 16 | Math.round(g * 255.0f) << 8 | Math.round(b * 255.0f);
    }

    /**
     * The glow seen on dust of this power, before its colour: none at 0, then from the floor up to SEEN_GLOW along the
     * square of the power, so the signal's fall along a line shows as a clear fade rather than a red that saturates
     * long before the weak end.
     */
    static float seenGlow(int power) {
        if (power <= 0) {
            return 0.0f;
        }
        float f = power / 15.0f;
        return SEEN_GLOW * (SEEN_FLOOR + (1.0f - SEEN_FLOOR) * f * f);
    }

    /**
     * The light a dust quad of this power drawing this sprite casts, before its colour: along the square of the power,
     * like the seen glow, so the floor light fades with the signal and a long weak line lights next to nothing; and in proportion to
     * how much of the quad the sprite covers, so a junction of five quads casts by the dust it shows, not five times
     * a line's light.
     */
    static float castLight(int power, TextureAtlasSprite sprite) {
        if (power <= 0) {
            return 0.0f;
        }
        float f = power / 15.0f;
        return CAST_PER_COVERAGE * coverage(sprite) * f * f;
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
