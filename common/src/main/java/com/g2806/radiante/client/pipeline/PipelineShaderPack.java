package com.g2806.radiante.client.pipeline;

import net.minecraft.client.resources.language.I18n;
import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.proxy.vulkan.RendererProxy;
import com.g2806.radiante.client.constant.VulkanConstants;
import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.pipeline.config.AttributeConfig;
import com.g2806.radiante.client.pipeline.config.ImageConfig;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import org.yaml.snakeyaml.Yaml;

import static com.g2806.radiante.client.pipeline.Pipeline.*;
import static com.g2806.radiante.client.pipeline.PipelineSettings.*;
import static com.g2806.radiante.client.pipeline.PipelineBuild.*;

/**
 * The shader pack the pipeline's ray tracing module uses: where it is, its settings and translations, choosing
 * another one, and going back to the built-in pack when the chosen one is gone or cannot run here.
 *
 * <p>These were part of {@code Pipeline}; the packs in the player's folder are {@link ShaderPackLibrary}'s.
 */
public final class PipelineShaderPack {

    private PipelineShaderPack() {
    }

    /**
     * The shader pack the pipeline uses, as stored (relative to the mod's folder, or absolute); the built-in pack's
     * path when none is set. Null without a ray tracing module.
     */
    public static String shaderPackPath() {
        AttributeConfig attribute = findAttribute(getRayTracingModule(), RAY_TRACING_SHADER_PACK_PATH_ATTRIBUTE);
        if (attribute == null) {
            return null;
        }
        return attribute.value == null || attribute.value.isBlank() ? BUILT_IN_SHADER_PACK_PATH : attribute.value;
    }

    /** The built-in pack (radiante-pt) as the pipeline stores it. */
    public static String builtInShaderPackPath() {
        return BUILT_IN_SHADER_PACK_PATH;
    }

    /**
     * The settings page of the pack in use ({@link ShaderPackSettings}): its own attributes with their groups and
     * the rest, and its presets. {@code NONE} for Radiante PT.
     */
    public static ShaderPackSettings shaderPackSettings() {
        String path = shaderPackPath();
        return path == null ? ShaderPackSettings.NONE : ShaderPackLibrary.settings(path);
    }

    /**
     * A name from the lang files of the pack in use (Radiante PT's lines, then the pack's, in the game's language),
     * or null when they have no such key. The renderer reads them with the pack's attributes.
     */
    public static String shaderPackTranslation(String key) {
        Module module = getRayTracingModule();
        return module == null || key == null ? null : module.dynamicTranslations.get(key);
    }

    /**
     * Switches the pipeline to another shader pack; true when it changed. That takes a build, which loads the pack's
     * own attributes and the values it saved in its settings file (the pack's name plus ".txt") instead of these.
     */
    public static boolean selectShaderPack(String path) {
        AttributeConfig attribute = findAttribute(getRayTracingModule(), RAY_TRACING_SHADER_PACK_PATH_ATTRIBUTE);
        String value = path == null || path.isBlank() ? BUILT_IN_SHADER_PACK_PATH : path;
        if (attribute == null || Objects.equals(shaderPackPath(), value)) {
            return false;
        }
        logSettingChange(RAY_TRACING_SHADER_PACK_PATH_ATTRIBUTE, attribute.value, value);
        assign(attribute, value);
        return true;
    }

    /** Whether a pack needs chunk emission collection that is switched off, so the build would use the built-in. */
    public static boolean shaderPackUnavailable(String path) {
        return !isShaderPackValueSelectable(path);
    }

    static Path resolveShaderPackPath(String configuredPath) {
        String value = configuredPath == null ? "" : configuredPath.trim();
        Path shaderPackPath = value.isEmpty()
                ? Path.of(BUILT_IN_SHADER_PACK_PATH)
                : Path.of(value);
        if (!shaderPackPath.isAbsolute() && RadianteClient.radianceDir != null) {
            shaderPackPath = RadianteClient.radianceDir.resolve(shaderPackPath);
        }
        return shaderPackPath.toAbsolutePath().normalize();
    }

