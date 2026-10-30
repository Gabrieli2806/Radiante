package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.pipeline.Pipeline;
import com.mojang.serialization.Codec;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.math.BigDecimal;
import java.math.MathContext;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import net.minecraft.client.OptionInstance;
import net.minecraft.locale.Language;
import net.minecraft.network.chat.Component;

/**
 * The Advanced part of the settings: every attribute of the render pipeline that has no row of its own, made into
 * rows from what the pipeline declares (its type and range), grouped by what it is about. Nothing here is listed by
 * hand, so a setting a shader pack adds shows up by itself.
 */
final class AdvancedOptions {

    /** The modules whose attributes are offered, in the order their groups appear. */
    static final String[] MODULES = {
        Pipeline.RAY_TRACING_MODULE_NAME,
        Pipeline.TONE_MAPPING_MODULE_NAME,
        Pipeline.POST_RENDER_MODULE_NAME,
        "render_pipeline.module.nrd.name",
        Pipeline.FSR_MODULE_NAME,
        Pipeline.XESS_MODULE_NAME};

    /** Steps of a slider for a fractional setting; a row's track is under a hundred pixels wide. */
    private static final int FLOAT_STEPS = 200;
    private static final Pattern RANGE = Pattern.compile("(-?[0-9.]+)-(-?[0-9.]+)");

    private AdvancedOptions() {
    }

    private static Set<String> covered;

    /**
     * Attributes the settings already have a row for: the ones {@link Tunable} and {@link Pipeline} name, found by
     * looking rather than listed, so a new row elsewhere takes its attribute out of Advanced by itself.
     */
    private static Set<String> covered() {
        if (covered == null) {
            Set<String> names = new HashSet<>();
            for (Tunable tunable : Tunable.ALL) {
                names.add(tunable.attribute);
                if (tunable.twin != null) {
                    names.add(tunable.twin);
                }
            }
            for (Field field : Pipeline.class.getDeclaredFields()) {
                if (Modifier.isStatic(field.getModifiers()) && field.getType() == String.class) {
                    try {
                        field.setAccessible(true);
                        if (field.get(null) instanceof String value && value.contains(".attribute.")) {
                            names.add(value);
                        }
                    } catch (ReflectiveOperationException | RuntimeException ignored) {
                        // Not reachable: the attribute simply stays in Advanced.
                    }
                }
            }
            names.add("render_pipeline.module.tone_mapping.attribute.method");
            covered = names;
        }
        return covered;
    }

    /** The key in the staged values: the module and the attribute. */
    static String key(String module, String attribute) {
        return module + "\n" + attribute;
    }

    /**
     * The Advanced groups of a section. {@code staged} holds values changed and not applied yet, by {@link #key};
     * rows read from it first and write to it.
     */
    static List<SettingsLayout.Group> groups(RadianteOptionsScreen.Category category, Map<String, String> staged) {
        Map<String, List<OptionInstance<?>>> byGroup = new LinkedHashMap<>();
        com.g2806.radiante.client.pipeline.ShaderPackSettings packOwn = Pipeline.shaderPackSettings();
        for (String module : MODULES) {
            String prefix = module.substring(0, module.length() - "name".length()) + "attribute.";
            for (Map.Entry<String, String> attribute : Pipeline.moduleValues(module).entrySet()) {
                String name = attribute.getKey();
                if (!name.startsWith(prefix) || covered().contains(name)
                    || (module.equals(Pipeline.RAY_TRACING_MODULE_NAME) && packOwn.isOwn(name))) {
                    // A shader pack's own settings are on the pack's page (PackSettingsPage).
                    continue;
                }
                String shortName = name.substring(prefix.length());
                String[] place = place(module, shortName);
                if (place == null || !place[0].equals(category.name())) {
                    continue;
                }
                String type = Pipeline.getModuleAttributeType(module, name);
                String value = staged.getOrDefault(key(module, name), attribute.getValue());
                OptionInstance<?> row = row(module, name, shortName, type, value, staged);
                if (row != null) {
                    byGroup.computeIfAbsent(place[1], k -> new ArrayList<>()).add(row);
                }
            }
        }
        List<SettingsLayout.Group> groups = new ArrayList<>();
        byGroup.forEach((group, rows) -> groups.add(new SettingsLayout.Group(
            "options.radiante.advanced.group." + group, List.copyOf(rows))));
        return groups;
    }

