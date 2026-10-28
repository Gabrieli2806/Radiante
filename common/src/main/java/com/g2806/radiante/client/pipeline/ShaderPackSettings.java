package com.g2806.radiante.client.pipeline;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * What a shader pack says about its settings page, read from its own {@code configs.json} (not the one merged with
 * Radiante PT's): the extra keys of its attributes ({@code group}, {@code order}, {@code advanced},
 * {@code description}, {@code performance_impact}, {@code step}, {@code visible_when}), its {@code presets} and its
 * {@code profile_keys}. Every key is optional and the renderer ignores them all; a key with a value of the wrong kind
 * is dropped with a warning, never the pack. No game classes here, so the rules are unit tested
 * ({@code ShaderPackSettingsTest}).
 *
 * <p>The pack's own attributes are the ones it declares that Radiante PT does not have: those get the pack's page.
 * An entry that only changes one of Radiante PT's attributes (a new default) leaves it where Radiante's settings show
 * it.
 */
public final class ShaderPackSettings {

    /** The prefix of every shader pack attribute; names in these keys may leave it out. */
    public static final String ATTRIBUTE_PREFIX = "render_pipeline.module.ray_tracing.attribute.";
    private static final String TRUE = "render_pipeline.true";
    private static final String FALSE = "render_pipeline.false";
    private static final Pattern RANGE = Pattern.compile("(-?[0-9.]+)-(-?[0-9.]+)");

    /** The cost shown under a setting's description, as Radiante's own settings show theirs. */
    public enum Impact {
        LOW, MEDIUM, HIGH
    }

    /**
     * {@code "visible_when": {"attribute": "...", "equals": ...}} (or {@code "not_equals"}): the row shows only while
     * that attribute has one of the values (or none of them, negated). Values are compared as the attribute's type
     * reads them, so {@code true}, {@code "true"} and {@code "render_pipeline.true"} are one value, as are {@code 2}
     * and {@code "2.0"}.
     */
    public record VisibleWhen(String attribute, List<String> values, boolean negated) {
        public boolean test(String type, String value) {
            boolean match = false;
            for (String wanted : this.values) {
                if (sameValue(type, this.attribute, wanted, value)) {
                    match = true;
                    break;
                }
            }
            return match != this.negated;
        }
    }

    /**
     * One of the pack's own attributes. {@code group} and {@code description} are keys of the pack's lang files (or
     * text shown as written when they have no translation); {@code group} is null for none. {@code step} is null for
     * the default slider, {@code impact} and {@code visibleWhen} null for none.
     */
    public record Attribute(String name, String type, String group, double order, boolean advanced,
                            String description, Impact impact, Double step, VisibleWhen visibleWhen, int position) {
    }

    /**
     * A named set of values: {@code name} and {@code description} are lang keys (or text), {@code values} the
     * attributes it sets (full names, the pack's own attributes only) and the values as written in the pack, read
     * with {@link #normalize} when the preset is picked.
     */
    public record Preset(String name, String description, Map<String, String> values) {
    }

    /** No settings page: the built-in pack, or a pack that could not be read. */
    public static final ShaderPackSettings NONE =
        new ShaderPackSettings(List.of(), List.of(), Set.of(), false, List.of());

    private final List<Attribute> attributes;
    private final List<Preset> presets;
    private final Set<String> profileKeys;
    private final boolean allProfileKeys;
    private final List<String> warnings;

    private ShaderPackSettings(List<Attribute> attributes, List<Preset> presets, Set<String> profileKeys,
                               boolean allProfileKeys, List<String> warnings) {
        this.attributes = attributes;
        this.presets = presets;
        this.profileKeys = profileKeys;
        this.allProfileKeys = allProfileKeys;
        this.warnings = warnings;
    }

    /** The pack's own attributes, in the order the page shows them: by {@code order}, then by place in the file. */
    public List<Attribute> attributes() {
        return this.attributes;
    }

    public List<Preset> presets() {
        return this.presets;
    }

    /** What was dropped and why, for the log. */
    public List<String> warnings() {
        return this.warnings;
    }

