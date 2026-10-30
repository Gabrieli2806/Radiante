package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.pipeline.Pipeline;
import com.g2806.radiante.client.pipeline.ShaderPackLibrary;
import com.g2806.radiante.client.pipeline.ShaderPackSettings;
import com.mojang.serialization.Codec;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.IdentityHashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import net.minecraft.client.OptionInstance;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;

/**
 * The page of the shader pack in use, when it is not Radiante PT: the settings the pack declares itself (the ones
 * Radiante PT does not have), under the pack's name and icon in the sidebar. Its presets come first, then the
 * settings without a group, then a heading per group; advanced settings show only with Advanced on, settings whose
 * {@code visible_when} does not hold are left out, and the description and cost are the pack's. Radiante PT's own
 * settings stay on Radiante's pages. What the keys mean is {@link ShaderPackSettings}'s; rows are staged with the
 * advanced options ({@link AdvancedOptions#key}), so Apply builds or pushes them live like any other setting.
 */
final class PackSettingsPage {

    private static final String MODULE = Pipeline.RAY_TRACING_MODULE_NAME;
    private static final String PRESET_KEY = "options.radiante.pack.preset";
    /** The preset row's entry while the values match none of the presets; never applied. */
    private static final int CUSTOM = -1;

    /** The section, and the cost of each of its rows the pack declared. */
    record Page(SettingsLayout.Section section, Map<OptionInstance<?>, OptionImpact> impacts) {
    }

    private PackSettingsPage() {
    }

    /**
     * The page, or null when the pack in use is Radiante PT or declares nothing of its own. {@code staged} holds the
     * values changed and not applied yet, by {@link AdvancedOptions#key}; {@code relayout} lays the screen out again
     * (once the mouse is let go) after a change that shows or hides rows or picks a preset.
     */
    static Page build(Map<String, String> staged, Runnable relayout) {
        String path = Pipeline.shaderPackPath();
        if (path == null || ShaderPackLibrary.samePack(path, Pipeline.builtInShaderPackPath())) {
            return null;
        }
        ShaderPackSettings settings = Pipeline.shaderPackSettings();
        Map<String, String> values = Pipeline.moduleValues(MODULE);
        // Rows only for what the renderer actually loaded: a pack that failed to load leaves Radiante PT's values.
        List<ShaderPackSettings.Attribute> loaded = new ArrayList<>();
        for (ShaderPackSettings.Attribute attribute : settings.attributes()) {
            if (values.containsKey(attribute.name())) {
                loaded.add(attribute);
            }
        }
        if (loaded.isEmpty()) {
            return null;
        }
        java.util.function.Function<String, String> current =
            name -> staged.getOrDefault(AdvancedOptions.key(MODULE, name), values.get(name));

        Set<String> watched = new HashSet<>();
        for (ShaderPackSettings.Attribute attribute : loaded) {
            if (attribute.visibleWhen() != null) {
                watched.add(attribute.visibleWhen().attribute());
            }
        }

        Map<OptionInstance<?>, OptionImpact> impacts = new IdentityHashMap<>();
        List<OptionInstance<?>> top = new ArrayList<>();
        List<OptionInstance<?>> topAdvanced = new ArrayList<>();
        Map<String, List<OptionInstance<?>>> everyday = new LinkedHashMap<>();
        Map<String, List<OptionInstance<?>>> advanced = new LinkedHashMap<>();
        for (String group : settings.groups()) {
            everyday.put(group, new ArrayList<>());
            advanced.put(group, new ArrayList<>());
        }

        OptionInstance<Integer> presets = presetRow(settings, loaded, current, staged, relayout);
        if (presets != null) {
            top.add(presets);
            // What a preset costs depends on what it sets.
            impacts.put(presets, OptionImpact.VARIES);
        }
        for (ShaderPackSettings.Attribute attribute : loaded) {
            ShaderPackSettings.VisibleWhen when = attribute.visibleWhen();
            if (when != null && !when.test(Pipeline.getModuleAttributeType(MODULE, when.attribute()),
                current.apply(when.attribute()))) {
                continue;
            }
            String name = attribute.name();
            int at = name.indexOf(".attribute.");
            String shortName = at < 0 ? name : name.substring(at + ".attribute.".length());
            boolean shows = watched.contains(name);
            OptionInstance<?> row = AdvancedOptions.row(MODULE, name, shortName,
                Pipeline.getModuleAttributeType(MODULE, name), current.apply(name), staged, attribute.step(),
                attribute.description() == null ? null : text(attribute.description()),
                // Another row's visible_when reads this one: the page is laid out again to show or hide it.
                shows ? relayout : () -> { });
            if (row == null) {
                continue;
            }
            if (attribute.impact() != null) {
                impacts.put(row, switch (attribute.impact()) {
                    case LOW -> OptionImpact.LOW;
                    case MEDIUM -> OptionImpact.MEDIUM;
                    case HIGH -> OptionImpact.HIGH;
                });
            }
            if (attribute.group() == null) {
                (attribute.advanced() ? topAdvanced : top).add(row);
            } else {
                (attribute.advanced() ? advanced : everyday).get(attribute.group()).add(row);
            }
        }

        ShaderPackLibrary.Entry entry = entry(path, settings);
        String packKey = "pack:" + (entry != null ? entry.fileName() : path) + ":";
        List<SettingsLayout.Block> blocks = new ArrayList<>();
        for (String group : settings.groups()) {
            List<OptionInstance<?>> rows = everyday.get(group);
            List<OptionInstance<?>> more = advanced.get(group);
            if (rows.isEmpty() && more.isEmpty()) {
                continue;
            }
            blocks.add(new SettingsLayout.Block(packKey + group, text(group), null, List.copyOf(rows), null,
                List.copyOf(more), true));
        }
        List<SettingsLayout.Group> groups = topAdvanced.isEmpty() ? List.of()
            : List.of(new SettingsLayout.Group("options.radiante.pack.advanced", List.copyOf(topAdvanced)));

        Component title = entry != null ? Component.literal(entry.displayName())
            : Component.translatable(RadianteOptionsScreen.Category.PACK.key());
        AddonIcons.Icon icon = null;
        Identifier texture = entry == null ? null : ShaderPacksScreen.icon(entry);
        if (texture != null) {
            // A pack's icon is drawn square, as in the pack list; the whole texture is one unit of it.
            icon = new AddonIcons.Icon(texture, 1, 1);
        }
        return new Page(new SettingsLayout.Section(RadianteOptionsScreen.Category.PACK, List.copyOf(top), groups,
            blocks, title, icon), impacts);
    }

