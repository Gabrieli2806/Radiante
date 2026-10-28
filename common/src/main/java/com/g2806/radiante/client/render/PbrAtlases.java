package com.g2806.radiante.client.render;

import com.g2806.radiante.client.proxy.vulkan.TextureProxy;
import com.g2806.radiante.mixin.texture.TextureAtlasAccessor;
import com.mojang.blaze3d.platform.NativeImage;
import com.mojang.renderpearl.api.GpuFormat;
import com.mojang.renderpearl.backend.vulkan.VulkanConst;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.util.Map;
import java.util.Optional;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.texture.TextureAtlas;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.resources.Identifier;
import net.minecraft.server.packs.resources.Resource;
import net.minecraft.server.packs.resources.ResourceManager;
import org.lwjgl.system.MemoryUtil;

/**
 * Stitches the PBR maps a resource pack ships next to its textures onto atlases that match the block atlas, so the
 * ray tracer can read roughness, metalness, emission and surface normals per texel. This is the LabPBR layout every
 * shader pack uses: "_s" carries roughness, metalness and emission, "_n" carries the normal plus ambient occlusion
 * and height. Textures without these files keep their neutral defaults and look exactly as before.
 */
public final class PbrAtlases {

    /** A normal map of all zeroes reads as "no map"; the alpha keeps the height at its neutral value. */
    static final int NORMAL_DEFAULT = 0xFF000000;
    private static final int SPECULAR_DEFAULT = 0x00000000;

    private static int specularTextureId = -1;
    private static int normalTextureId = -1;
    private static int rawMerTextureId = -1;
    private static boolean hasRawMer;
    private static int atlasId = -1;
    private static int spriteCount;

    private PbrAtlases() {
    }

    public static void invalidate() {
        atlasId = -1;
        spriteCount = 0;
        hasRawMer = false;
    }

    /** The specular map stitched for this atlas, or -1 when the pack ships none. */
    public static int specularTextureFor(int albedoTextureId) {
        return albedoTextureId == atlasId && spriteCount > 0 ? specularTextureId : -1;
    }

    public static int rawMerTextureFor(int albedoTextureId) {
        return albedoTextureId == atlasId && hasRawMer ? rawMerTextureId : -1;
    }

    /** The normal map stitched for this atlas, or -1 when the pack ships none. */
    public static int normalTextureFor(int albedoTextureId) {
        return albedoTextureId == atlasId && spriteCount > 0 ? normalTextureId : -1;
    }