    /** The groups in the order their first attribute comes; attributes without a group are not in it. */
    public List<String> groups() {
        Set<String> groups = new LinkedHashSet<>();
        for (Attribute attribute : this.attributes) {
            if (attribute.group() != null) {
                groups.add(attribute.group());
            }
        }
        return List.copyOf(groups);
    }

    public boolean isOwn(String name) {
        for (Attribute attribute : this.attributes) {
            if (attribute.name().equals(name)) {
                return true;
            }
        }
        return false;
    }

    /**
     * Whether a renderer profile may write this Radiante PT attribute while the pack is in use: only one the pack
     * lists in {@code profile_keys} ({@code "*"} lists them all).
     */
    public boolean profileKey(String name) {
        return this.allProfileKeys || this.profileKeys.contains(name);
    }

    /** {@code "mellow_sky"} as {@code "render_pipeline.module.ray_tracing.attribute.mellow_sky"}; a full name as is. */
    public static String fullName(String name) {
        String trimmed = name.trim();
        return trimmed.startsWith("render_pipeline.") ? trimmed : ATTRIBUTE_PREFIX + trimmed;
    }

    /**
     * Reads the settings keys of a pack's {@code configs.json}. {@code baseNames} are Radiante PT's attribute names,
     * which are not the pack's own.
     */
    public static ShaderPackSettings parse(JsonObject root, Set<String> baseNames) {
        if (root == null) {
            return NONE;
        }
        List<String> warnings = new ArrayList<>();
        List<Attribute> attributes = new ArrayList<>();
        JsonElement attributesNode = root.get("attributes");
        if (attributesNode != null && attributesNode.isJsonArray()) {
            int position = 0;
            for (JsonElement element : attributesNode.getAsJsonArray()) {
                position++;
                if (!element.isJsonObject()) {
                    continue;
                }
                JsonObject object = element.getAsJsonObject();
                String name = string(object, "name");
                String type = string(object, "type");
                if (name == null || type == null || baseNames.contains(name)) {
                    continue;
                }
                attributes.add(attribute(object, name, type, position, warnings));
            }
        }
        // Stable: equal orders keep the file's order.
        attributes.sort(Comparator.comparingDouble(Attribute::order));

        Set<String> ownNames = new HashSet<>();
        for (Attribute attribute : attributes) {
            ownNames.add(attribute.name());
        }
        List<Preset> presets = new ArrayList<>();
        JsonElement presetsNode = root.get("presets");
        if (presetsNode != null) {
            if (!presetsNode.isJsonArray()) {
                warnings.add("presets: not an array");
            } else {
                int index = 0;
                for (JsonElement element : presetsNode.getAsJsonArray()) {
                    index++;
                    Preset preset = preset(element, index, ownNames, warnings);
                    if (preset != null) {
                        presets.add(preset);
                    }
                }
            }
        }

        Set<String> profileKeys = new HashSet<>();
        boolean all = false;
        JsonElement keysNode = root.get("profile_keys");
        if (keysNode != null) {
            if (!keysNode.isJsonArray()) {
                warnings.add("profile_keys: not an array");
            } else {
                for (JsonElement key : keysNode.getAsJsonArray()) {
                    if (!isString(key)) {
                        warnings.add("profile_keys: " + key + " is not a name");
                        continue;
                    }
                    String text = key.getAsString().trim();
                    if (text.equals("*")) {
                        all = true;
                    } else if (!text.isEmpty()) {
                        profileKeys.add(fullName(text));
                    }
                }
            }
        }
        return new ShaderPackSettings(List.copyOf(attributes), List.copyOf(presets), Set.copyOf(profileKeys), all,
            List.copyOf(warnings));
    }