    /**
     * The file a shader pack's settings are kept in: the pack's own name with {@code .txt} after it, next to the
     * pack. The pack is the one these attributes of the ray tracing module name, the built-in one when they name
     * none. Null when that cannot be said (no folder of the mod's yet, a path the system cannot read).
     */
    static Path settingsFileOf(List<AttributeConfig> attributes) {
        String configuredPath = "";
        if (attributes != null) {
            for (AttributeConfig attribute : attributes) {
                if (attribute != null && Objects.equals(attribute.name, RAY_TRACING_SHADER_PACK_PATH_ATTRIBUTE)) {
                    configuredPath = attribute.value == null ? "" : attribute.value.trim();
                    break;
                }
            }
        }
        try {
            if (configuredPath.isEmpty() && RadianteClient.radianceDir == null) {
                return null;
            }
            Path pack = resolveShaderPackPath(configuredPath);
            return pack.getFileName() == null ? null : pack.resolveSibling(pack.getFileName().toString() + ".txt");
        } catch (Exception e) {
            RadianteClient.LOGGER.error("Error while resolving shader pack path: {}", configuredPath, e);
            return null;
        }
    }

    static Object loadShaderPackDocument(Path shaderPackPath, String fileName) {
        if (shaderPackPath == null || fileName == null || fileName.isBlank()) {
            return null;
        }

        try {
            if (Files.isDirectory(shaderPackPath)) {
                Path configPath = shaderPackPath.resolve(fileName);
                if (!Files.exists(configPath)) {
                    return null;
                }
                return new Yaml().load(Files.readString(configPath, StandardCharsets.UTF_8));
            }

            if (!Files.exists(shaderPackPath)) {
                return null;
            }

            try (ZipFile zipFile = new ZipFile(shaderPackPath.toFile())) {
                ZipEntry entry = zipFile.getEntry(fileName);
                if (entry == null) {
                    return null;
                }
                try (InputStream in = zipFile.getInputStream(entry)) {
                    return new Yaml().load(new String(in.readAllBytes(), StandardCharsets.UTF_8));
                }
            }
        } catch (Exception e) {
            RadianteClient.LOGGER.warn("Failed to read shader pack document: {} from {}", fileName, shaderPackPath, e);
            return null;
        }
    }

    static boolean readRequiresEmissionFromConfig(Path shaderPackPath) {
        try {
            Object loaded = loadShaderPackDocument(shaderPackPath, SHADER_PACK_CONFIG_FILE);
            if (loaded instanceof Map<?, ?> root) {
                // The key the renderer reads too (shader_pack_loader.cpp), so both sides agree on the pack.
                Object value = root.get("requires_emission");
                if (value instanceof Boolean bool) {
                    return bool;
                }
                if (value instanceof String string) {
                    return Boolean.parseBoolean(string);
                }
            }
        } catch (Exception e) {
            RadianteClient.LOGGER.warn("Failed to read shader pack config: {}", shaderPackPath, e);
        }
        return false;
    }

    static boolean isShaderPackValueSelectable(String configuredPath) {
        return !readRequiresEmissionFromConfig(resolveShaderPackPath(configuredPath)) || Options.collectChunkEmission;
    }

    static boolean fallbackUnavailableShaderPacks() {
        boolean changed = false;
        for (Module module : INSTANCE.modules) {
            if (module == null || !Objects.equals(module.name, RAY_TRACING_MODULE_NAME)) {
                continue;
            }
            AttributeConfig shaderPackPath = findAttribute(module, RAY_TRACING_SHADER_PACK_PATH_ATTRIBUTE);
            if (shaderPackPath == null || isShaderPackValueSelectable(shaderPackPath.value)) {
                continue;
            }
            shaderPackPath.value = BUILT_IN_SHADER_PACK_PATH;
            changed = true;
            RadianteClient.LOGGER.warn("Selected shader pack requires chunk emission collection. Falling back to the built-in pack.");
        }
        return changed;
    }

    public static void ensureSelectedShaderPackAvailable() {
        if (!fallbackUnavailableShaderPacks()) {
            return;
        }
        savePipeline();
        build();
    }
}