    /**
     * Builds both maps for an atlas. Returns the number of sprites a map was found for, zero when the pack ships
     * none, in which case nothing is uploaded and the renderer keeps its own generated emission.
     */
    public static int build(Minecraft minecraft, TextureAtlas atlas, int albedoTextureId,
        @org.jetbrains.annotations.Nullable ByteBuffer specularSeed) {
        Map<Identifier, TextureAtlasSprite> sprites = ((TextureAtlasAccessor) (Object) atlas).radiante$texturesByName();
        if (sprites.isEmpty()) {
            return 0;
        }

        int width = atlas.getTexture().texture().getWidth(0);
        int height = atlas.getTexture().texture().getHeight(0);
        int mipLevels = atlas.getTexture().texture().getMipLevels();
        ResourceManager resources = minecraft.getResourceManager();

        // Emission Radiante derived from the albedo comes in as the seed, so a block whose map nobody authored -
        // a lit redstone lamp, say - keeps the glow that was worked out for it, and an authored map simply writes
        // over its own texels. Without this a single authored map anywhere turned the derived emission off for
        // every block at once, which is what left redstone lamps dark with vanilla textures.
        ByteBuffer specular = specularSeed != null ? specularSeed : fill(width, height, SPECULAR_DEFAULT);
        boolean ownsSpecular = specularSeed == null;
        ByteBuffer normal = fill(width, height, NORMAL_DEFAULT);
        ByteBuffer rawMer = null;
        hasRawMer = false;
        int found = 0;
        Map<String, Integer> packHeights = packHeights(resources);
        // Only with no Bedrock pack at all: one that leaves a light block out means it to glow as Radiante works out.
        boolean defaults = BedrockDefaults.emissionActive() && resources.listResources("bedrock_mer",
            id -> id.getNamespace().equals("radiante") && id.getPath().endsWith(".png")).isEmpty();
        try {
            for (Map.Entry<Identifier, TextureAtlasSprite> entry : sprites.entrySet()) {
                if (isPackMap(sprites, entry.getKey())) {
                    continue;
                }
                Optional<Resource> raw = rawMerResource(resources, entry.getKey(), packHeights);
                boolean merStitched = false;
                if (raw.isPresent()) {
                    if (rawMer == null) { rawMer = fill(width, height, 0); }
                    merStitched = stitchResource(raw.get(), entry.getValue(), rawMer, width, height, true, true);
                    hasRawMer |= merStitched;
                    if (merStitched) {
                        EmissionTiles.softenMer(entry.getValue(), rawMer, width, height);
                        EmissionTiles.uploadFromMer(albedoTextureId, entry.getValue(), rawMer, width, height);
                    }
                } else if (defaults && EmissionTiles.isLightSprite(entry.getValue())
                    && BedrockDefaults.glow(entry.getKey()) != null && coversVanilla(resources, entry.getKey())) {
                    // No Bedrock pack covers this light block: it takes Radiante's built-in Bedrock RTX
                    // glow, through the very path a pack's MER map takes.
                    if (rawMer == null) { rawMer = fill(width, height, 0); }
                    EmissionTiles.defaultMer(entry.getValue(), BedrockDefaults.glow(entry.getKey()), specular, rawMer,
                        width, height);
                    hasRawMer = true;
                    EmissionTiles.softenMer(entry.getValue(), rawMer, width, height);
                    EmissionTiles.uploadFromMer(albedoTextureId, entry.getValue(), rawMer, width, height);
                }
                byte[] derivedGlow = EmissionTiles.derivedGlow(entry.getValue(), specular, width, height);
                boolean any = stitch(resources, entry.getKey(), entry.getValue(), "_s", specular, width, height);
                if (any) {
                    EmissionTiles.keepSetGlow(entry.getValue(), specular, width, height);
                    EmissionTiles.keepVisibleGlow(entry.getValue(), specular, derivedGlow, width, height);
                    EmissionTiles.softenSpecular(entry.getValue(), specular, width, height);
                }
                any |= stitch(resources, entry.getKey(), entry.getValue(), "_n", normal, width, height);
                if (any) {
                    found++;
                }
            }

            if (found == 0 && !hasRawMer) {
                return 0;
            }

            specularTextureId = upload(specularTextureId, specular, width, height, mipLevels);
            normalTextureId = upload(normalTextureId, normal, width, height, mipLevels);
            if (hasRawMer) { rawMerTextureId = upload(rawMerTextureId, rawMer, width, height, mipLevels); }
        } finally {
            if (ownsSpecular) {
                MemoryUtil.memFree(specular);
            }
            MemoryUtil.memFree(normal);
            if (rawMer != null) { MemoryUtil.memFree(rawMer); }
        }

        atlasId = albedoTextureId;
        spriteCount = Math.max(found, hasRawMer ? 1 : 0);
        if (com.g2806.radiante.client.option.Options.debugLogging) {
            RadianteRenderer.LOGGER.info("Stitched PBR maps for {} of {} sprites", found, sprites.size());
        }
        return spriteCount;
    }

    /**
     * True for a sprite that is itself one of a pack's PBR maps. Minecraft 26.3 builds the block atlas from a
     * directory source, so every PNG under textures/block - "stone_s.png" and "stone_n.png" included - is stitched
     * in as a sprite of its own. Those have to be skipped as bases, but they must not be refused as maps: an
     * earlier version rejected any map that was also a sprite, and with a directory source that is every map a
     * pack ships, so no PBR pack worked at all. Vanilla has no texture named after another with these suffixes.
     */
    static boolean isPackMap(Map<Identifier, TextureAtlasSprite> sprites, Identifier spriteId) {
        String path = spriteId.getPath();
        if (!path.endsWith("_s") && !path.endsWith("_n")) {
            return false;
        }
        Identifier base = Identifier.fromNamespaceAndPath(spriteId.getNamespace(),
            path.substring(0, path.length() - 2));
        return sprites.containsKey(base);
    }

    static ByteBuffer fill(int width, int height, int value) {
        ByteBuffer buffer = MemoryUtil.memAlloc(width * height * 4);
        for (int i = 0; i < width * height; i++) {
            buffer.putInt(i * 4, value);
        }
        return buffer;
    }

    /**
     * Copies one sprite's map into the atlas. Packs are free to author these at a different resolution than the
     * base texture, and animated textures stack their frames, so only the first frame is read and it is brought to
     * the size the sprite occupies (see stitchResource).
     */
    static boolean stitch(ResourceManager resources, Identifier spriteId, TextureAtlasSprite sprite,
        String suffix, ByteBuffer target, int width, int height) {
        Identifier mapId = Identifier.fromNamespaceAndPath(spriteId.getNamespace(),
            "textures/" + spriteId.getPath() + suffix + ".png");
        Optional<Resource> resource = resources.getResource(mapId);
        if (resource.isEmpty()) {
            return false;
        }

        return stitchResource(resource.get(), sprite, target, width, height, false, suffix.equals("_n"));
    }

