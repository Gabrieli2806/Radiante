package com.g2806.radiante.client.pipeline;

import static com.g2806.radiante.client.pipeline.Pipeline.BUILT_IN_SHADER_PACK_PATH;
import static com.g2806.radiante.client.pipeline.Pipeline.RAY_TRACING_MODULE_NAME;
import static com.g2806.radiante.client.pipeline.Pipeline.RAY_TRACING_SHADER_PACK_PATH_ATTRIBUTE;
import static com.g2806.radiante.client.pipeline.Pipeline.getAttributes;
import static com.g2806.radiante.client.pipeline.Pipeline.shaderPackDebugViews;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.pipeline.config.AttributeConfig;
import net.minecraft.client.Minecraft;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;
import java.util.Set;

import org.yaml.snakeyaml.Yaml;

/**
 * A module's attribute list as a build needs it: the module's own attributes, and for the ray tracing module the
 * shader pack's, which the renderer reads from the pack ({@link Pipeline#getAttributes}) with the pack's translations
 * and debug views, and whose values come from the pack's settings file (the pack's name plus ".txt").
 */
final class ModuleAttributes {

    private ModuleAttributes() {
    }

    static String getCurrentLanguageCode() {
        Minecraft client = Minecraft.getInstance();
        if (client != null) {
            var languageManager = client.getLanguageManager();
            if (languageManager != null) {
                String language = languageManager.getSelected();
                if (language != null && !language.isBlank()) {
                    return language.toLowerCase(Locale.ROOT);
                }
            }
            if (client.options != null && client.options.languageCode != null && !client.options.languageCode.isBlank()) {
                return client.options.languageCode.toLowerCase(Locale.ROOT);
            }
        }

        String fallback = Locale.getDefault().toString();
        return fallback == null || fallback.isBlank()
                ? "en_us"
                : fallback.toLowerCase(Locale.ROOT);
    }

