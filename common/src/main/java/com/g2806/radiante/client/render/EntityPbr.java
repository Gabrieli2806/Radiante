package com.g2806.radiante.client.render;

import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.compat.VersionCompat;
import com.g2806.radiante.mixin.texture.TextureAtlasAccessor;
import com.mojang.blaze3d.platform.NativeImage;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.texture.TextureAtlas;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.data.AtlasIds;
import net.minecraft.resources.Identifier;
import net.minecraft.server.packs.resources.Resource;
import net.minecraft.server.packs.resources.ResourceManager;
import org.lwjgl.system.MemoryUtil;

/**
 * LabPBR maps for everything that is not a block: entities, block entities and held items. A pack ships them the
 * same way as for blocks, "_s" and "_n" next to the texture ({@code textures/entity/minecart_s.png}), and the ray
 * tracer finds them through the same texture mapping, keyed by the renderer id of the texture the geometry samples.
 *
 * <p>Two kinds of textures are covered. Standalone ones (a minecart, most mobs) are picked up the first time a
 * render type samples them, and their maps are uploaded whole. Atlases (chests, shulker boxes, decorated pots,
 * banner and shield patterns, items) get maps stitched sprite by sprite, like the block atlas. Everything is thrown
 * away and built again after a resource reload.
 */
public final class EntityPbr {

    /** Renderer ids of one texture's maps; -1 where the pack has none. */
    private record Maps(int albedoId, int specular, int normal) {
    }

    private static final Maps NONE = new Maps(0, -1, -1);

    /** Atlases whose sprites are drawn as entity geometry. The block atlas has its own maps (PbrAtlases). */
    private static final List<Identifier> ATLASES = List.of(AtlasIds.CHESTS, AtlasIds.SHULKER_BOXES,
        AtlasIds.DECORATED_POT, AtlasIds.BANNER_PATTERNS, AtlasIds.SHIELD_PATTERNS, AtlasIds.ITEMS, AtlasIds.PARTICLES);

    /** Written on the render thread, read from whichever thread collects entities. */
    private static final Map<Identifier, Maps> STANDALONE = new ConcurrentHashMap<>();
    private static final Set<Identifier> PENDING = ConcurrentHashMap.newKeySet();
    private static final Map<Identifier, Maps> ATLAS_MAPS = new HashMap<>();
    /** Atlases still to look at: one whose image the renderer has not mirrored yet is tried again next frame. */
    private static final Set<Identifier> ATLASES_PENDING = new java.util.HashSet<>();
    /** GPU images are kept per map across reloads and filled again, since the renderer has no way to free one. */
    private static final Map<Identifier, Integer> MAP_TEXTURE_IDS = new HashMap<>();
    private static Object builtFor;
    private static String emissiveSuffix = "_e";

    private EntityPbr() {
    }

    /** Called for every texture a render type samples; queues the ones not looked at since the last reload. */
    public static void note(Identifier texture, int albedoId) {
        if (texture == null || albedoId <= 0) {
            return;
        }
        Maps known = STANDALONE.get(texture);
        if (known == null || (known != NONE && known.albedoId() != albedoId)) {
            PENDING.add(texture);
        }
    }

    /** Loads what was queued, and rebuilds after a resource reload. Render thread, before the mapping is uploaded. */
    public static void update(Minecraft minecraft) {
        TextureAtlas blocks = minecraft.getAtlasManager().getAtlasOrThrow(AtlasIds.BLOCKS);
        Object key = TextureTracker.gpuTextureOrNull(blocks);
        if (key == null) {
            return;
        }
        ResourceManager resources = minecraft.getResourceManager();
        if (key != builtFor) {
            builtFor = key;
            emissiveSuffix = com.g2806.radiante.client.compat.emfetf.EmfEtfCompat.readEmissiveSuffix(resources);
            STANDALONE.clear();
            ATLAS_MAPS.clear();
            ATLASES_PENDING.clear();
            ATLASES_PENDING.addAll(ATLASES);
            BlockEntityCache.invalidate();
        }
        if (!ATLASES_PENDING.isEmpty()) {
            ATLASES_PENDING.removeIf(atlasId -> buildAtlas(minecraft, resources, atlasId));
        }
        if (PENDING.isEmpty()) {
            return;
        }
        boolean bakeEmissive = !com.g2806.radiante.client.compat.emfetf.EmfEtfCompat.isEtfInstalled();
        for (Identifier texture : List.copyOf(PENDING)) {
            PENDING.remove(texture);
            int albedoId = TextureTracker.idOf(texture);
            if (albedoId <= 0) {
                continue;
            }
            int specular = loadStandaloneSpecular(resources, texture, bakeEmissive ? emissiveSuffix : null);
            int normal = loadWhole(resources, texture, "_n");
            STANDALONE.put(texture, specular < 0 && normal < 0 ? NONE : new Maps(albedoId, specular, normal));
        }
    }

