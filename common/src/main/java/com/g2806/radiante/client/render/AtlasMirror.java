package com.g2806.radiante.client.render;

import com.g2806.radiante.client.proxy.vulkan.TextureProxy;
import com.g2806.radiante.compat.VersionCompat;
import it.unimi.dsi.fastutil.ints.IntArrayList;
import java.nio.ByteBuffer;
import java.util.List;
import net.minecraft.client.renderer.texture.SpriteContents;
import net.minecraft.client.renderer.texture.TextureAtlas;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;

/**
 * Minecraft stitches atlases on the GPU, so the mirrored copy the ray tracer samples is rebuilt here from the
 * sprite images instead of from texture writes.
 */
public final class AtlasMirror {

    private AtlasMirror() {
    }

    public static void mirror(TextureAtlas atlas, List<TextureAtlasSprite> sprites, int maxMipLevel) {
        if (!RadianteRenderer.isActive() || TextureTracker.gpuTextureOrNull(atlas) == null) {
            return;
        }

        var texture = VersionCompat.gpuTexture(atlas);
        int atlasWidth = texture.getWidth(0);
        int atlasHeight = texture.getHeight(0);

        // How much of its detail the tracer keeps (Texture Detail): the image is made again without the levels left
        // out, and the uploads below into those are dropped on the way.
        int dropped = TracedTextureDetail.choose(TextureTracker.idOf(texture), atlas.location().toString(),
            atlasWidth, atlasHeight, Math.min(maxMipLevel + 1, texture.getMipLevels()), typicalSpriteWidth(sprites));
        TextureTracker.prepareDetail(texture, dropped);

        // The block atlas's cut-out coverage is taken from the uploads below, for the opacity micromaps of leaves,
        // grass and the like. Animated sprites change from frame to frame and are left to the shaders.
        int coverageId = atlas.location().equals(TextureAtlas.LOCATION_BLOCKS)
            ? TextureTracker.idOf(texture) : 0;
        IntArrayList animated = new IntArrayList();
        if (coverageId != 0) {
            // Taken at the detail traced, which the micromaps have to agree with.
            TextureProxy.beginCutoutCoverage(coverageId, Math.max(1, atlasWidth >> dropped),
                Math.max(1, atlasHeight >> dropped));
        }

        for (TextureAtlasSprite sprite : sprites) {
            SpriteContents contents = sprite.contents();
            // The stitcher reserves a padding border around every sprite, so the pixels live at the UV origin
            // rather than at the slot origin the sprite reports.
            int paddingX = Math.round(sprite.getU0() * atlasWidth) - sprite.getX();
            int paddingY = Math.round(sprite.getV0() * atlasHeight) - sprite.getY();
            ByteBuffer[] mips = ((SpriteContentsAccess) contents).radiante$mipImages();
            int levels = Math.min(maxMipLevel + 1, mips.length);
            if (coverageId != 0 && sprite.isAnimated()) {
                animated.add((sprite.getX() + paddingX) >> dropped);
                animated.add((sprite.getY() + paddingY) >> dropped);
                animated.add(Math.max(1, contents.width() >> dropped));
                animated.add(Math.max(1, contents.height() >> dropped));
            }

            for (int level = dropped; level < levels; level++) {
                ByteBuffer pixels = mips[level];
                if (pixels == null) {
                    continue;
                }

                int width = Math.max(1, contents.width() >> level);
                int height = Math.max(1, contents.height() >> level);
                int rowPixels = Math.max(1, ((SpriteContentsAccess) contents).radiante$mipWidth(level));
                int x = (sprite.getX() + paddingX) >> level;
                int y = (sprite.getY() + paddingY) >> level;
                TextureTracker.uploadRegion(texture, pixels, rowPixels, 0, 0, x, y, width, height, level);

                // The border the stitcher leaves round a sprite repeats the sprite's edge in Minecraft's atlas.
                // Left empty here, a ray hitting a face on its very edge sampled it - and on a cut out texture
                // empty is a hole: a hairline along the top of a grass block where the fringe let the layer under
                // it through. One texel of each edge is repeated outwards, which is as far as such a sample strays.
                int levelWidth = Math.max(1, atlasWidth >> level);
                int levelHeight = Math.max(1, atlasHeight >> level);
                if (paddingY >> level > 0 || level == dropped && paddingY > 0) {
                    if (y > 0) {
                        TextureTracker.uploadRegion(texture, pixels, rowPixels, 0, 0, x, y - 1, width, 1,
                            level);
                    }
                    if (y + height < levelHeight) {
                        TextureTracker.uploadRegion(texture, pixels, rowPixels, 0, height - 1, x,
                            y + height, width, 1, level);
                    }
                }
                if (paddingX >> level > 0 || level == dropped && paddingX > 0) {
                    if (x > 0) {
                        TextureTracker.uploadRegion(texture, pixels, rowPixels, 0, 0, x - 1, y, 1, height,
                            level);
                    }
                    if (x + width < levelWidth) {
                        TextureTracker.uploadRegion(texture, pixels, rowPixels, width - 1, 0, x + width, y,
                            1, height, level);
                    }
                }
            }
        }

        if (coverageId != 0) {
            TextureProxy.finishCutoutCoverage(coverageId, animated.toIntArray());
        }
    }

    /** The sprite width most of an atlas's sprites have: its pack's pixels a block. */
    static int typicalSpriteWidth(List<TextureAtlasSprite> sprites) {
        java.util.Map<Integer, Integer> counts = new java.util.HashMap<>();
        int typical = 16;
        int most = 0;
        for (TextureAtlasSprite sprite : sprites) {
            int count = counts.merge(sprite.contents().width(), 1, Integer::sum);
            if (count > most) {
                most = count;
                typical = sprite.contents().width();
            }
        }
        return typical;
    }
}