    /** The section and group of an attribute, or null for one that is not a setting (a path, an on/off of a module). */
    private static String[] place(String module, String name) {
        if (name.equals("enable") || name.equals("render_scale") || name.contains("path")) {
            return null;
        }
        if (module.equals(Pipeline.TONE_MAPPING_MODULE_NAME)) {
            return new String[] {"IMAGE", "exposure"};
        }
        if (module.equals(Pipeline.POST_RENDER_MODULE_NAME)) {
            return new String[] {"SKY_AND_WATER", "stars"};
        }
        if (module.equals("render_pipeline.module.nrd.name")) {
            return new String[] {"PERFORMANCE", "denoiser"};
        }
        if (module.equals(Pipeline.FSR_MODULE_NAME) || module.equals(Pipeline.XESS_MODULE_NAME)) {
            return new String[] {"QUALITY", "upscaler"};
        }
        if (name.contains("debug")) {
            return new String[] {"DEBUG", "debug"};
        }
        if (name.contains("cloud")) {
            return new String[] {"SKY_AND_WATER", "clouds"};
        }
        if (name.contains("water")) {
            return new String[] {"SKY_AND_WATER", "water"};
        }
        if (name.contains("fog") || name.contains("volumetric")) {
            return new String[] {"SKY_AND_WATER", "fog"};
        }
        if (name.contains("atmosphere") || name.contains("rayleigh") || name.contains("mie_") || name.contains("sky")
            || name.startsWith("sun_") || name.startsWith("moon_") || name.contains("view_cosine")) {
            return new String[] {"SKY_AND_WATER", "atmosphere"};
        }
        if (name.startsWith("post_")) {
            return new String[] {"IMAGE", "camera"};
        }
        if (name.contains("light") || name.contains("radiance") || name.contains("emission")) {
            return new String[] {"LIGHTING", "light"};
        }
        if (name.contains("sharc") || name.contains("jitter") || name.contains("pbr") || name.contains("transparent")
            || name.contains("bounce") || name.contains("sample")) {
            return new String[] {"PERFORMANCE", "renderer"};
        }
        return new String[] {"OTHER", "unsorted"};
    }

    private static OptionInstance<?> row(String module, String name, String shortName, String type, String value,
        Map<String, String> staged) {
        return row(module, name, shortName, type, value, staged, null, null, () -> { });
    }

    /**
     * A row for an attribute from its type. {@code step} (null for the default) is the slider's step for a range,
     * as a shader pack declares it; {@code description} (null for the translation of the name's
     * {@code .tooltip} key) goes above the attribute's name in the description; {@code changed} runs after every
     * change is staged.
     */
    static OptionInstance<?> row(String module, String name, String shortName, String type, String value,
        Map<String, String> staged, Double step, Component description, Runnable changed) {
        if (type == null || value == null) {
            return null;
        }
        Component caption = caption(name);
        String key = key(module, name);
        java.util.function.Consumer<String> stage = v -> {
            staged.put(key, v);
            changed.run();
        };
        try {
            if (type.equals("bool")) {
                return OptionInstance.createBoolean(name, tooltip(name, shortName, null, description),
                    value.equals("render_pipeline.true"),
                    on -> stage.accept(on ? "render_pipeline.true" : "render_pipeline.false"));
            }
            if (type.startsWith("enum:")) {
                List<String> values = List.of(type.substring(5).split("-"));
                if (!values.contains(value)) {
                    return null;
                }
                return new OptionInstance<>(name, tooltip(name, shortName, null, description),
                    (c, v) -> Component.empty().append(caption).append(": ").append(valueName(v)),
                    new OptionInstance.Enum<>(values, Codec.STRING), value, stage::accept);
            }
            boolean whole = type.startsWith("int_range:");
            if (!whole && !type.startsWith("float_range:")) {
                return null;
            }
            Matcher range = RANGE.matcher(type.substring(type.indexOf(':') + 1));
            if (!range.matches()) {
                return null;
            }
            double min = Double.parseDouble(range.group(1));
            double max = Double.parseDouble(range.group(2));
            String rangeText = range.group(1) + " – " + range.group(2);
            OptionInstance.TooltipSupplier<Integer> tooltip = tooltip(name, shortName, rangeText, description);
            // A declared step: the slider moves from the minimum in steps of that size, up to the last one in range.
            int stepCount = step == null || !(max > min) ? -1 : (int) Math.floor((max - min) / step + 1.0e-6);
            if (stepCount >= 1 && stepCount <= 10_000 && (!whole || step >= 1.0)) {
                double size = whole ? Math.rint(step) : step;
                int count = whole ? (int) Math.floor((max - min) / size + 1.0e-6) : stepCount;
                double now = Math.max(min, Math.min(max, Double.parseDouble(value)));
                int index = (int) Math.max(0, Math.min(count, Math.round((now - min) / size)));
                java.util.function.IntFunction<String> text = i -> whole
                    ? String.valueOf(Math.round(min + i * size)) : number(Math.min(max, min + i * size));
                return new OptionInstance<Integer>(name, tooltip,
                    (c, v) -> Component.empty().append(caption).append(": " + text.apply(v)),
                    new OptionInstance.IntRange(0, count, false), index, v -> stage.accept(text.apply(v)));
            }
            if (whole) {
                int current = (int) Math.max(min, Math.min(max, Math.round(Double.parseDouble(value))));
                return new OptionInstance<Integer>(name, tooltip,
                    (c, v) -> Component.empty().append(caption).append(": " + v),
                    new OptionInstance.IntRange((int) min, (int) max, false), current,
                    v -> stage.accept(String.valueOf(v)));
            }
            // A wide range starting at zero is finer near zero, where such settings are used: the step grows with
            // the value.
            boolean curved = min >= 0.0 && max >= 4.0;
            double now = Math.max(min, Math.min(max, Double.parseDouble(value)));
            double t = (now - min) / (max - min);
            int position = (int) Math.round((curved ? Math.sqrt(t) : t) * FLOAT_STEPS);
            return new OptionInstance<Integer>(name, tooltip,
                (c, v) -> Component.empty().append(caption).append(": "
                    + (v == position ? number(now) : number(fromStep(v, min, max, curved)))),
                new OptionInstance.IntRange(0, FLOAT_STEPS, false), position,
                v -> stage.accept(number(fromStep(v, min, max, curved))));
        } catch (NumberFormatException notANumber) {
            return null;
        }
    }

