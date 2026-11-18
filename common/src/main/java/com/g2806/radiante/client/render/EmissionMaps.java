package com.g2806.radiante.client.render;

import java.nio.ByteBuffer;
import net.minecraft.client.renderer.texture.SpriteContents;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.resources.Identifier;

/**
 * Synthesis, softening and preservation of per-texel emission in Bedrock MER and LabPBR specular atlas maps
 * (split out of {@link EmissionTiles}, which builds and uploads the sampled emitter cells).
 */
final class EmissionMaps {

    /** The share of a firefly bush's pack glow that is kept: its few specks are bright, and were glaring. */
    private static final float FIREFLY_GLOW = 0.25f;

    /**
     * While Bedrock's glow is cast lava glows and casts what its pack's MER map says (EmissionTiles.uploadFromMer), so
     * none of the legacy tuning (surface glow, cast, light colour) reached it: a sea of lava, glowing at the pack's
     * full strength, burnt the Nether out to pale peach and a flat red. The share of that glow kept (its MER green is
     * set to its root, see softenMer).
     */
    private static final float LAVA_MER_GLOW = 0.008f;

    private EmissionMaps() {
    }

    /**
     * Writes a MER map for a light block no Bedrock pack covers, with the built-in glow for it
     * (BedrockDefaults): its MER green squared, summed over the sprite, comes to what the pack's does, so the block
     * casts the light it would with the pack (uploadFromMer) and its glowing texels show that light. Which texels glow,
     * and how much more than each other, is what Radiante worked out from vanilla's texture (the derived emission
     * already in the specular map): the pack's own pattern belongs to its texture. Metalness and roughness on those
     * texels are the means of the pack's glowing ones; every other texel is left without a Bedrock material (alpha 0)
     * and keeps Radiante's. Alpha 255 marks a texel without a height map, so it is not bevelled.
     */
    static void defaultMer(TextureAtlasSprite sprite, BedrockDefaults.Glow glow, ByteBuffer specular, ByteBuffer mer,
        int atlasWidth, int atlasHeight) {
        int width = sprite.contents().width();
        int height = sprite.contents().height();
        int originX = Math.round(sprite.getU0() * atlasWidth);
        int originY = Math.round(sprite.getV0() * atlasHeight);
        ByteBuffer[] mips = ((SpriteContentsAccess) sprite.contents()).radiante$mipImages();
        ByteBuffer pixels = mips.length == 0 ? null : mips[0];
        int rowPixels = Math.max(1, ((SpriteContentsAccess) sprite.contents()).radiante$mipWidth(0));
        float[] weights = new float[width * height];
        float total = 0.0f;
        int opaque = 0;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int pixel = (y * rowPixels + x) * 4 + 3;
                // As uploadFromMer: a cut out texel gives no light.
                if (pixels == null || pixel >= pixels.capacity() || (pixels.get(pixel) & 0xFF) < 128) {
                    continue;
                }
                opaque++;
                int index = ((originY + y) * atlasWidth + originX + x) * 4 + 3;
                int value = index < specular.capacity() ? specular.get(index) & 0xFF : 0;
                // LabPBR's emission: 1 to 254 from faintest to full, 0 and 255 none.
                float weight = value == 0 || value == 255 ? 0.0f : value / 254.0f;
                weights[y * width + x] = weight;
                total += weight;
            }
        }
        if (total <= 0.0f) {
            // Nothing was worked out to glow: the opaque texels share the glow evenly.
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    int pixel = (y * rowPixels + x) * 4 + 3;
                    if (pixels != null && pixel < pixels.capacity() && (pixels.get(pixel) & 0xFF) >= 128) {
                        weights[y * width + x] = 1.0f;
                    }
                }
            }
            total = opaque;
        }
        if (total <= 0.0f || glow.glow() <= 0.0f) {
            return;
        }
        float scale = glow.glow() * width * height / total;
        byte metal = (byte) Math.round(Math.max(0.0f, Math.min(1.0f, glow.metalness())) * 255.0f);
        byte rough = (byte) Math.round(Math.max(0.0f, Math.min(1.0f, glow.roughness())) * 255.0f);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                float weight = weights[y * width + x];
                int index = ((originY + y) * atlasWidth + originX + x) * 4;
                if (weight <= 0.0f || index + 3 >= mer.capacity()) {
                    continue;
                }
                // Green squared is the light; a texel takes at most a full green, which no light block's share
                // reaches but a candle's single flame texel nearly does.
                float green = (float) Math.sqrt(Math.min(1.0f, scale * weight));
                mer.put(index, metal);
                mer.put(index + 1, (byte) Math.max(1, Math.round(green * 255.0f)));
                mer.put(index + 2, rough);
                mer.put(index + 3, (byte) 255);
            }
        }
    }

    /**
     * Tones down a block whose pack glow is a few very bright specks (the firefly bush): emission is the MER green
     * squared, so its green is scaled by the root of the share kept. Done on the map itself, before the light is
     * taken from it, so the specks and the light they cast are softened together.
     */
    static void softenMer(TextureAtlasSprite sprite, ByteBuffer mer, int atlasWidth, int atlasHeight) {
        boolean lava = EmissionTiles.isLava(sprite);
        if (!lava && !sprite.contents().name().getPath().startsWith("block/firefly_bush")) {
            return;
        }
        float scale = (float) Math.sqrt(FIREFLY_GLOW);
        // Lava: its emission is one value all over, MER green squared = LAVA_MER_GLOW. The pack's map has one frame
        // only here (the atlas keeps the first of lava's 32), while the colour animates: a glow pattern of its own
        // stood still under the moving colour, and a hot fleck's glow was far above the dark cells' (the texture's
        // contrast was burnt out). Constant, the surface shows the colour as it flows.
        int lavaGreen = Math.round(255.0f * (float) Math.sqrt(LAVA_MER_GLOW));
        int originX = Math.round(sprite.getU0() * atlasWidth);
        int originY = Math.round(sprite.getV0() * atlasHeight);
        for (int y = 0; y < sprite.contents().height(); y++) {
            for (int x = 0; x < sprite.contents().width(); x++) {
                int index = ((originY + y) * atlasWidth + originX + x) * 4 + 1;
                if (index < mer.capacity()) {
                    mer.put(index, lava ? (byte) lavaGreen : (byte) Math.round((mer.get(index) & 0xFF) * scale));
                }
            }
        }
    }

    /**
     * The same softening for a block whose glow comes from a LabPBR specular map (alpha 1..254, linear) rather than a
     * MER one: the vanilla textures' own specks, and a pack's _s maps.
     */
    static void softenSpecular(TextureAtlasSprite sprite, ByteBuffer specular, int atlasWidth, int atlasHeight) {
        if (!sprite.contents().name().getPath().startsWith("block/firefly_bush")) {
            return;
        }
        int originX = Math.round(sprite.getU0() * atlasWidth);
        int originY = Math.round(sprite.getV0() * atlasHeight);
        for (int y = 0; y < sprite.contents().height(); y++) {
            for (int x = 0; x < sprite.contents().width(); x++) {
                int index = ((originY + y) * atlasWidth + originX + x) * 4 + 3;
                if (index < specular.capacity()) {
                    int value = specular.get(index) & 0xFF;
                    if (value != 0 && value != 255) {
                        specular.put(index, (byte) Math.max(1, Math.round(value * FIREFLY_GLOW)));
                    }
                }
            }
        }
    }

    /** A sprite whose glow Radiante sets itself, whatever a pack gives it: see {@link #keepSetGlow}. */
    static boolean setsOwnGlow(Identifier spriteId) {
        // Not while Bedrock's glow is cast: then the eye glows and casts what its pack's MER map says, as in Bedrock
        // RTX (Vanilla RTX: 5 of 255, next to nothing). Kept out of the MER, its worked out level-8 glow lit a
        // closed room brighter than three torches.
        return EmissionTiles.END_PORTAL_EYE.equals(spriteId)
            && !com.g2806.radiante.client.pipeline.PipelineSettings.castsBedrockGlow();
    }

    /**
     * A sprite whose glow Radiante sets itself (an eye of ender in its frame) keeps that strength under a pack's
     * own specular map: the pack's emission gives the shape, and its brightest texel is brought up to the level set
     * here. Packs author emission on scales of their own - Vanilla RTX gives the eye 5 of 255, a glow too faint to
     * see - and the light the eye casts already follows the level, so the eye lit the frame around it without
     * visibly glowing itself. A map with no emission at all leaves the whole sprite glowing.
     */
    static void keepSetGlow(TextureAtlasSprite sprite, ByteBuffer specular, int atlasWidth, int atlasHeight) {
        if (!setsOwnGlow(sprite.contents().name())) {
            return;
        }
        SpriteContents contents = sprite.contents();
        int width = contents.width();
        int height = contents.height();
        int originX = Math.round(sprite.getU0() * atlasWidth);
        int originY = Math.round(sprite.getV0() * atlasHeight);
        float strength = EmissionTiles.END_PORTAL_EYE_GLOW;
        // LabPBR's emission: 1 to 254 from faintest to full, 0 and 255 none.
        int peak = 0;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int index = ((originY + y) * atlasWidth + originX + x) * 4 + 3;
                if (index < specular.capacity()) {
                    int value = specular.get(index) & 0xFF;
                    peak = Math.max(peak, value == 255 ? 0 : value);
                }
            }
        }
        ByteBuffer[] mips = ((SpriteContentsAccess) contents).radiante$mipImages();
        ByteBuffer pixels = mips.length == 0 ? null : mips[0];
        int rowPixels = Math.max(1, ((SpriteContentsAccess) contents).radiante$mipWidth(0));
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int index = ((originY + y) * atlasWidth + originX + x) * 4 + 3;
                if (index >= specular.capacity()) {
                    continue;
                }
                int value = specular.get(index) & 0xFF;
                if (peak > 0) {
                    if (value > 0 && value < 255 && peak < strength * 254.0f) {
                        writeSpecularEmission(specular, atlasWidth, atlasHeight, originX + x, originY + y,
                            strength * value / peak);
                    }
                    continue;
                }
                int pixel = (y * rowPixels + x) * 4;
                if (pixels == null || pixel + 3 >= pixels.capacity() || (pixels.get(pixel + 3) & 0xFF) < 26) {
                    continue;
                }
                writeSpecularEmission(specular, atlasWidth, atlasHeight, originX + x, originY + y, strength);
            }
        }
    }

    /**
     * The glow Radiante worked out for a light block's sprite (see {@link EmissionTiles}), kept before a pack's
     * specular map is laid over it, for {@link #keepVisibleGlow}; null for a sprite that gives no light.
     */
    static byte[] derivedGlow(TextureAtlasSprite sprite, ByteBuffer specular, int atlasWidth, int atlasHeight) {
        int level = EmissionTiles.lightLevelOf(sprite);
        if (level <= 0) {
            return null;
        }
        int width = sprite.contents().width();
        int height = sprite.contents().height();
        int originX = Math.round(sprite.getU0() * atlasWidth);
        int originY = Math.round(sprite.getV0() * atlasHeight);
        byte[] glow = new byte[width * height];
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int index = ((originY + y) * atlasWidth + originX + x) * 4 + 3;
                if (index < specular.capacity()) {
                    glow[y * width + x] = specular.get(index);
                }
            }
        }
        return glow;
    }

    /**
     * A light block whose pack gives it little glow - Vanilla RTX authors its redstone ore with an emission of 4 of
     * 255, its redstone torch 13, a block of redstone 8 - lit everything around it without seeming to give that light
     * itself: what it casts follows its light level, what it shows followed the pack alone. The pack's map keeps the
     * shape (which texels glow, and how much more than each other), and its brightest texel is brought up to what a
     * light of that level shows when Radiante works the glow out itself (the level times that texel's brightness); a
     * map that gives the block no glow at all gives way to the worked out one. Lights the pack already makes brighter
     * than that are left as they are. (While Bedrock's glow is cast, radiante-pt shows and casts the glow of the MER map as
     * the pack authors it, as Bedrock RTX does: see EmissionTiles.uploadFromMer.)
     */
    static void keepVisibleGlow(TextureAtlasSprite sprite, ByteBuffer specular, byte[] derived, int atlasWidth,
        int atlasHeight) {
        int level = EmissionTiles.lightLevelOf(sprite);
        // The eye of ender in its frame is no lamp: it shows the glow its pack gives it (see setsOwnGlow).
        if (level <= 0 || EmissionTiles.END_PORTAL_EYE.equals(sprite.contents().name())) {
            return;
        }
        SpriteContents contents = sprite.contents();
        int width = contents.width();
        int height = contents.height();
        int originX = Math.round(sprite.getU0() * atlasWidth);
        int originY = Math.round(sprite.getV0() * atlasHeight);
        ByteBuffer[] mips = ((SpriteContentsAccess) contents).radiante$mipImages();
        ByteBuffer pixels = mips.length == 0 ? null : mips[0];
        int rowPixels = Math.max(1, ((SpriteContentsAccess) contents).radiante$mipWidth(0));

        // LabPBR's emission: 1 to 254 from faintest to full, 0 and 255 none.
        int peak = 0;
        float brightest = 0.0f;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int index = ((originY + y) * atlasWidth + originX + x) * 4 + 3;
                int value = index < specular.capacity() ? specular.get(index) & 0xFF : 0;
                if (value == 0 || value == 255) {
                    continue;
                }
                peak = Math.max(peak, value);
                int pixel = (y * rowPixels + x) * 4;
                if (pixels != null && pixel + 2 < pixels.capacity()) {
                    brightest = Math.max(brightest, Math.max(pixels.get(pixel) & 0xFF,
                        Math.max(pixels.get(pixel + 1) & 0xFF, pixels.get(pixel + 2) & 0xFF)) / 255.0f);
                }
            }
        }
        if (peak == 0) {
            if (derived == null) {
                return;
            }
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    int index = ((originY + y) * atlasWidth + originX + x) * 4 + 3;
                    if (index < specular.capacity()) {
                        specular.put(index, derived[y * width + x]);
                    }
                }
            }
        } else {
            int target = Math.round(254.0f * level / 15.0f * (pixels == null ? 1.0f : brightest));
            if (peak < target) {
                float scale = target / (float) peak;
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        int index = ((originY + y) * atlasWidth + originX + x) * 4 + 3;
                        int value = index < specular.capacity() ? specular.get(index) & 0xFF : 0;
                        if (value != 0 && value != 255) {
                            specular.put(index, (byte) Math.max(1, Math.min(254, Math.round(value * scale))));
                        }
                    }
                }
            }
        }
    }

    static void writeSpecularEmission(ByteBuffer specular, int width, int height, int x, int y, float emission) {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return;
        }
        int value = Math.max(1, Math.min(254, Math.round(emission * 254.0f)));
        specular.put((y * width + x) * 4 + 3, (byte) value);
    }
}
