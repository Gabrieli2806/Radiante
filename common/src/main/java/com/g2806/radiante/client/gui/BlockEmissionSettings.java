package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.option.EmissionOverrides;
import com.g2806.radiante.client.option.EmissionColor;
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

    static SettingsLayout.Group group(Map<String, EmissionColor> staged) {
        List<String> profiles = new ArrayList<>(BlockEmissionControls.profiles());
        profiles.sort(Comparator.comparing(id -> title(id).getString()));
        List<OptionInstance<?>> rows = new ArrayList<>();
        for (String id : profiles) {
            String key = PREFIX + id;
            var row = new OptionInstance<EmissionColor>(key,
                OptionInstance.cachedConstantTooltip(Component.translatable(PREFIX + "tooltip", id)),
                (caption, colour) -> Component.empty().append(caption).append(": " + colour.display()),
                new ColourValues(), EmissionOverrides.color(staged, id), value -> {
                    if (value.equals(EmissionColor.WHITE)) staged.remove(id);
                    else staged.put(id, value);
                });
            // A unique key retains focus when captions share a block name; the fallback follows Minecraft's language.
            ((OptionInstanceAccessor) (Object) row).radiante$setCaption(
                Component.translatableWithFallback(key, title(id).getString()));
            rows.add(row);
        }
        return new SettingsLayout.Group("options.radiante.advanced.group.block_emission", rows);
    }

    @SuppressWarnings("unchecked")
    static void open(OptionInstance<?> option) {
        var minecraft = net.minecraft.client.Minecraft.getInstance();
        var colour = (OptionInstance<EmissionColor>) option;
        minecraft.gui.setScreen(new HdrEmissionScreen(minecraft.gui.screen(),
            ((OptionInstanceAccessor) (Object) option).radiante$caption(), colour.get(), colour::set));
    }

    private static final class ColourValues implements OptionInstance.ValueSet<EmissionColor> {
        @Override
        public java.util.Optional<EmissionColor> validateValue(EmissionColor value) {
            return java.util.Optional.of(value);
        }

        @Override
        public com.mojang.serialization.Codec<EmissionColor> codec() {
            return com.mojang.serialization.Codec.FLOAT.listOf().comapFlatMap(rgb -> rgb.size() == 3
                ? com.mojang.serialization.DataResult.success(new EmissionColor(rgb.get(0), rgb.get(1), rgb.get(2)))
                : com.mojang.serialization.DataResult.error(() -> "Expected three RGB channels"),
                colour -> List.of(colour.red(), colour.green(), colour.blue()));
        }

        @Override
        public java.util.function.Function<OptionInstance<EmissionColor>, net.minecraft.client.gui.components.AbstractWidget>
            createButton(OptionInstance.TooltipSupplier<EmissionColor> tooltip, net.minecraft.client.Options options,
                int x, int y, int width, OptionInstance.ValueUpdateListener<? super EmissionColor> changed) {
            return option -> net.minecraft.client.gui.components.Button.builder(Component.literal(option.get().display()),
                button -> open(option)).bounds(x, y, width, 20).build();
        }
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