    /** An enum value's name: its translation (Radiante's or the shader pack's), or its last part made readable. */
    static Component valueName(String value) {
        Component known = known(value);
        return known != null ? known : Component.literal(pretty(value.substring(value.lastIndexOf('.') + 1)));
    }

    private static double fromStep(int step, double min, double max, boolean curved) {
        double t = step / (double) FLOAT_STEPS;
        return min + (max - min) * (curved ? t * t : t);
    }

    /** A fraction as the pipeline wants it: three significant digits, always with a decimal point. */
    private static String number(double value) {
        String text = new BigDecimal(value).round(new MathContext(3)).stripTrailingZeros().toPlainString();
        return text.contains(".") ? text : text + ".0";
    }

    private static boolean exists(String key) {
        return Language.getInstance().has(key);
    }

    /**
     * The name shown for a caption key: its translation, or for a pipeline attribute without one its own name made
     * readable ("volumetric_cloud_view_steps" as "Volumetric cloud view steps").
     */
    static Component caption(String key) {
        int at = key.indexOf(".attribute.");
        if (at < 0) {
            return Component.translatable(key);
        }
        Component known = known(key);
        return known != null ? known : Component.literal(pretty(key.substring(at + ".attribute.".length())));
    }

    /**
     * A key's text: the game's translation (Radiante's own lang files), or else the line the shader pack in use gives
     * it in its {@code lang} files; null when neither has it.
     */
    static Component known(String key) {
        if (exists(key)) {
            return Component.translatable(key);
        }
        String pack = Pipeline.shaderPackTranslation(key);
        return pack != null ? Component.literal(pack) : null;
    }

    private static <T> OptionInstance.TooltipSupplier<T> tooltip(String name, String shortName, String range,
        Component description) {
        Component text = Component.empty();
        if (description != null) {
            text = Component.empty().append(description).append("\n\n");
        } else if (exists(name + ".tooltip")) {
            text = Component.empty().append(Component.translatable(name + ".tooltip")).append("\n\n");
        }
        Component full = Component.empty().append(text).append(Component.literal(shortName
            + (range == null ? "" : "\n" + range)).withColor(0xFF808888));
        return OptionInstance.cachedConstantTooltip(full);
    }

    /** "volumetric_cloud_view_steps" as "Volumetric cloud view steps". */
    static String pretty(String name) {
        String spaced = name.replace('_', ' ').trim();
        return spaced.isEmpty() ? name : spaced.substring(0, 1).toUpperCase(Locale.ROOT) + spaced.substring(1);
    }
}
