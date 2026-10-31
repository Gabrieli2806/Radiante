package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.option.EmissionOverrides;
import com.g2806.radiante.client.render.BlockEmissionControls;
import com.g2806.radiante.mixin.gui.OptionInstanceAccessor;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import net.minecraft.client.OptionInstance;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;

/** All emission profiles share the existing staged, searchable Advanced controls. */
final class BlockEmissionSettings {
    static final String PREFIX = "options.radiante.emission_profile.";

    private BlockEmissionSettings() {
    }

    static SettingsLayout.Group group(Map<String, Integer> staged) {
        List<String> profiles = new ArrayList<>(BlockEmissionControls.profiles());
        profiles.sort(Comparator.comparing(id -> title(id).getString()));
        List<OptionInstance<?>> rows = new ArrayList<>();
        for (String id : profiles) {
            String key = PREFIX + id;
            var row = new OptionInstance<Integer>(key,
                OptionInstance.cachedConstantTooltip(Component.translatable(PREFIX + "tooltip", id)),
                (caption, percent) -> Component.translatable("options.percent_value", caption, percent),
                new OptionInstance.IntRange(0, EmissionOverrides.MAX, false),
                EmissionOverrides.percent(staged, id), value -> {
                    if (value == EmissionOverrides.DEFAULT) staged.remove(id);
                    else staged.put(id, value);
                });
            // A unique key retains focus when captions share a block name; the fallback follows Minecraft's language.
            ((OptionInstanceAccessor) (Object) row).radiante$setCaption(
                Component.translatableWithFallback(key, title(id).getString()));
            rows.add(row);
        }
        return new SettingsLayout.Group("options.radiante.advanced.group.block_emission", rows);
    }

    private static Component title(String name) {
        Identifier sprite = Identifier.tryParse(name);
        if (sprite == null) return Component.literal(name);
        String path = sprite.getPath().replaceFirst("^block/", "");
        String blockPath = switch (path) {
            case "magma" -> "magma_block";
            case "pumpkin_face_on" -> "jack_o_lantern";
            default -> path.startsWith("redstone_dust") ? "redstone_wire" : path;
        };
        while (!blockPath.isEmpty()) {
            Identifier blockId = Identifier.fromNamespaceAndPath(sprite.getNamespace(), blockPath);
            if (BuiltInRegistries.BLOCK.containsKey(blockId)) {
                var block = BuiltInRegistries.BLOCK.getValue(blockId);
                Component title = block.getName();
                return path.equals(blockPath) ? title :
                    Component.translatable(PREFIX + "variant", title, path);
            }
            int split = blockPath.lastIndexOf('_');
            if (split < 0) break;
            blockPath = blockPath.substring(0, split);
        }
        return Component.translatable(PREFIX + "texture", name);
    }
}