    private static Attribute attribute(JsonObject object, String name, String type, int position,
                                       List<String> warnings) {
        String where = "attribute " + name + ": ";
        String group = optionalString(object, "group", where, warnings);
        String description = optionalString(object, "description", where, warnings);
        double order = 0.0;
        JsonElement orderNode = object.get("order");
        if (orderNode != null) {
            if (isNumber(orderNode)) {
                order = orderNode.getAsDouble();
            } else {
                warnings.add(where + "order is not a number");
            }
        }
        boolean advanced = false;
        JsonElement advancedNode = object.get("advanced");
        if (advancedNode != null) {
            if (advancedNode.isJsonPrimitive() && advancedNode.getAsJsonPrimitive().isBoolean()) {
                advanced = advancedNode.getAsBoolean();
            } else {
                warnings.add(where + "advanced is not true or false");
            }
        }
        Impact impact = null;
        String impactText = optionalString(object, "performance_impact", where, warnings);
        if (impactText != null) {
            try {
                impact = Impact.valueOf(impactText.trim().toUpperCase(Locale.ROOT));
            } catch (IllegalArgumentException e) {
                warnings.add(where + "performance_impact \"" + impactText + "\" is not low, medium or high");
            }
        }
        Double step = null;
        JsonElement stepNode = object.get("step");
        if (stepNode != null) {
            if (!isNumber(stepNode) || !(stepNode.getAsDouble() > 0.0) || !Double.isFinite(stepNode.getAsDouble())) {
                warnings.add(where + "step is not a number above 0");
            } else if (!type.startsWith("int_range:") && !type.startsWith("float_range:")) {
                warnings.add(where + "step only applies to int_range and float_range");
            } else {
                step = stepNode.getAsDouble();
            }
        }
        VisibleWhen visibleWhen = null;
        JsonElement visibleNode = object.get("visible_when");
        if (visibleNode != null) {
            visibleWhen = visibleWhen(visibleNode, where, warnings);
        }
        return new Attribute(name, type, group == null || group.isBlank() ? null : group.trim(), order, advanced,
            description == null || description.isBlank() ? null : description, impact, step, visibleWhen, position);
    }

    private static VisibleWhen visibleWhen(JsonElement node, String where, List<String> warnings) {
        if (!node.isJsonObject()) {
            warnings.add(where + "visible_when is not an object");
            return null;
        }
        JsonObject object = node.getAsJsonObject();
        JsonElement attribute = object.get("attribute");
        if (!isString(attribute) || attribute.getAsString().isBlank()) {
            warnings.add(where + "visible_when has no attribute");
            return null;
        }
        boolean negated = !object.has("equals") && object.has("not_equals");
        JsonElement values = object.get(negated ? "not_equals" : "equals");
        if (values == null) {
            warnings.add(where + "visible_when has neither equals nor not_equals");
            return null;
        }
        List<String> texts = new ArrayList<>();
        if (values.isJsonArray()) {
            for (JsonElement value : values.getAsJsonArray()) {
                String text = scalar(value);
                if (text != null) {
                    texts.add(text);
                }
            }
        } else {
            String text = scalar(values);
            if (text != null) {
                texts.add(text);
            }
        }
        if (texts.isEmpty()) {
            warnings.add(where + "visible_when has no value to compare with");
            return null;
        }
        return new VisibleWhen(fullName(attribute.getAsString()), List.copyOf(texts), negated);
    }

    private static Preset preset(JsonElement element, int index, Set<String> ownNames, List<String> warnings) {
        String where = "presets[" + index + "]: ";
        if (!element.isJsonObject()) {
            warnings.add(where + "not an object");
            return null;
        }
        JsonObject object = element.getAsJsonObject();
        String title = string(object, "name");
        if (title == null || title.isBlank()) {
            warnings.add(where + "has no name");
            return null;
        }
        JsonElement valuesNode = object.get("values");
        if (valuesNode == null || !valuesNode.isJsonObject()) {
            warnings.add(where + "has no values object");
            return null;
        }
        Map<String, String> values = new LinkedHashMap<>();
        for (Map.Entry<String, JsonElement> entry : valuesNode.getAsJsonObject().entrySet()) {
            String value = scalar(entry.getValue());
            if (value == null) {
                warnings.add(where + entry.getKey() + " is not a single value");
                continue;
            }
            String name = fullName(entry.getKey());
            // Only the pack's own settings: Radiante PT's have rows of their own on Radiante's pages.
            if (!ownNames.contains(name)) {
                warnings.add(where + entry.getKey() + " is not one of the pack's own settings");
                continue;
            }
            values.put(name, value);
        }
        if (values.isEmpty()) {
            warnings.add(where + "sets nothing");
            return null;
        }
        return new Preset(title.trim(), optionalString(object, "description", where, warnings),
            Collections.unmodifiableMap(values));
    }