    public static void getModuleAttributes(Module module) {
        if (module == null) {
            return;
        }

        if (module.staticAttributeConfigs == null) {
            module.staticAttributeConfigs = Module.copyAttributeConfigs(module.attributeConfigs);
        }

        Map<String, String> currentValues = new HashMap<>();
        if (module.attributeConfigs != null) {
            for (AttributeConfig attributeConfig : module.attributeConfigs) {
                if (attributeConfig == null || attributeConfig.name == null) {
                    continue;
                }
                currentValues.put(attributeConfig.name, attributeConfig.value);
            }
        }

        List<AttributeConfig> mergedAttributeConfigs = Module.copyAttributeConfigs(module.staticAttributeConfigs);
        Map<String, String> translations = new HashMap<>();
        // The shader pack's own attributes and their defaults, to check the values they end up with.
        Map<String, String> packDefaults = new HashMap<>();
        Path shaderPackAttributeStoragePath = null;
        boolean preserveCurrentDynamicValues = true;

        if (Objects.equals(module.name, RAY_TRACING_MODULE_NAME)) {
            List<AttributeConfig> sourceAttributes =
                    module.attributeConfigs != null ? module.attributeConfigs : module.staticAttributeConfigs;
            String configuredPath = "";
            if (sourceAttributes != null) {
                for (AttributeConfig attributeConfig : sourceAttributes) {
                    if (attributeConfig == null || !Objects.equals(attributeConfig.name, RAY_TRACING_SHADER_PACK_PATH_ATTRIBUTE)) {
                        continue;
                    }
                    configuredPath = attributeConfig.value == null ? "" : attributeConfig.value.trim();
                    break;
                }
            }

            try {
                Path shaderPackPath = null;
                if (configuredPath.isEmpty()) {
                    if (RadianteClient.radianceDir != null) {
                        shaderPackPath = RadianteClient.radianceDir
                                .resolve(BUILT_IN_SHADER_PACK_PATH)
                                .toAbsolutePath()
                                .normalize();
                    }
                } else {
                    shaderPackPath = Path.of(configuredPath);
                    if (!shaderPackPath.isAbsolute() && RadianteClient.radianceDir != null) {
                        shaderPackPath = RadianteClient.radianceDir.resolve(shaderPackPath);
                    }
                    shaderPackPath = shaderPackPath.toAbsolutePath().normalize();
                }

                if (shaderPackPath != null && shaderPackPath.getFileName() != null) {
                    shaderPackAttributeStoragePath =
                            shaderPackPath.resolveSibling(shaderPackPath.getFileName().toString() + ".txt");
                }
            } catch (Exception e) {
                RadianteClient.LOGGER.error("Error while resolving shader pack path: {}", configuredPath, e);
            }

            String normalizedStoragePath = shaderPackAttributeStoragePath == null
                    ? null
                    : shaderPackAttributeStoragePath.toAbsolutePath().normalize().toString();
            preserveCurrentDynamicValues = Objects.equals(
                    module.dynamicAttributeStoragePath,
                    normalizedStoragePath);

            List<String> attributeList = new ArrayList<>();
            if (sourceAttributes != null) {
                for (AttributeConfig attributeConfig : sourceAttributes) {
                    if (attributeConfig == null || attributeConfig.name == null) {
                        continue;
                    }
                    attributeList.add(attributeConfig.name);
                    attributeList.add(attributeConfig.value != null ? attributeConfig.value : "");
                }
            }

            String language = getCurrentLanguageCode();

            String metadata = getAttributes(
                    module.name,
                    attributeList.toArray(String[]::new),
                    language);
            if (metadata != null && !metadata.isBlank()) {
                Object loaded = new Yaml().load(metadata);
                if (loaded instanceof Map<?, ?> root) {
                    Object attributesNode = root.get("attributes");
                    if (attributesNode instanceof List<?> attributeNodes) {
                        for (Object attributeNode : attributeNodes) {
                            if (!(attributeNode instanceof Map<?, ?> attributeMap)) {
                                continue;
                            }

                            Object name = attributeMap.get("name");
                            Object type = attributeMap.get("type");
                            if (name == null || type == null) {
                                continue;
                            }

                            AttributeConfig attributeConfig = new AttributeConfig();
                            attributeConfig.name = Objects.toString(name);
                            attributeConfig.type = Objects.toString(type);
                            Object defaultValue = attributeMap.containsKey("default_value")
                                    ? attributeMap.get("default_value")
                                    : attributeMap.get("value");
                            attributeConfig.value = Objects.toString(defaultValue, "");
                            attributeConfig.runtime =
                                    Boolean.parseBoolean(Objects.toString(attributeMap.get("runtime"), "false"));
                            packDefaults.put(attributeConfig.name, attributeConfig.value);
                            mergedAttributeConfigs.add(attributeConfig);
                        }
                    }

                    List<String> debugViews = new ArrayList<>();
                    if (root.get("debug_views") instanceof List<?> viewNodes) {
                        for (Object viewNode : viewNodes) {
                            if (viewNode != null) {
                                debugViews.add(Objects.toString(viewNode));
                            }
                        }
                    }
                    shaderPackDebugViews = List.copyOf(debugViews);

                    Object translationsNode = root.get("translations");
                    if (translationsNode instanceof Map<?, ?> translationMap) {
                        for (Map.Entry<?, ?> entry : translationMap.entrySet()) {
                            if (entry.getKey() == null || entry.getValue() == null) {
                                continue;
                            }
                            translations.put(
                                    Objects.toString(entry.getKey()),
                                    Objects.toString(entry.getValue()));
                        }
                    }
                }
            }

            if (shaderPackAttributeStoragePath != null && Files.exists(shaderPackAttributeStoragePath)) {
                Map<String, String> storedValues = new HashMap<>();
                try {
                    for (String line : Files.readAllLines(shaderPackAttributeStoragePath, StandardCharsets.UTF_8)) {
                        if (line == null || line.isBlank()) {
                            continue;
                        }
                        int separatorIndex = line.indexOf('=');
                        if (separatorIndex <= 0) {
                            continue;
                        }
                        String key = line.substring(0, separatorIndex).trim();
                        if (key.isEmpty()) {
                            continue;
                        }
                        storedValues.put(key, line.substring(separatorIndex + 1));
                    }
                } catch (IOException e) {
                    RadianteClient.LOGGER.error(
                            "Error while loading shader attribute storage: {}",
                            shaderPackAttributeStoragePath,
                            e);
                }

                if (!storedValues.isEmpty()) {
                    for (AttributeConfig attributeConfig : mergedAttributeConfigs) {
                        if (attributeConfig == null || attributeConfig.name == null) {
                            continue;
                        }
                        if (!storedValues.containsKey(attributeConfig.name)) {
                            continue;
                        }
                        attributeConfig.value = storedValues.get(attributeConfig.name);
                    }
                }
            }
        }

        Set<String> staticAttributeNames = preserveCurrentDynamicValues
                ? Set.of()
                : collectStaticAttributeNames(module);
        for (AttributeConfig attributeConfig : mergedAttributeConfigs) {
            if (attributeConfig == null || attributeConfig.name == null) {
                continue;
            }
            if (currentValues.containsKey(attributeConfig.name)) {
                if (!preserveCurrentDynamicValues
                        && !staticAttributeNames.contains(attributeConfig.name)) {
                    continue;
                }
                attributeConfig.value = currentValues.get(attributeConfig.name);
            }
            // A value the pack does not offer goes back to the pack's default. Packs share attribute names, and a
            // value only one of them has (radiante-pt's Bedrock water, kept from radiante-pt by the player) reached
            // another pack's enum, where the renderer compiled the value's text into the shaders and the build failed.
            String packDefault = packDefaults.get(attributeConfig.name);
            String type = attributeConfig.type;
            if (packDefault != null && attributeConfig.value != null && type != null
                    && (type.equals("bool") || type.startsWith("enum:") || type.startsWith("int_range:")
                        || type.startsWith("float_range:"))
                    && !com.g2806.radiante.client.option.SettingsCode.allowed(type, attributeConfig.value)) {
                RadianteClient.LOGGER.warn("Shader pack setting {}: \"{}\" is not one of its values; using {}",
                        attributeConfig.name, attributeConfig.value, packDefault);
                attributeConfig.value = packDefault;
            }
        }

        module.attributeConfigs = mergedAttributeConfigs;
        module.dynamicTranslations.clear();
        module.dynamicTranslations.putAll(translations);
        module.dynamicAttributeStoragePath = shaderPackAttributeStoragePath == null
                ? null
                : shaderPackAttributeStoragePath.toAbsolutePath().normalize().toString();
    }

    static Set<String> collectStaticAttributeNames(Module module) {
        Set<String> names = new HashSet<>();
        if (module == null || module.staticAttributeConfigs == null) {
            return names;
        }
        for (AttributeConfig attributeConfig : module.staticAttributeConfigs) {
            if (attributeConfig == null || attributeConfig.name == null) {
                continue;
            }
            names.add(attributeConfig.name);
        }
        return names;
    }
}