    /**
     * Whether a sprite is still vanilla's own: its colour from the game and its specular map, if any, Radiante's
     * built-in one. The built-in glow is for vanilla's light blocks; a Java pack that redraws one, or gives it a
     * material of its own, keeps it.
     */
    private static boolean coversVanilla(ResourceManager resources, Identifier spriteId) {
        Optional<Resource> albedo = resources.getResource(Identifier.fromNamespaceAndPath(spriteId.getNamespace(),
            "textures/" + spriteId.getPath() + ".png"));
        if (albedo.isEmpty() || !albedo.get().sourcePackId().equals("vanilla")) {
            return false;
        }
        Optional<Resource> specular = resources.getResource(Identifier.fromNamespaceAndPath(spriteId.getNamespace(),
            "textures/" + spriteId.getPath() + "_s.png"));
        if (specular.isEmpty()) {
            return true;
        }
        // Radiante's own resources come from its mod's pack, which its icon is in too.
        Optional<Resource> own = resources.getResource(Identifier.fromNamespaceAndPath("radiante", "icon.png"));
        return own.isPresent() && own.get().sourcePackId().equals(specular.get().sourcePackId());
    }

    /** Where each pack sits in the stack, counted from the bottom, by its id. */
    private static Map<String, Integer> packHeights(ResourceManager resources) {
        Map<String, Integer> heights = new java.util.HashMap<>();
        resources.listPacks().forEach(pack -> heights.put(pack.packId(), heights.size()));
        return heights;
    }

    private static Optional<Resource> rawMerResource(ResourceManager resources, Identifier spriteId,
        Map<String, Integer> packHeights) {
        // Nor for a sprite whose glow Radiante sets itself: radiante-pt's Bedrock RTX look takes emission from
        // the MER as authored, and the eye of ender sat dark again in the light it casts.
        if (!spriteId.getNamespace().equals("minecraft") || !spriteId.getPath().startsWith("block/")
            || EmissionTiles.setsOwnGlow(spriteId)) {
            return Optional.empty();
        }
        Identifier id = Identifier.fromNamespaceAndPath("radiante",
            "bedrock_mer/" + spriteId.getPath().substring("block/".length()) + ".png");
        Optional<Resource> raw = resources.getResource(id);
        Optional<Resource> specular = resources.getResource(Identifier.fromNamespaceAndPath("minecraft",
            "textures/" + spriteId.getPath() + "_s.png"));
        Optional<Resource> albedo = resources.getResource(Identifier.fromNamespaceAndPath("minecraft",
            "textures/" + spriteId.getPath() + ".png"));
        // Never attach a lower pack's MER to a higher pack's replacement material or colour. A colour from below
        // the MER's own pack is another matter: the pack means to keep it. The conversion leaves Java's colour in
        // place where it cannot take Bedrock's (animated lanterns, lava, fire, ice), and the MER belongs on it;
        // refused there, none of those got the material or the light their pack gives them.
        if (raw.isEmpty() || specular.isEmpty()
            || !raw.get().sourcePackId().equals(specular.get().sourcePackId())
            || (albedo.isPresent() && packHeights.getOrDefault(albedo.get().sourcePackId(), -1)
                > packHeights.getOrDefault(raw.get().sourcePackId(), -1))) {
            return Optional.empty();
        }
        return raw;
    }

