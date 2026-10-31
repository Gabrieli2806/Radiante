package com.g2806.radiante.client.render;

import com.g2806.radiante.client.option.EmissionOverrides;
import com.g2806.radiante.client.option.Options;
import com.google.gson.JsonParser;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.Identifier;
import net.minecraft.world.level.block.Block;

/** Resolves the saved emission percentages while geometry is collected, rather than on every traced ray. */
public final class BlockEmissionControls {
    private static volatile Map<String, Integer> values = Map.of();
    private static volatile String loaded = null;
    private static volatile Set<String> activeSprites = Set.of();
    private static volatile Rect[] rectangles = new Rect[0];
    private static volatile int atlasId = -1;

    private record Rect(float u0, float v0, float u1, float v1, int percent) {
    }

    private static final Map<String, Float> BUILT_IN = builtIn();

    private BlockEmissionControls() {
    }

    private static Map<String, Float> builtIn() {
        Map<String, Float> entries = new HashMap<>();
        try (var stream = BlockEmissionControls.class.getResourceAsStream("/assets/radiante/bedrock_defaults/emission.json")) {
            if (stream != null) {
                try (var reader = new InputStreamReader(stream, StandardCharsets.UTF_8)) {
                    JsonParser.parseReader(reader).getAsJsonObject().getAsJsonObject("sprites").entrySet()
                        .forEach(entry -> entries.put(entry.getKey(), entry.getValue().getAsJsonArray().get(0).getAsFloat()));
                }
            }
        } catch (Exception e) {
            throw new IllegalStateException("Cannot read built-in block emission profiles", e);
        }
        return Map.copyOf(entries);
    }

    public static Map<String, Integer> values() {
        String text = Options.blockEmissionOverrides;
        if (!text.equals(loaded)) {
            synchronized (BlockEmissionControls.class) {
                if (!text.equals(loaded)) {
                    values = EmissionOverrides.parse(text);
                    loaded = text;
                }
            }
        }
        return values;
    }

    public static int percent(Identifier sprite) {
        return EmissionOverrides.percent(values(), sprite.toString());
    }

    public static Set<String> profiles() {
        Set<String> profiles = new TreeSet<>(BUILT_IN.keySet());
        // Packs may make otherwise dark vines emissive without changing their vanilla light level.
        for (String vine : new String[] {"vine", "cave_vines", "cave_vines_plant", "twisting_vines",
            "twisting_vines_plant", "weeping_vines", "weeping_vines_plant", "glow_lichen"}) {
            profiles.add("minecraft:block/" + vine);
        }
        profiles.addAll(activeSprites);
        profiles.addAll(values().keySet());
        return profiles;
    }

    static void atlas(net.minecraft.client.renderer.texture.TextureAtlas atlas, int textureId,
        Set<net.minecraft.client.renderer.texture.TextureAtlasSprite> emitters) {
        var sprites = ((com.g2806.radiante.mixin.texture.TextureAtlasAccessor) (Object) atlas).radiante$texturesByName();
        Set<String> active = new TreeSet<>();
        for (var sprite : emitters) active.add(sprite.contents().name().toString());
        activeSprites = Set.copyOf(active);
        var rects = new ArrayList<Rect>();
        values().forEach((name, percent) -> {
            var sprite = sprites.get(Identifier.tryParse(name));
            if (sprite != null) rects.add(new Rect(sprite.getU0(), sprite.getV0(), sprite.getU1(), sprite.getV1(), percent));
        });
        rectangles = rects.toArray(Rect[]::new);
        atlasId = textureId;
    }

    /** Fluids and custom quads have no sprite argument; their UV centre identifies their atlas sprite. */
    static int percentAt(int textureId, float u, float v) {
        if (textureId != atlasId) return EmissionOverrides.DEFAULT;
        for (Rect rect : rectangles) {
            if (u >= rect.u0 && u < rect.u1 && v >= rect.v0 && v < rect.v1) return rect.percent;
        }
        return EmissionOverrides.DEFAULT;
    }

    /** A point source represents the combined emission of the item's texture variants. */
    static float forBlock(Block block) {
        var id = BuiltInRegistries.BLOCK.getKey(block);
        return forBlock(id.getNamespace(), id.getPath());
    }

    static float forBlock(String namespace, String path) {
        if (values().isEmpty()) return 1.0f;
        path = path.replace("wall_torch", "torch");
        if (path.startsWith("waxed_")) path = path.substring(6);
        if (path.equals("magma_block")) path = "magma";
        String base = namespace + ":block/" + path;
        if (BUILT_IN.containsKey(base) && values().containsKey(base)) return values().get(base) / 100.0f;
        float total = 0.0f, scaled = 0.0f;
        for (var entry : BUILT_IN.entrySet()) {
            if (entry.getKey().equals(base) || entry.getKey().startsWith(base + "_")) {
                total += entry.getValue();
                scaled += entry.getValue() * EmissionOverrides.percent(values(), entry.getKey()) / 100.0f;
            }
        }
        return total > 0.0f ? scaled / total : EmissionOverrides.percent(values(), base) / 100.0f;
    }

    public static void apply(Map<String, Integer> changes) {
        String text = EmissionOverrides.serialize(changes);
        if (text.equals(Options.blockEmissionOverrides)) return;
        Options.blockEmissionOverrides = text;
        values();
        BlockEntityCache.invalidate();
        EmissionTiles.invalidate();
        ChunkManager.markAllDirty();
    }
}