    /**
     * Picking a preset stages all its values; the row shows the preset the values match, or Custom (listed only
     * then) when they match none.
     */
    private static OptionInstance<Integer> presetRow(ShaderPackSettings settings,
        List<ShaderPackSettings.Attribute> loaded, java.util.function.Function<String, String> current,
        Map<String, String> staged, Runnable relayout) {
        List<ShaderPackSettings.Preset> presets = settings.presets();
        if (presets.isEmpty()) {
            return null;
        }
        Set<String> names = new HashSet<>();
        for (ShaderPackSettings.Attribute attribute : loaded) {
            names.add(attribute.name());
        }
        int matching = CUSTOM;
        for (int i = 0; i < presets.size() && matching == CUSTOM; i++) {
            boolean all = true;
            for (Map.Entry<String, String> value : presets.get(i).values().entrySet()) {
                String type = Pipeline.getModuleAttributeType(MODULE, value.getKey());
                if (!names.contains(value.getKey())
                    || !ShaderPackSettings.sameValue(type, value.getKey(), value.getValue(),
                        current.apply(value.getKey()))) {
                    all = false;
                    break;
                }
            }
            if (all) {
                matching = i;
            }
        }
        List<Integer> choices = new ArrayList<>();
        for (int i = 0; i < presets.size(); i++) {
            choices.add(i);
        }
        if (matching == CUSTOM) {
            choices.add(CUSTOM);
        }
        int shown = matching;
        Component description = Component.translatable(PRESET_KEY + ".tooltip");
        return new OptionInstance<>(PRESET_KEY, value -> {
            Component text = description;
            if (value != null && value != CUSTOM && presets.get(value).description() != null) {
                text = Component.empty().append(text(presets.get(value).description())).append("\n\n")
                    .append(description);
            }
            return net.minecraft.client.gui.components.Tooltip.create(text);
        },
            (caption, value) -> value == CUSTOM ? Component.translatable("options.radiante.pack.preset.custom")
                : text(presets.get(value).name()),
            new OptionInstance.Enum<>(choices, Codec.INT), shown, value -> {
                if (value == CUSTOM || Objects.equals(value, shown)) {
                    return;
                }
                for (Map.Entry<String, String> entry : presets.get(value).values().entrySet()) {
                    if (!names.contains(entry.getKey())) {
                        continue;
                    }
                    String type = Pipeline.getModuleAttributeType(MODULE, entry.getKey());
                    String normalized = ShaderPackSettings.normalize(type, entry.getKey(), entry.getValue());
                    if (normalized == null) {
                        com.g2806.radiante.client.RadianteClient.LOGGER.warn(
                            "Shader pack preset {}: \"{}\" is not a value of {}", presets.get(value).name(),
                            entry.getValue(), entry.getKey());
                        continue;
                    }
                    staged.put(AdvancedOptions.key(MODULE, entry.getKey()), normalized);
                }
                relayout.run();
            });
    }

    private static String entryPath;
    private static ShaderPackSettings entrySettings;
    private static ShaderPackLibrary.Entry entry;

    /**
     * The pack's name and icon, read again only when the pack changes: the page is built anew after most changes on
     * the screen, and that reads the pack's zip.
     */
    private static ShaderPackLibrary.Entry entry(String path, ShaderPackSettings settings) {
        if (!path.equals(entryPath) || settings != entrySettings) {
            entry = ShaderPackLibrary.entry(path);
            entryPath = path;
            entrySettings = settings;
        }
        return entry;
    }

    /** A pack's lang key as its text (the pack's line, or Radiante's), or the key itself as written. */
    static Component text(String key) {
        Component known = AdvancedOptions.known(key);
        return known != null ? known : Component.literal(key);
    }
}