    /** Adds the maps to the texture mapping: four ints per albedo id (specular, normal, flags, raw MER). */
    public static void writeMapping(ByteBuffer mapping, int entries) {
        for (Maps maps : STANDALONE.values()) {
            put(mapping, entries, maps);
        }
        for (Maps maps : ATLAS_MAPS.values()) {
            put(mapping, entries, maps);
        }
    }

    private static void put(ByteBuffer mapping, int entries, Maps maps) {
        if (maps == NONE || maps.albedoId() <= 0 || maps.albedoId() >= entries) {
            return;
        }
        int entry = maps.albedoId() * 4 * Integer.BYTES;
        if (maps.specular() >= 0) {
            mapping.putInt(entry, maps.specular());
        }
        if (maps.normal() >= 0) {
            mapping.putInt(entry + Integer.BYTES, maps.normal());
        }
    }

    /** A standalone texture's specular map, folding in OptiFine/CEM {@code _e.png} emission when ETF is absent. */
    private static int loadStandaloneSpecular(ResourceManager resources, Identifier texture, String emSuffix) {
        Identifier specId = mapOf(texture, "_s");
        Optional<Resource> specRes = resources.getResource(specId);
        Optional<Resource> emRes = emSuffix == null ? Optional.empty() : resources.getResource(mapOf(texture, emSuffix));
        if (specRes.isEmpty() && emRes.isEmpty()) {
            return -1;
        }
        if (emRes.isEmpty()) {
            return loadWhole(resources, texture, "_s");
        }
        NativeImage specImage = null;
        NativeImage emImage = null;
        try {
            if (specRes.isPresent()) {
                try (InputStream in = specRes.get().open()) {
                    specImage = NativeImage.read(in);
                }
            }
            try (InputStream in = emRes.get().open()) {
                emImage = NativeImage.read(in);
            }
            int width = specImage != null ? Math.max(specImage.getWidth(), emImage.getWidth()) : emImage.getWidth();
            int height = specImage != null ? Math.max(specImage.getHeight(), emImage.getHeight()) : emImage.getHeight();
            ByteBuffer pixels = MemoryUtil.memAlloc(width * height * 4);
            try {
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        int baseRgba = 0;
                        if (specImage != null) {
                            int sx = x * specImage.getWidth() / width;
                            int sy = y * specImage.getHeight() / height;
                            baseRgba = PbrAtlases.toRgba(specImage.getPixel(sx, sy));
                        }
                        int ex = x * emImage.getWidth() / width;
                        int ey = y * emImage.getHeight() / height;
                        int combined = com.g2806.radiante.client.compat.emfetf.EmfEtfCompat
                            .applyEmissiveToLabPbrRgba(baseRgba, emImage.getPixel(ex, ey));
                        pixels.putInt((y * width + x) * 4, combined);
                    }
                }
                return PbrAtlases.upload(textureIdFor(specId), pixels, width, height, 1, 0, PbrAtlases.MIPS_POINT);
            } finally {
                MemoryUtil.memFree(pixels);
            }
        } catch (Exception e) {
            RadianteRenderer.LOGGER.warn("Could not read PBR/emissive map for {}", texture, e);
            return -1;
        } finally {
            if (specImage != null) {
                specImage.close();
            }
            if (emImage != null) {
                emImage.close();
            }
        }
    }

    /** A standalone texture's map, uploaded as it is; -1 when the pack has none. */
    private static int loadWhole(ResourceManager resources, Identifier texture, String suffix) {
        Identifier mapId = mapOf(texture, suffix);
        Optional<Resource> resource = resources.getResource(mapId);
        if (resource.isEmpty()) {
            return -1;
        }
        try (InputStream stream = resource.get().open(); NativeImage image = NativeImage.read(stream)) {
            int width = image.getWidth();
            int height = image.getHeight();
            ByteBuffer pixels = MemoryUtil.memAlloc(width * height * 4);
            try {
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        pixels.putInt((y * width + x) * 4, PbrAtlases.toRgba(image.getPixel(x, y)));
                    }
                }
                return PbrAtlases.upload(textureIdFor(mapId), pixels, width, height, 1, 0, PbrAtlases.MIPS_POINT);
            } finally {
                MemoryUtil.memFree(pixels);
            }
        } catch (Exception e) {
            RadianteRenderer.LOGGER.warn("Could not read PBR map {}", mapId, e);
            return -1;
        }
    }

    /**
     * Stitches the maps of an atlas's sprites, if the pack has any for them. False while the atlas is not ready to be
     * looked at yet, so it is tried again.
     */
    private static boolean buildAtlas(Minecraft minecraft, ResourceManager resources, Identifier atlasId) {
        TextureAtlas atlas;
        try {
            atlas = minecraft.getAtlasManager().getAtlasOrThrow(atlasId);
        } catch (RuntimeException missing) {
            return true;
        }
        int albedoId = TextureTracker.idOf(TextureTracker.gpuTextureOrNull(atlas));
        Map<Identifier, TextureAtlasSprite> sprites = ((TextureAtlasAccessor) (Object) atlas).radiante$texturesByName();
        if (albedoId <= 0 || sprites.isEmpty()) {
            return false;
        }
        // Most packs cover blocks only; an atlas nothing is authored for costs no memory at all.
        boolean bakeEmissive = !com.g2806.radiante.client.compat.emfetf.EmfEtfCompat.isEtfInstalled();
        boolean anySpecular = false;
        boolean anyEmissive = false;
        boolean anyNormal = false;
        for (Identifier spriteId : sprites.keySet()) {
            if (PbrAtlases.isPackMap(sprites, spriteId, emissiveSuffix)) {
                continue;
            }
            anySpecular |= resources.getResource(spriteMapOf(spriteId, "_s")).isPresent();
            if (bakeEmissive) {
                anyEmissive |= resources.getResource(spriteMapOf(spriteId, emissiveSuffix)).isPresent();
            }
            anyNormal |= resources.getResource(spriteMapOf(spriteId, "_n")).isPresent();
        }
        if (!anySpecular && !anyEmissive && !anyNormal) {
            return true;
        }

        int width = VersionCompat.gpuTexture(atlas).getWidth(0);
        int height = VersionCompat.gpuTexture(atlas).getHeight(0);
        int mipLevels = VersionCompat.gpuTexture(atlas).getMipLevels();
        int specular = -1;
        int normal = -1;
        if (anySpecular || anyEmissive) {
            specular = stitchSpecularAtlas(resources, sprites, atlasId, anySpecular, anyEmissive ? emissiveSuffix : null,
                width, height, mipLevels, albedoId);
        }
        if (anyNormal) {
            normal = stitchAtlas(resources, sprites, atlasId, "_n", PbrAtlases.NORMAL_DEFAULT, width, height,
                mipLevels, albedoId);
        }
        ATLAS_MAPS.put(atlasId, new Maps(albedoId, specular, normal));
        if (Options.debugLogging) {
            RadianteRenderer.LOGGER.info("Stitched PBR maps for the {} atlas", atlasId);
        }
        return true;
    }

    private static int stitchSpecularAtlas(ResourceManager resources, Map<Identifier, TextureAtlasSprite> sprites,
        Identifier atlasId, boolean anySpecular, String emSuffix, int width, int height, int mipLevels, int albedoId) {
        ByteBuffer target = PbrAtlases.fill(width, height, 0);
        try {
            for (Map.Entry<Identifier, TextureAtlasSprite> entry : sprites.entrySet()) {
                if (PbrAtlases.isPackMap(sprites, entry.getKey(), emissiveSuffix)) {
                    continue;
                }
                if (anySpecular) {
                    PbrAtlases.stitch(resources, entry.getKey(), entry.getValue(), "_s", target, width, height);
                }
                if (emSuffix != null) {
                    stitchSpriteEmissive(resources, entry.getKey(), entry.getValue(), emSuffix, target, width, height);
                }
            }
            Identifier mapId = Identifier.fromNamespaceAndPath(atlasId.getNamespace(),
                "radiante_atlas_" + atlasId.getPath() + "_s");
            return PbrAtlases.upload(textureIdFor(mapId), target, width, height, mipLevels,
                TracedTextureDetail.droppedFor(albedoId), PbrAtlases.MIPS_SPECULAR);
        } finally {
            MemoryUtil.memFree(target);
        }
    }

    private static void stitchSpriteEmissive(ResourceManager resources, Identifier spriteId, TextureAtlasSprite sprite,
        String emSuffix, ByteBuffer target, int width, int height) {
        Optional<Resource> res = resources.getResource(spriteMapOf(spriteId, emSuffix));
        if (res.isEmpty()) {
            return;
        }
        try (InputStream stream = res.get().open(); NativeImage image = NativeImage.read(stream)) {
            int spriteWidth = sprite.contents().width();
            int spriteHeight = sprite.contents().height();
            int frameHeight = Math.min(image.getHeight(),
                Math.max(1, image.getWidth() * spriteHeight / Math.max(1, spriteWidth)));
            int originX = Math.round(sprite.getU0() * width);
            int originY = Math.round(sprite.getV0() * height);
            for (int y = 0; y < spriteHeight; y++) {
                int sy = y * frameHeight / spriteHeight;
                for (int x = 0; x < spriteWidth; x++) {
                    int sx = x * image.getWidth() / spriteWidth;
                    int tx = originX + x;
                    int ty = originY + y;
                    if (tx < 0 || ty < 0 || tx >= width || ty >= height) {
                        continue;
                    }
                    int idx = (ty * width + tx) * 4;
                    int existing = target.getInt(idx);
                    target.putInt(idx, com.g2806.radiante.client.compat.emfetf.EmfEtfCompat
                        .applyEmissiveToLabPbrRgba(existing, image.getPixel(sx, sy)));
                }
            }
        } catch (Exception e) {
            RadianteRenderer.LOGGER.warn("Could not read emissive sprite map for {}", spriteId, e);
        }
    }

    private static int stitchAtlas(ResourceManager resources, Map<Identifier, TextureAtlasSprite> sprites,
        Identifier atlasId, String suffix, int neutral, int width, int height, int mipLevels, int albedoId) {
        ByteBuffer target = PbrAtlases.fill(width, height, neutral);
        try {
            for (Map.Entry<Identifier, TextureAtlasSprite> entry : sprites.entrySet()) {
                if (!PbrAtlases.isPackMap(sprites, entry.getKey())) {
                    PbrAtlases.stitch(resources, entry.getKey(), entry.getValue(), suffix, target, width, height);
                }
            }
            Identifier mapId = Identifier.fromNamespaceAndPath(atlasId.getNamespace(),
                "radiante_atlas_" + atlasId.getPath() + suffix);
            // At the colours' traced detail (TracedTextureDetail).
            return PbrAtlases.upload(textureIdFor(mapId), target, width, height, mipLevels,
                TracedTextureDetail.droppedFor(albedoId),
                suffix.equals("_n") ? PbrAtlases.MIPS_NORMAL : PbrAtlases.MIPS_SPECULAR);
        } finally {
            MemoryUtil.memFree(target);
        }
    }

    private static int textureIdFor(Identifier mapId) {
        return MAP_TEXTURE_IDS.computeIfAbsent(mapId,
            id -> com.g2806.radiante.client.proxy.vulkan.TextureProxy.generateTextureId());
    }

    /** "textures/entity/minecart.png" to "textures/entity/minecart_s.png". */
    private static Identifier mapOf(Identifier texture, String suffix) {
        String path = texture.getPath();
        String base = path.endsWith(".png") ? path.substring(0, path.length() - 4) : path;
        return Identifier.fromNamespaceAndPath(texture.getNamespace(), base + suffix + ".png");
    }

    /** A sprite "entity/chest/normal" has its map at "textures/entity/chest/normal_s.png", as PbrAtlases reads it. */
    private static Identifier spriteMapOf(Identifier spriteId, String suffix) {
        return Identifier.fromNamespaceAndPath(spriteId.getNamespace(),
            "textures/" + spriteId.getPath() + suffix + ".png");
    }
}