    /**
     * A map larger than its sprite covers each of the sprite's texels with a block of its own. {@code average}:
     * the texel gets the block's mean (normals, heights, a MER map's plain values). Otherwise it gets the block's
     * middle texel: a LabPBR specular map holds codes (which metal, how porous) that have no mean.
     *
     * <p>Every texel used to get its block's first, corner texel, whatever the rest of the block held. (Bedrock
     * packs do not come here larger: their conversion brings the colour up to the normal map's size.)
     */
    private static boolean stitchResource(Resource resource, TextureAtlasSprite sprite,
        ByteBuffer target, int width, int height, boolean rawMer, boolean average) {
        try (InputStream stream = resource.open(); NativeImage image = NativeImage.read(stream)) {
            int spriteWidth = sprite.contents().width();
            int spriteHeight = sprite.contents().height();
            int frameHeight = Math.max(1, image.getWidth() * spriteHeight / Math.max(1, spriteWidth));
            frameHeight = Math.min(frameHeight, image.getHeight());

            int originX = Math.round(sprite.getU0() * width);
            int originY = Math.round(sprite.getV0() * height);
            for (int y = 0; y < spriteHeight; y++) {
                int sourceY = y * frameHeight / spriteHeight;
                int endY = Math.max(sourceY + 1, Math.min(frameHeight, (y + 1) * frameHeight / spriteHeight));
                for (int x = 0; x < spriteWidth; x++) {
                    int sourceX = x * image.getWidth() / spriteWidth;
                    int endX = Math.max(sourceX + 1, Math.min(image.getWidth(), (x + 1) * image.getWidth() / spriteWidth));
                    int targetX = originX + x;
                    int targetY = originY + y;
                    if (targetX < 0 || targetY < 0 || targetX >= width || targetY >= height) {
                        continue;
                    }
                    int pixel = average ? blockMean(image, sourceX, sourceY, endX, endY)
                        : image.getPixel((sourceX + endX) / 2, (sourceY + endY) / 2);
                    // A raw MER map's alpha is its height map (see BedrockPackConverter.keepRawMer); 0 is kept for
                    // "no Bedrock material", so a texel of one never reads as that.
                    if (rawMer && (pixel >>> 24) == 0) {
                        pixel |= 0xFF000000;
                    }
                    target.putInt(((targetY * width) + targetX) * 4, toRgba(pixel));
                }
            }
            return true;
        } catch (IOException | RuntimeException e) {
            RadianteRenderer.LOGGER.warn("Could not read PBR map from {}", resource.sourcePackId(), e);
            return false;
        }
    }

    /** The mean of a block of texels, channel by channel. */
    private static int blockMean(NativeImage image, int x0, int y0, int x1, int y1) {
        if (x1 - x0 == 1 && y1 - y0 == 1) {
            return image.getPixel(x0, y0);
        }
        int[] sum = new int[4];
        for (int y = y0; y < y1; y++) {
            for (int x = x0; x < x1; x++) {
                int pixel = image.getPixel(x, y);
                sum[0] += (pixel >>> 24) & 0xFF;
                sum[1] += (pixel >>> 16) & 0xFF;
                sum[2] += (pixel >>> 8) & 0xFF;
                sum[3] += pixel & 0xFF;
            }
        }
        int count = (x1 - x0) * (y1 - y0);
        int half = count / 2;
        return ((sum[0] + half) / count << 24) | ((sum[1] + half) / count << 16) | ((sum[2] + half) / count << 8)
            | (sum[3] + half) / count;
    }

    /** NativeImage hands out ARGB, the atlas stores RGBA in memory order. */
    static int toRgba(int argb) {
        int alpha = (argb >>> 24) & 0xFF;
        int red = (argb >>> 16) & 0xFF;
        int green = (argb >>> 8) & 0xFF;
        int blue = argb & 0xFF;
        return (alpha << 24) | (blue << 16) | (green << 8) | red;
    }

    static int upload(int textureId, ByteBuffer level0, int width, int height, int mipLevels) {
        if (textureId < 0) {
            textureId = TextureProxy.generateTextureId();
        }
        TextureProxy.prepareImage(textureId, mipLevels, width, height, VulkanConst.toVk(GpuFormat.RGBA8_UNORM));

        ByteBuffer current = level0;
        int levelWidth = width;
        int levelHeight = height;
        for (int level = 0; level < mipLevels; level++) {
            TextureProxy.queueUpload(MemoryUtil.memAddress(current), levelWidth * levelHeight * 4, levelWidth,
                textureId, 0, 0, 0, 0, levelWidth, levelHeight, level);
            if (level + 1 >= mipLevels) {
                break;
            }

            int nextWidth = Math.max(1, levelWidth / 2);
            int nextHeight = Math.max(1, levelHeight / 2);
            ByteBuffer next = MemoryUtil.memAlloc(nextWidth * nextHeight * 4);
            // Point sampling, because averaging normals or material flags of neighbouring texels is meaningless.
            for (int y = 0; y < nextHeight; y++) {
                for (int x = 0; x < nextWidth; x++) {
                    int sourceX = Math.min(levelWidth - 1, x * 2);
                    int sourceY = Math.min(levelHeight - 1, y * 2);
                    next.putInt((y * nextWidth + x) * 4, current.getInt((sourceY * levelWidth + sourceX) * 4));
                }
            }
            if (current != level0) {
                MemoryUtil.memFree(current);
            }
            current = next;
            levelWidth = nextWidth;
            levelHeight = nextHeight;
        }
        if (current != level0) {
            MemoryUtil.memFree(current);
        }
        return textureId;
    }
}