    /**
     * A value as written in a pack (a preset, {@code visible_when}) as the attribute stores it, or null when it is
     * not one of the attribute's values: {@code true}/{@code "true"} as {@code render_pipeline.true}, a whole number
     * for an {@code int_range}, a number with a decimal point for a {@code float_range}, and an enum value by its full
     * name or by the part after the attribute's name ({@code "off"} for {@code <attribute>.off}).
     */
    public static String normalize(String type, String attribute, String raw) {
        if (type == null || raw == null) {
            return null;
        }
        String value = raw.trim();
        try {
            if (type.equals("bool")) {
                if (value.equalsIgnoreCase("true") || value.equals(TRUE)) {
                    return TRUE;
                }
                if (value.equalsIgnoreCase("false") || value.equals(FALSE)) {
                    return FALSE;
                }
                return null;
            }
            if (type.startsWith("int_range:") || type.startsWith("float_range:")) {
                Matcher range = RANGE.matcher(type.substring(type.indexOf(':') + 1));
                if (!range.matches()) {
                    return null;
                }
                double number = Double.parseDouble(value);
                if (!Double.isFinite(number) || number < Double.parseDouble(range.group(1))
                    || number > Double.parseDouble(range.group(2))) {
                    return null;
                }
                if (type.startsWith("int_range:")) {
                    return number == Math.rint(number) ? Long.toString((long) number) : null;
                }
                return value.contains(".") || value.contains("e") || value.contains("E") ? value : value + ".0";
            }
            if (type.startsWith("enum:")) {
                List<String> options = List.of(type.substring(5).split("-"));
                if (options.contains(value)) {
                    return value;
                }
                String longName = attribute == null ? null : attribute + "." + value;
                return longName != null && options.contains(longName) ? longName : null;
            }
            if (type.equals("vec3")) {
                String[] parts = value.split(",");
                if (parts.length != 3) {
                    return null;
                }
                for (String part : parts) {
                    if (!Double.isFinite(Double.parseDouble(part.trim()))) {
                        return null;
                    }
                }
                return value;
            }
        } catch (NumberFormatException e) {
            return null;
        }
        return value;
    }

    /** Whether a value as written in a pack and a stored value are the same value of the attribute's type. */
    public static boolean sameValue(String type, String attribute, String written, String stored) {
        if (written == null || stored == null) {
            return false;
        }
        if (type != null && (type.startsWith("int_range:") || type.startsWith("float_range:"))) {
            try {
                return Double.parseDouble(written.trim()) == Double.parseDouble(stored.trim());
            } catch (NumberFormatException e) {
                return false;
            }
        }
        String normalized = normalize(type, attribute, written);
        return stored.equals(normalized != null ? normalized : written.trim());
    }

    private static boolean isString(JsonElement element) {
        return element != null && element.isJsonPrimitive() && element.getAsJsonPrimitive().isString();
    }

    private static boolean isNumber(JsonElement element) {
        return element != null && element.isJsonPrimitive() && element.getAsJsonPrimitive().isNumber();
    }

    private static String string(JsonObject object, String key) {
        JsonElement value = object.get(key);
        return isString(value) ? value.getAsString() : null;
    }

    private static String optionalString(JsonObject object, String key, String where, List<String> warnings) {
        JsonElement value = object.get(key);
        if (value == null || value.isJsonNull()) {
            return null;
        }
        if (!isString(value)) {
            warnings.add(where + key + " is not text");
            return null;
        }
        return value.getAsString();
    }

    /** A JSON string, number or true/false as text ({@code true} as {@code "true"}); null for anything else. */
    private static String scalar(JsonElement value) {
        if (value == null || !value.isJsonPrimitive()) {
            return null;
        }
        JsonPrimitive primitive = value.getAsJsonPrimitive();
        if (primitive.isBoolean()) {
            return primitive.getAsBoolean() ? "true" : "false";
        }
        return primitive.getAsString();
    }
}
