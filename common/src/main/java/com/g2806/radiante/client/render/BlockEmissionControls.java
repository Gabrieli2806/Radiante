package com.g2806.radiante.client.render;

import com.g2806.radiante.client.option.EmissionOverrides;
import com.g2806.radiante.client.option.EmissionColor;
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

/** Resolves the saved HDR emission colours while geometry is collected, rather than on every traced ray. */
public final class BlockEmissionControls {
    private static volatile Map<String, EmissionColor> values = Map.of();
    private static volatile Map<String, EmissionColor> packColours = Map.of();
    private static volatile String loaded = null;
    private static volatile Set<String> activeSprites = Set.of();
    private static volatile Rect[] rectangles = new Rect[0];
    private static volatile int atlasId = -1;

    private record Rect(float u0, float v0, float u1, float v1, EmissionColor color) {
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

    public static Map<String, EmissionColor> values() {
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

    public static EmissionColor color(Identifier sprite) {
        return EmissionOverrides.color(packColours, sprite.toString()).multiply(EmissionOverrides.color(values(), sprite.toString()));
    }

    public static Set<String> profiles() {
        Set<String> profiles = new TreeSet<>(BUILT_IN.keySet());
        // Packs may make otherwise dark vines emissive without changing their vanilla light level.
        for (String vine : new String[] {"vine", "cave_vines", "cave_vines_plant", "twisting_vines",
            "twisting_vines_plant", "weeping_vines", "weeping_vines_plant", "glow_lichen"}) {
            profiles.add("minecraft:block/" + vine);
        }
        profiles.addAll(activeSprites);
        profiles.addAll(packColours.keySet());
        profiles.addAll(values().keySet());
        return profiles;
    }

    static void atlas(net.minecraft.client.renderer.texture.TextureAtlas atlas, int textureId,
        Set<net.minecraft.client.renderer.texture.TextureAtlasSprite> emitters) {
        var sprites = ((com.g2806.radiante.mixin.texture.TextureAtlasAccessor) (Object) atlas).radiante$texturesByName();
        Set<String> active = new TreeSet<>();
        for (var sprite : emitters) active.add(sprite.contents().name().toString());
        active.addAll(EmissionTiles.authoredEmissionProfiles());
        activeSprites = Set.copyOf(active);
        Map<String, EmissionColor> authored = new HashMap<>();
        for (var resource : net.minecraft.client.Minecraft.getInstance().getResourceManager().getResourceStack(
            Identifier.fromNamespaceAndPath("radiante", "emission_tints.json"))) {
            try (var reader = resource.openAsReader()) {
                authored.putAll(EmissionOverrides.parse(JsonParser.parseReader(reader).toString()));
            } catch (Exception invalid) {
                RadianteRenderer.LOGGER.warn("Cannot read emission tints from {}", resource.sourcePackId(), invalid);
            }
        }
        packColours = Map.copyOf(authored);
        var rects = new ArrayList<Rect>();
        Set<String> changed = new TreeSet<>(packColours.keySet());
        changed.addAll(values().keySet());
        changed.forEach(name -> {
            var percent = color(Identifier.parse(name));
            var sprite = sprites.get(Identifier.tryParse(name));
            if (sprite != null) rects.add(new Rect(sprite.getU0(), sprite.getV0(), sprite.getU1(), sprite.getV1(), percent));
        });
        rectangles = rects.toArray(Rect[]::new);
        atlasId = textureId;
    }

    /** Fluids and custom quads have no sprite argument; their UV centre identifies their atlas sprite. */
    static boolean hasTints() { return !values().isEmpty() || !packColours.isEmpty(); }

    static EmissionColor colorAt(int textureId, float u, float v) {
        if (textureId != atlasId) return EmissionColor.WHITE;
        for (Rect rect : rectangles) {
            if (u >= rect.u0 && u < rect.u1 && v >= rect.v0 && v < rect.v1) return rect.color;
        }
        return EmissionColor.WHITE;
    }

    /** A point source represents the combined emission of the item's texture variants. */
    static EmissionColor forBlock(Block block) {
        var id = BuiltInRegistries.BLOCK.getKey(block);
        return forBlock(id.getNamespace(), id.getPath());
    }

    static EmissionColor forBlock(String namespace, String path) {
        if (values().isEmpty()) return EmissionColor.WHITE;
        path = path.replace("wall_torch", "torch");
        if (path.startsWith("waxed_")) path = path.substring(6);
        if (path.equals("magma_block")) path = "magma";
        String base = namespace + ":block/" + path;
        if (BUILT_IN.containsKey(base) && values().containsKey(base)) return values().get(base);
        float total = 0.0f, red = 0.0f, green = 0.0f, blue = 0.0f;
        for (var entry : BUILT_IN.entrySet()) {
            if (entry.getKey().equals(base) || entry.getKey().startsWith(base + "_")) {
                total += entry.getValue();
                var colour = EmissionOverrides.color(values(), entry.getKey());
                red += entry.getValue() * colour.red();
                green += entry.getValue() * colour.green();
                blue += entry.getValue() * colour.blue();
            }
        }
        return total > 0.0f ? new EmissionColor(red / total, green / total, blue / total) : EmissionOverrides.color(values(), base);
    }

    public static void apply(Map<String, EmissionColor> changes) {
        String text = EmissionOverrides.serialize(changes);
        if (text.equals(Options.blockEmissionOverrides)) return;
        Options.blockEmissionOverrides = text;
        values();
        BlockEntityCache.invalidate();
        EmissionTiles.invalidate();
        ChunkManager.markAllDirty();
    }
}
