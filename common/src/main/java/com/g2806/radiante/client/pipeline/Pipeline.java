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

/**
 * The render pipeline: the modules in use, how their images connect and the values of their attributes, with the
 * settings' way to read and change those values and the build that hands it all to the renderer. Its parts live
 * beside it: which modules a preset is made of in {@link PresetAssembly}, the files it is stored in in
 * {@link PipelineStore}, a module's attributes as the shader pack declares them in {@link ModuleAttributes}, the
 * layout the renderer reads in {@link NativePipelineParams}, and the attribute names in {@link RtAttributes}. The
 * native methods and the classes {@code pipeline.yaml} is made of stay here, where the renderer and the file look
 * for them by name.
 */
public class Pipeline {

    public static Pipeline INSTANCE = new Pipeline();

    /**
     * What assembling a pipeline asks of the game and of the native renderer: which modules this build of the
     * renderer has, what a module says about its attributes, whether DLSS can do Ray Reconstruction, and the
     * language the attributes' names come in. In the game that is the renderer itself; a test puts its own answers
     * here, so the presets and the stored pipeline can be checked without a game or a GPU.
     */
    interface Host {

        boolean hasModule(String name);

        String attributes(String name, String[] attributes, String language);

        boolean rayReconstruction();

        String language();
    }

    static final Host RENDERER = new Host() {
        @Override
        public boolean hasModule(String name) {
            return isNativeModuleAvailable(name);
        }

        @Override
        public String attributes(String name, String[] attributes, String language) {
            return getAttributes(name, attributes, language);
        }

        @Override
        public boolean rayReconstruction() {
            return isRayReconstructionAvailable();
        }

        @Override
        public String language() {
            return ModuleAttributes.getCurrentLanguageCode();
        }
    };

    static Host host = RENDERER;
    public static final String RAY_TRACING_MODULE_NAME = "render_pipeline.module.ray_tracing.name";
    static final String RAY_TRACING_SHADER_PACK_PATH_ATTRIBUTE = "render_pipeline.module.ray_tracing.attribute.shader_pack_path";
    static final String BUILT_IN_SHADER_PACK_PATH = "shaders/world/ray_tracing/radiante-pt.zip";
    private static final String SHADER_PACK_CONFIG_FILE = "configs.json";
    public static final String DLSS_MODULE_NAME = "render_pipeline.module.dlss.name";
    static final String NRD_MODULE_NAME = "render_pipeline.module.nrd.name";
    static final String TEMPORAL_ACCUMULATION_MODULE_NAME = "render_pipeline.module.temporal_accumulation.name";
    public static final String TONE_MAPPING_MODULE_NAME = "render_pipeline.module.tone_mapping.name";
    public static final String POST_RENDER_MODULE_NAME = "render_pipeline.module.post_render.name";
    static Path PIPELINE_CONFIG_PATH = null;
    final List<Module> modules = new ArrayList<>();
    final Map<ImageConfig, List<ImageConfig>> moduleConnections = new HashMap<>();
    private Map<String, ModuleEntry> moduleEntries;

    PipelineMode mode = PipelineMode.PRESET;
    String activePresetName = null;
    /**
     * Runtime attributes changed since the last build, by name, for pushRuntimeChanges to hand to the renderer.
     * Names rather than the attributes themselves: every build replaces the attribute lists.
     */
    private final Map<String, String> pendingRuntimeChanges = new LinkedHashMap<>();
    /** Something changed since the last build that only a build applies (see takeStructuralChange). */
    private boolean structuralChange;

    private Pipeline() {
    }

    public static void initFolderPath(Path folderPath) {
        PIPELINE_CONFIG_PATH = folderPath.resolve("pipeline.yaml");
        PipelineStore.migrateRenamedPack(folderPath);
        PipelineStore.migrateBuiltInPixelLight();
        ShaderPackLibrary.recoverFromCrashedTrial(folderPath, PIPELINE_CONFIG_PATH);
    }

    public static void reloadAllModuleEntries() {
        try {
            INSTANCE.moduleEntries = ModuleEntry.loadAllModuleEntries();
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    public static void clear() {
        INSTANCE.modules.clear();
        INSTANCE.moduleConnections.clear();
    }

    public static Module addModule(String name) {
        if (!isModuleAvailable(name)) {
            throw new RuntimeException("Module with name " + name + " is not available.");
        }

        ModuleEntry moduleEntry = INSTANCE.moduleEntries.get(name);
        if (moduleEntry == null) {
            throw new RuntimeException("Module with name " + name + " not found.");
        }

        Module module = moduleEntry.loadModule();
        if (module == null) {
            throw new RuntimeException("Module with name " + name + " not found.");
        }
        ModuleAttributes.getModuleAttributes(module);
        INSTANCE.modules.add(module);

        return module;
    }

    public static boolean isModuleAvailable(String moduleName) {
        if (moduleName == null || moduleName.isEmpty()) {
            return false;
        }
        if (INSTANCE.moduleEntries == null || !INSTANCE.moduleEntries.containsKey(moduleName)) {
            return false;
        }
        return host.hasModule(moduleName);
    }

    private static boolean areModulesAvailable(String... moduleNames) {
        if (moduleNames == null || moduleNames.length == 0) {
            return false;
        }
        for (String moduleName : moduleNames) {
            if (!isModuleAvailable(moduleName)) {
                return false;
            }
        }
        return true;
    }

    public static boolean isPresetAvailable(String presetName) {
        if (Objects.equals(presetName, Presets.RT_DLSSRR.key)) {
            return areModulesAvailable(
                    RAY_TRACING_MODULE_NAME,
                    DLSS_MODULE_NAME,
                    TONE_MAPPING_MODULE_NAME,
                    POST_RENDER_MODULE_NAME)
                    && rayReconstructionAvailable();
        }

        if (Objects.equals(presetName, Presets.RT_NRD.key)) {
            return areModulesAvailable(
                    RAY_TRACING_MODULE_NAME,
                    NRD_MODULE_NAME,
                    TEMPORAL_ACCUMULATION_MODULE_NAME,
                    TONE_MAPPING_MODULE_NAME,
                    POST_RENDER_MODULE_NAME);
        }

        if (Objects.equals(presetName, Presets.RT_NRD_FSR.key)) {
            return areModulesAvailable(
                    RAY_TRACING_MODULE_NAME,
                    NRD_MODULE_NAME,
                    FSR_MODULE_NAME,
                    TONE_MAPPING_MODULE_NAME,
                    POST_RENDER_MODULE_NAME);
        }

        if (Objects.equals(presetName, Presets.RT_NRD_XESS.key)) {
            return areModulesAvailable(
                    RAY_TRACING_MODULE_NAME,
                    NRD_MODULE_NAME,
                    XESS_MODULE_NAME,
                    TONE_MAPPING_MODULE_NAME,
                    POST_RENDER_MODULE_NAME);
        }

        if (Objects.equals(presetName, Presets.RT_NRD_DLSS.key)) {
            return areModulesAvailable(
                    RAY_TRACING_MODULE_NAME,
                    NRD_MODULE_NAME,
                    DLSS_MODULE_NAME,
                    TONE_MAPPING_MODULE_NAME,
                    POST_RENDER_MODULE_NAME);
        }

        return false;
    }

    /**
     * Whether DLSS can run Ray Reconstruction on this GPU. NGX may offer super resolution alone (an old driver, a GPU it
     * does not support Ray Reconstruction on), and the Ray Reconstruction pipeline then drew nothing at all.
     */
    private static boolean rayReconstructionAvailable() {
        try {
            return host.rayReconstruction();
        } catch (UnsatisfiedLinkError olderRenderer) {
            // A renderer from before the question: DLSS meant Ray Reconstruction then.
            return true;
        }
    }

    public static native boolean isRayReconstructionAvailable();

    /**
     * The pipeline for a GPU without Ray Reconstruction (RT_DLSSRR is the default where it runs): NRD with the best
     * upscaler there is. DLSS super resolution where NGX offers it without Ray Reconstruction; XeSS before FSR on an
     * Intel GPU, which runs it on its matrix units; FSR before XeSS everywhere else; NRD alone without any of them.
     */
    private static String getBestAvailablePresetName() {
        if (isPresetAvailable(Presets.RT_NRD_DLSS.key)) {
            return Presets.RT_NRD_DLSS.key;
        }
        if (com.g2806.radiante.client.render.RadianteRenderer.isIntelGpu()
                && isPresetAvailable(Presets.RT_NRD_XESS.key)) {
            return Presets.RT_NRD_XESS.key;
        }
        if (isPresetAvailable(Presets.RT_NRD_FSR.key)) {
            return Presets.RT_NRD_FSR.key;
        }
        if (isPresetAvailable(Presets.RT_NRD_XESS.key)) {
            return Presets.RT_NRD_XESS.key;
        }
        if (isPresetAvailable(Presets.RT_NRD.key)) {
            return Presets.RT_NRD.key;
        }
        if (isPresetAvailable(Presets.RT_DLSSRR.key)) {
            return Presets.RT_DLSSRR.key;
        }
        return null;
    }

    private static void assembleBestAvailablePreset(String reason) {
        String fallbackPresetName = getBestAvailablePresetName();
        if (fallbackPresetName == null) {
            throw new RuntimeException("No compatible preset is available.");
        }

        if (reason != null && !reason.isEmpty()) {
            RadianteClient.LOGGER.warn(reason + " Fallback preset: " + fallbackPresetName);
        }
        PresetAssembly.assemblePresetByKeyInternal(fallbackPresetName);
    }

    public static void connect(ImageConfig src, ImageConfig dst) {
        if (!Objects.equals(src.format, dst.format)) {
            throw new RuntimeException(
                    "Connected format does not match: " + src.format + " != " + dst.format);
        }
        if (!INSTANCE.moduleConnections.containsKey(src)) {
            INSTANCE.moduleConnections.put(src, new ArrayList<>());
        }
        INSTANCE.moduleConnections.get(src).add(dst);
    }

    public static void connectOutput(ImageConfig src) {
        if (!Objects.equals(src.format, "R8G8B8A8_UNORM")) {
            throw new RuntimeException("Invalid output format.");
        }
        src.finalOutput = true;
    }
    public static final String DLSS_MODE_ATTRIBUTE = "render_pipeline.module.dlss.attribute.mode";
    // Ultra Performance runs as Performance on small windows; see DLSSModule for the render size floor.
    public static final List<String> DLSS_MODES = List.of(
            "render_pipeline.module.dlss.attribute.mode.ultra_performance",
            "render_pipeline.module.dlss.attribute.mode.performance",
            "render_pipeline.module.dlss.attribute.mode.balanced",
            "render_pipeline.module.dlss.attribute.mode.quality",
            "render_pipeline.module.dlss.attribute.mode.dlaa");


    /**
     * The upscaler of the active pipeline and its quality mode, for the debug screen: "DLSS Balanced + Ray
     * Reconstruction", "DLSS Balanced (NRD denoiser)", "FSR3 Upscaler Quality", or "Native" when nothing upscales.
     */
    public static String describeUpscaler() {
        String upscaler = describePipelineUpscaler();
        int[] size = com.g2806.radiante.client.render.RadianteRenderer.lastOutputSize();
        if (size[0] > 0 && (size[0] != size[2] || size[1] != size[3])) {
            // A lower output resolution: what the pipeline upscales to, then FSR 1 to the window.
            return upscaler + " to " + size[0] + "x" + size[1] + ", FSR 1 to " + size[2] + "x" + size[3];
        }
        return upscaler;
    }

    private static String describePipelineUpscaler() {
        for (Module module : INSTANCE.modules) {
            if (module == null) {
                continue;
            }
            String modeAttribute = switch (module.name) {
                case DLSS_MODULE_NAME -> DLSS_MODE_ATTRIBUTE;
                case FSR_MODULE_NAME -> FSR_MODE_ATTRIBUTE;
                case XESS_MODULE_NAME -> XESS_MODE_ATTRIBUTE;
                default -> null;
            };
            if (modeAttribute == null) {
                continue;
            }
            StringBuilder description = new StringBuilder(I18n.get(module.name));
            AttributeConfig mode = findAttribute(module, modeAttribute);
            if (mode != null && mode.value != null) {
                description.append(' ').append(I18n.get(mode.value));
            }
            if (Objects.equals(module.name, DLSS_MODULE_NAME)) {
                AttributeConfig rayReconstruction = findAttribute(module, RAY_RECONSTRUCTION_ATTRIBUTE);
                if (rayReconstruction != null && Objects.equals(rayReconstruction.value, "render_pipeline.true")) {
                    description.append(" + ").append(I18n.get(RAY_RECONSTRUCTION_ATTRIBUTE));
                } else {
                    // Said outright: without it the line read the same as DLSS with a missing suffix.
                    description.append(" (NRD denoiser)");
                }
            }
            return description.toString();
        }
        return "Native";
    }

    /** The DLSS quality mode of the active pipeline, or null when it has no DLSS module. */
    public static String getDlssMode() {
        for (Module module : INSTANCE.modules) {
            if (module != null && Objects.equals(module.name, DLSS_MODULE_NAME)) {
                AttributeConfig mode = findAttribute(module, DLSS_MODE_ATTRIBUTE);
                return mode == null ? null : mode.value;
            }
        }
        return null;
    }

    static void setRayReconstructionOn(Module dlssModule, boolean enabled) {
        AttributeConfig attribute = findAttribute(dlssModule, RAY_RECONSTRUCTION_ATTRIBUTE);
        if (attribute != null) {
            assign(attribute, enabled ? "render_pipeline.true" : "render_pipeline.false");
        }
    }

    public static final String RAY_RECONSTRUCTION_ATTRIBUTE =
            "render_pipeline.module.dlss.attribute.ray_reconstruction";

    /** Sets the DLSS quality mode; returns true when the pipeline needs rebuilding. */
    public static boolean setDlssMode(String mode) {
        boolean changed = false;
        for (Module module : INSTANCE.modules) {
            if (module == null || !Objects.equals(module.name, DLSS_MODULE_NAME)) {
                continue;
            }
            AttributeConfig attribute = findAttribute(module, DLSS_MODE_ATTRIBUTE);
            if (attribute != null && !Objects.equals(attribute.value, mode)) {
                logSettingChange(DLSS_MODE_ATTRIBUTE, attribute.value, mode);
                assign(attribute, mode);
                changed = true;
            }
        }
        return changed;
    }

    public static final String FSR_MODULE_NAME = "render_pipeline.module.fsr_upscaler.name";
    /**
     * The share of the screen each upscaler renders at, in percent; 0 leaves it to the upscaler's mode. Every
     * upscaler module has one.
     */
    public static final String DLSS_RENDER_SCALE_ATTRIBUTE = "render_pipeline.module.dlss.attribute.render_scale";
    public static final String FSR_RENDER_SCALE_ATTRIBUTE = "render_pipeline.module.fsr_upscaler.attribute.render_scale";
    public static final String XESS_RENDER_SCALE_ATTRIBUTE = "render_pipeline.module.xess_sr.attribute.render_scale";
    public static final String FSR_MODE_ATTRIBUTE = "render_pipeline.module.fsr_upscaler.attribute.quality_mode";
    /**
     * The render resolution modes of the FSR and XeSS upscalers, from full resolution down; the renderer draws at
     * the mode's share of the screen and upscales the rest. Index-aligned with the quality levels (QualityPreset).
     */
    public static final List<String> FSR_MODES = List.of(
        "render_pipeline.module.fsr_upscaler.attribute.quality_mode.ultra",
        "render_pipeline.module.fsr_upscaler.attribute.quality_mode.performance",
        "render_pipeline.module.fsr_upscaler.attribute.quality_mode.balanced",
        "render_pipeline.module.fsr_upscaler.attribute.quality_mode.quality",
        "render_pipeline.module.fsr_upscaler.attribute.quality_mode.native");
    public static final String XESS_MODULE_NAME = "render_pipeline.module.xess_sr.name";
    public static final String XESS_MODE_ATTRIBUTE = "render_pipeline.module.xess_sr.attribute.quality_mode";
    public static final List<String> XESS_MODES = List.of(
        "render_pipeline.module.xess_sr.attribute.quality_mode.ultra_performance",
        "render_pipeline.module.xess_sr.attribute.quality_mode.performance",
        "render_pipeline.module.xess_sr.attribute.quality_mode.balanced",
        "render_pipeline.module.xess_sr.attribute.quality_mode.quality",
        "render_pipeline.module.xess_sr.attribute.quality_mode.ultra_quality",
        "render_pipeline.module.xess_sr.attribute.quality_mode.ultra_quality_plus",
        "render_pipeline.module.xess_sr.attribute.quality_mode.native");

    public static final String CLOUD_MODE_ATTRIBUTE = "render_pipeline.module.ray_tracing.attribute.cloud_mode";
    /**
     * The modes the renderer can produce: none, Minecraft's own blocky clouds as traced geometry, or the pack's
     * ray marched ones.
     */
    public static final List<String> CLOUD_MODES = List.of(
        "render_pipeline.module.ray_tracing.attribute.cloud_mode.off",
        "render_pipeline.module.ray_tracing.attribute.cloud_mode.vanilla",
        "render_pipeline.module.ray_tracing.attribute.cloud_mode.volumetric");

    private static final String CLOUD_TEMPORAL_ATTRIBUTE =
        "render_pipeline.module.ray_tracing.attribute.volumetric_cloud_temporal_accumulation";

    /** Sets the cloud mode; returns true when the pipeline needs rebuilding. */
    public static boolean setCloudMode(String mode) {
        AttributeConfig attribute = findAttribute(getRayTracingModule(), CLOUD_MODE_ATTRIBUTE);
        if (attribute == null || Objects.equals(attribute.value, mode)) {
            return false;
        }
        assign(attribute, mode);

        // Ray marched clouds are noisy frame to frame; without accumulation across frames they visibly flicker, so
        // turning the clouds on turns this on with them.
        AttributeConfig temporal = findAttribute(getRayTracingModule(), CLOUD_TEMPORAL_ATTRIBUTE);
        if (temporal != null && mode.endsWith(".volumetric")) {
            assign(temporal, "render_pipeline.true");
        }
        return true;
    }

    public static String getCloudMode() {
        AttributeConfig attribute = findAttribute(getRayTracingModule(), CLOUD_MODE_ATTRIBUTE);
        return attribute == null ? null : attribute.value;
    }

    public static boolean supportsClouds() {
        return findAttribute(getRayTracingModule(), CLOUD_MODE_ATTRIBUTE) != null;
    }

    public static final String VOLUMETRIC_LIGHT_ATTRIBUTE =
        "render_pipeline.module.ray_tracing.attribute.volumetric_light_mode";
    private static final String VOLUMETRIC_LIGHT_ON = VOLUMETRIC_LIGHT_ATTRIBUTE + ".volumetric";
    private static final String VOLUMETRIC_LIGHT_OFF = VOLUMETRIC_LIGHT_ATTRIBUTE + ".vanilla";

    /**
     * Volumetric fog: the shader pack ray marches the air with a shadow ray per step, which is what gives light
     * shafts, and the biome fog becomes part of that medium. Returns true when the pipeline needs rebuilding.
     */
    public static boolean setVolumetricFog(boolean enabled) {
        AttributeConfig attribute = findAttribute(getRayTracingModule(), VOLUMETRIC_LIGHT_ATTRIBUTE);
        String value = enabled ? VOLUMETRIC_LIGHT_ON : VOLUMETRIC_LIGHT_OFF;
        if (attribute == null || Objects.equals(attribute.value, value)) {
            return false;
        }
        assign(attribute, value);
        return true;
    }

    public static boolean isVolumetricFog() {
        AttributeConfig attribute = findAttribute(getRayTracingModule(), VOLUMETRIC_LIGHT_ATTRIBUTE);
        return attribute != null && Objects.equals(attribute.value, VOLUMETRIC_LIGHT_ON);
    }

    public static boolean supportsVolumetricFog() {
        return findAttribute(getRayTracingModule(), VOLUMETRIC_LIGHT_ATTRIBUTE) != null;
    }

    /**
     * Whether block lights cast what their Bedrock MER maps show (EmissionTiles.uploadFromMer): by default they show
     * that glow and light the world with it; with either switch off the light levels' scale is kept.
     */
    public static boolean castsBedrockGlow() {
        return isShaderPackToggleOn(RtAttributes.BEDROCK_MATERIALS_ATTRIBUTE)
            && isShaderPackToggleOn(RtAttributes.BEDROCK_ORIGINAL_ATTRIBUTE);
    }

    /** The wind's strength for this frame (WorldUBO.windStrength): the slider, or 0 with wind off or not offered. */
    public static float windStrength() {
        if (!isShaderPackToggleOn(RtAttributes.WIND_ATTRIBUTE)) {
            return 0.0f;
        }
        String value = getShaderPackValue(RtAttributes.WIND_STRENGTH_ATTRIBUTE);
        try {
            return value == null ? 1.0f : Math.max(0.0f, Float.parseFloat(value.trim()));
        } catch (NumberFormatException notANumber) {
            return 1.0f;
        }
    }

    /**
     * Switches one of the shader pack's on/off attributes, such as motion blur or depth of field. Returns true when
     * it changed; nearly all of them are compiled into the pack, so the pipeline needs rebuilding (see
     * takeStructuralChange).
     */
    public static boolean setShaderPackToggle(String attributeName, boolean enabled) {
        AttributeConfig attribute = findAttribute(getRayTracingModule(), attributeName);
        String value = enabled ? "render_pipeline.true" : "render_pipeline.false";
        if (attribute == null || Objects.equals(attribute.value, value)) {
            return false;
        }
        logSettingChange(attributeName, attribute.value, value);
        assign(attribute, value);
        return true;
    }

    public static boolean isShaderPackToggleOn(String attributeName) {
        AttributeConfig attribute = findAttribute(getRayTracingModule(), attributeName);
        return attribute != null && Objects.equals(attribute.value, "render_pipeline.true");
    }

    /** A shader pack attribute's value as text, or null when the pack does not have it. */
    public static String getShaderPackValue(String attributeName) {
        AttributeConfig attribute = findAttribute(getRayTracingModule(), attributeName);
        return attribute == null ? null : attribute.value;
    }

    /**
     * Sets a shader pack attribute; true when it changed. Whether that takes a rebuild or reaches the renderer live
     * is takeStructuralChange's and pushRuntimeChanges' to say.
     */
    public static boolean setShaderPackValue(String attributeName, String value) {
        AttributeConfig attribute = findAttribute(getRayTracingModule(), attributeName);
        if (attribute == null || Objects.equals(attribute.value, value)) {
            return false;
        }
        logSettingChange(attributeName, attribute.value, value);
        assign(attribute, value);
        return true;
    }

    /** With debug logging, which setting asked for a rebuild: a value written over a different one. */
    private static void logSettingChange(String attribute, String from, String to) {
        if (Options.debugLogging) {
            RadianteClient.LOGGER.info("[settings] {}: {} -> {}", attribute, from, to);
        }
    }

    /**
     * Every setter's write, so the pipeline knows how the renderer has to learn of it: a runtime attribute's new
     * value waits for pushRuntimeChanges, any other change for a build. True when the value changed.
     */
    private static boolean assign(AttributeConfig attribute, String value) {
        if (Objects.equals(attribute.value, value)) {
            return false;
        }
        attribute.value = value;
        if (attribute.runtime) {
            INSTANCE.pendingRuntimeChanges.put(attribute.name, value);
        } else {
            INSTANCE.structuralChange = true;
        }
        return true;
    }

    /** Every attribute of a module of the active pipeline and its value, in order; empty without that module. */
    public static Map<String, String> moduleValues(String moduleName) {
        Map<String, String> values = new java.util.LinkedHashMap<>();
        for (Module module : INSTANCE.modules) {
            if (module != null && Objects.equals(module.name, moduleName) && module.attributeConfigs != null) {
                for (AttributeConfig attribute : module.attributeConfigs) {
                    if (attribute != null && attribute.name != null) {
                        values.put(attribute.name, attribute.value);
                    }
                }
            }
        }
        return values;
    }

    /** The declared type of a module's attribute ("bool", "int_range:1-8", "enum:a-b"...), or null without it. */
    public static String getModuleAttributeType(String moduleName, String attributeName) {
        for (Module module : INSTANCE.modules) {
            if (module != null && Objects.equals(module.name, moduleName)) {
                AttributeConfig attribute = findAttribute(module, attributeName);
                return attribute == null ? null : attribute.type;
            }
        }
        return null;
    }

    /** An attribute of any module by module and attribute name, or null when there is no such module or attribute. */
    public static String getModuleValue(String moduleName, String attributeName) {
        for (Module module : INSTANCE.modules) {
            if (module != null && Objects.equals(module.name, moduleName)) {
                AttributeConfig attribute = findAttribute(module, attributeName);
                return attribute == null ? null : attribute.value;
            }
        }
        return null;
    }

    /** Sets an attribute of any module by module and attribute name; true when it changed. */
    public static boolean setModuleValue(String moduleName, String attributeName, String value) {
        for (Module module : INSTANCE.modules) {
            if (module != null && Objects.equals(module.name, moduleName)) {
                AttributeConfig attribute = findAttribute(module, attributeName);
                if (attribute == null || Objects.equals(attribute.value, value)) {
                    return false;
                }
                logSettingChange(attributeName, attribute.value, value);
                assign(attribute, value);
                return true;
            }
        }
        return false;
    }

    /** A shader pack attribute as a whole number, or {@code fallback} when it is missing or not a number. */
    public static int getShaderPackInt(String attributeName, int fallback) {
        String value = getShaderPackValue(attributeName);
        try {
            return value == null ? fallback : (int) Float.parseFloat(value.trim());
        } catch (NumberFormatException notANumber) {
            return fallback;
        }
    }

    /** False when the active shader pack does not offer the attribute at all. */
    public static boolean supportsShaderPackToggle(String attributeName) {
        return findAttribute(getRayTracingModule(), attributeName) != null;
    }

    private static AttributeConfig findAttribute(Module module, String name) {
        if (module == null || module.attributeConfigs == null) {
            return null;
        }
        for (AttributeConfig attributeConfig : module.attributeConfigs) {
            if (attributeConfig != null && Objects.equals(attributeConfig.name, name)) {
                return attributeConfig;
            }
        }
        return null;
    }

    public static Module getRayTracingModule() {
        for (Module module : INSTANCE.modules) {
            if (module != null && Objects.equals(module.name, RAY_TRACING_MODULE_NAME)) {
                return module;
            }
        }
        return null;
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

    private static Path resolveShaderPackPath(String configuredPath) {
        String value = configuredPath == null ? "" : configuredPath.trim();
        Path shaderPackPath = value.isEmpty()
                ? Path.of(BUILT_IN_SHADER_PACK_PATH)
                : Path.of(value);
        if (!shaderPackPath.isAbsolute() && RadianteClient.radianceDir != null) {
            shaderPackPath = RadianteClient.radianceDir.resolve(shaderPackPath);
        }
        return shaderPackPath.toAbsolutePath().normalize();
    }

    private static Object loadShaderPackDocument(Path shaderPackPath, String fileName) {
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

    private static boolean readRequiresEmissionFromConfig(Path shaderPackPath) {
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

    private static boolean isShaderPackValueSelectable(String configuredPath) {
        return !readRequiresEmissionFromConfig(resolveShaderPackPath(configuredPath)) || Options.collectChunkEmission;
    }

    private static boolean fallbackUnavailableShaderPacks() {
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


    /** Why the last {@link #build()} failed, for the progress toast; null when it succeeded. */
    private static volatile String lastBuildError;

    public static String lastBuildError() {
        return lastBuildError;
    }

    public static void build() {
        com.g2806.radiante.client.render.EntityManager.onPipelineRebuilt();
        boolean built = false;
        lastBuildError = null;
        try {
            buildInternal();
            built = true;
        } catch (Exception e) {
            RadianteClient.LOGGER.error("Failed to build render pipeline.", e);
            lastBuildError = e.getMessage() != null && !e.getMessage().isBlank() ? e.getMessage()
                : e.getClass().getSimpleName();
            if (isPipelineCompatibilityFailure(e) && tryRebuildCompatiblePipeline(e)) {
                built = true;
                lastBuildError = null;
            }
        } finally {
            // A build carries every value, so nothing is left to push live or to build again for; hence cleared after it, not before. A failed build is tried again
            // with the next change, live or not, as it was before values could change live.
            INSTANCE.pendingRuntimeChanges.clear();
            INSTANCE.structuralChange = !built;
            if (built) {
                savePipeline();
            }
        }
    }

    /**
     * Whether a change since the last build needs the pipeline built anew: anything but a runtime attribute (a
     * switch compiled into the shader pack, another module's setting, the pack itself). It stays set until a build
     * succeeds: a wait screen replaced before its build ran (a disconnect) must not leave the change unapplied while
     * later changes are pushed live.
     */
    public static boolean takeStructuralChange() {
        return INSTANCE.structuralChange;
    }

    /**
     * Hands the runtime attributes changed since the last build to the renderer, which uses them from the next frame:
     * nothing is compiled and no wait screen is needed. False when the pipeline has to be built after all (the
     * renderer turned a value down); that build carries every value.
     */
    public static boolean pushRuntimeChanges() {
        boolean live = !INSTANCE.structuralChange;
        for (Map.Entry<String, String> change : INSTANCE.pendingRuntimeChanges.entrySet()) {
            if (!live) {
                break;
            }
            try {
                live = setRuntimeAttribute(change.getKey(), change.getValue());
            } catch (UnsatisfiedLinkError olderRenderer) {
                // A renderer from before live values; the build carries the value instead.
                live = false;
            }
            if (!live && Options.debugLogging) {
                RadianteClient.LOGGER.info("[settings] {} not applied live; rebuilding", change.getKey());
            }
        }
        INSTANCE.pendingRuntimeChanges.clear();
        if (!live) {
            INSTANCE.structuralChange = true;
        }
        return live;
    }

    private static void connectFogTransmission() {
        for (var entry : new ArrayList<>(INSTANCE.moduleConnections.entrySet())) {
            if (!entry.getKey().name.equals("fog_image")) { continue; }
            ImageConfig source = entry.getKey().owner.outputImageConfigs.stream()
                .filter(c -> c.name.equals("fog_transmission")).findFirst().orElse(null);
            if (source == null) { continue; }
            for (ImageConfig fogInput : entry.getValue()) {
                ImageConfig destination = fogInput.owner.inputImageConfigs.stream()
                    .filter(c -> c.name.equals("fog_transmission")).findFirst().orElse(null);
                if (destination != null && INSTANCE.moduleConnections.values().stream()
                    .noneMatch(inputs -> inputs.contains(destination))) {
                    connect(source, destination);
                }
            }
        }
    }

    /**
     * Starts compiling, in the background, what {@link #build()} with the current values will need, while the game
     * keeps drawing with the pipeline it has: the build that follows then takes a fraction of a second instead of
     * several seconds behind a wait screen. Nothing is applied or saved. False when nothing could be warmed (no ray
     * tracing pipeline running, another shader pack, a renderer from before prewarming): build the usual way then.
     * Calling it again while one runs restarts it with the newer values.
     */
    public static boolean prewarm() {
        try {
            return buildInternal(true);
        } catch (Exception | UnsatisfiedLinkError e) {
            // The build that follows reports what is wrong with the pipeline, and repairs it where it can.
            if (Options.debugLogging) {
                RadianteClient.LOGGER.info("[settings] no prewarm: {}", e.toString());
            }
            return false;
        }
    }

    /**
     * Tells the shader pack whether NRD denoises this pipeline (NRD_SIGNALS_ATTRIBUTE): with Ray Reconstruction the
     * path tracer hands over one picture, with NRD the light in signals NRD can denoise well. Part of every build, so
     * a preset switch, a custom pipeline or a pipeline file from an older session cannot leave it wrong.
     */
    private static void syncDenoiserSignals() {
        boolean nrd = false;
        for (Module module : INSTANCE.modules) {
            nrd |= module != null && Objects.equals(module.name, NRD_MODULE_NAME);
        }
        setShaderPackValue(RtAttributes.NRD_SIGNALS_ATTRIBUTE, nrd ? "render_pipeline.true" : "render_pipeline.false");
    }

    private static void buildInternal() {
        buildInternal(false);
    }

    /** With prewarmOnly, hands the pipeline to {@link #prewarmNative} instead and returns what it says. */
    private static boolean buildInternal(boolean prewarmOnly) {
        connectFogTransmission();
        fallbackUnavailableShaderPacks();
        syncDenoiserSignals();
        getModuleAttributes();

        Map<ImageConfig, ImageConfig> dstTosrcMap = new HashMap<>();
        for (Map.Entry<ImageConfig, List<ImageConfig>> entry : INSTANCE.moduleConnections.entrySet()) {
            ImageConfig source = entry.getKey();
            for (ImageConfig dest : entry.getValue()) {
                if (dstTosrcMap.containsKey(dest)) {
                    throw new RuntimeException(
                            "Input config '" + dest.name + "' has multiple sources connected!");
                }
                dstTosrcMap.put(dest, source);
            }
        }

        ImageConfig finalOutputConfig = null;
        Module finalModule = null;

        for (Module module : INSTANCE.modules) {
            for (ImageConfig conf : module.outputImageConfigs) {
                if (conf.finalOutput) {
                    if (finalOutputConfig != null) {
                        throw new RuntimeException(
                                "Multiple final outputs detected! Only one allows.");
                    }
                    finalOutputConfig = conf;
                    finalModule = module;
                }
            }
        }

        if (finalOutputConfig == null) {
            throw new RuntimeException("No final output configured.");
        }

        List<Module> sortedModules = new ArrayList<>();
        Set<Module> visited = new HashSet<>();
        Set<Module> visiting = new HashSet<>();

        topologicalSort(finalModule, dstTosrcMap, visited, visiting, sortedModules);

        for (Module m : sortedModules) {
            for (ImageConfig inputConf : m.inputImageConfigs) {
                if (!dstTosrcMap.containsKey(inputConf)) {
                    throw new RuntimeException(
                            "Module '" + m.name + "' has unconnected input: " + inputConf.name);
                }
            }
        }

        List<Integer> imageFormatList = new ArrayList<>();
        Map<ImageConfig, Integer> configToImageIdMap = new HashMap<>();

        int finalFmtId = VulkanConstants.VkFormat.getVkFormatByName(finalOutputConfig.format);
        imageFormatList.add(finalFmtId);
        configToImageIdMap.put(finalOutputConfig, 0);

        for (Module module : sortedModules) {
            for (ImageConfig outConfig : module.outputImageConfigs) {
                int imgId;
                if (configToImageIdMap.containsKey(outConfig)) {
                    imgId = configToImageIdMap.get(outConfig);

                    if (imgId != 0) {
                        throw new RuntimeException();
                    }
                } else {
                    imgId = imageFormatList.size();
                    imageFormatList.add(
                            VulkanConstants.VkFormat.getVkFormatByName(outConfig.format));
                    configToImageIdMap.put(outConfig, imgId);
                }

                List<ImageConfig> connectedInputs = INSTANCE.moduleConnections.get(outConfig);
                if (connectedInputs != null && !connectedInputs.isEmpty()) {
                    for (ImageConfig inputConf : connectedInputs) {
                        configToImageIdMap.put(inputConf, imgId);
                    }
                }
            }
        }

        List<List<AttributeConfig>> moduleAttributes = new ArrayList<>();
        for (Module m : sortedModules) {
            List<AttributeConfig> attributes = m.attributeConfigs != null ? m.attributeConfigs : new ArrayList<>();
            // The debug view travels with the build only; it is never part of the module's saved values.
            if (Objects.equals(m.name, RAY_TRACING_MODULE_NAME) && !DEBUG_VIEW_OFF.equals(debugView)) {
                attributes = new ArrayList<>(attributes);
                AttributeConfig view = new AttributeConfig();
                view.name = DEBUG_VIEW_ATTRIBUTE;
                view.type = "string";
                view.value = debugView;
                attributes.add(view);
            }
            moduleAttributes.add(attributes);
        }

        if (prewarmOnly) {
            return NativePipelineParams.buildNative(sortedModules, imageFormatList, configToImageIdMap,
                moduleAttributes, true);
        }
        NativePipelineParams.buildNative(sortedModules, imageFormatList, configToImageIdMap, moduleAttributes, false);
        publishFrameGenerationImages(sortedModules, configToImageIdMap);
        return true;
    }

    /**
     * Tells the renderer which pipeline images frame generation reads. Upscaled variants win, so frame generation
     * works at the presented resolution when DLSS, FSR or XeSS is in the pipeline.
     */
    private static void publishFrameGenerationImages(List<Module> modules, Map<ImageConfig, Integer> imageIds) {
        int depth = -1;
        int motionVectors = -1;
        for (Module module : modules) {
            if (module == null || module.outputImageConfigs == null) {
                continue;
            }
            for (ImageConfig config : module.outputImageConfigs) {
                Integer id = imageIds.get(config);
                if (id == null || config.name == null) {
                    continue;
                }
                switch (config.name) {
                    case "first_hit_depth" -> depth = depth < 0 ? id : depth;
                    case "motion_vector" -> motionVectors = motionVectors < 0 ? id : motionVectors;
                    case "upscaled_first_hit_depth" -> depth = id;
                    case "upscaled_motion_vector" -> motionVectors = id;
                    default -> {
                    }
                }
            }
        }

        RendererProxy.setFrameGenerationImages(depth, motionVectors);
    }

    private static boolean isPipelineCompatibilityFailure(Exception e) {
        if (e == null) {
            return false;
        }

        String message = e.getMessage();
        if (message == null || message.isBlank()) {
            return false;
        }

        return message.contains("has unconnected input")
                || message.contains("has multiple sources connected")
                || message.contains("No final output configured")
                || message.contains("Multiple final outputs detected")
                || message.contains("Cycle detected involving module")
                || message.contains("Module with name")
                || message.contains("Unsupported preset");
    }

    private static boolean tryRebuildCompatiblePipeline(Exception cause) {
        PipelineConfigStorage storage = PipelineStore.loadConfigStorage();
        String requestedPreset = INSTANCE.activePresetName;
        if (storage != null && storage.presetName != null && !storage.presetName.isBlank()) {
            requestedPreset = storage.presetName;
        }

        String presetToBuild = processPresetName(requestedPreset);
        if (presetToBuild == null) {
            presetToBuild = getBestAvailablePresetName();
        }
        if (presetToBuild == null) {
            RadianteClient.LOGGER.error("Failed to rebuild an incompatible pipeline: no preset is available.", cause);
            return false;
        }

        try {
            RadianteClient.LOGGER.warn(
                    "Stored pipeline is incompatible with current module definitions. Rebuilding preset: {}",
                    presetToBuild);

            INSTANCE.mode = PipelineMode.PRESET;
            assemblePreset(presetToBuild);

            if (storage != null
                    && PipelineMode.fromString(storage.mode) == PipelineMode.PRESET
                    && Objects.equals(processPresetName(storage.presetName), INSTANCE.activePresetName)) {
                PipelineStore.applyPresetModuleOverrides(storage.presetModules);
                normaliseRayReconstruction();
            }

            buildInternal();
            return true;
        } catch (Exception rebuildError) {
            RadianteClient.LOGGER.error("Automatic pipeline rebuild failed.", rebuildError);
            return false;
        }
    }

    private static void topologicalSort(Module current,
                                        Map<ImageConfig, ImageConfig> inputToSourceMap, Set<Module> visited, Set<Module> visiting,
                                        List<Module> result) {
        if (visiting.contains(current)) {
            throw new RuntimeException("Cycle detected involving module: " + current.name);
        }
        if (visited.contains(current)) {
            return;
        }

        visiting.add(current);

        for (ImageConfig inputConf : current.inputImageConfigs) {
            ImageConfig sourceOutput = inputToSourceMap.get(inputConf);
            if (sourceOutput != null) {
                Module dependencyModule = sourceOutput.owner;
                topologicalSort(dependencyModule, inputToSourceMap, visited, visiting, result);
            }
        }

        visiting.remove(current);
        visited.add(current);

        result.add(current);
    }

    public static void assembleDefault() {
        String defaultPresetName = processPresetName(Presets.RT_DLSSRR.key);
        if (defaultPresetName == null) {
            assembleBestAvailablePreset("Default preset is unavailable.");
            return;
        }
        PresetAssembly.assemblePresetByKeyInternal(defaultPresetName);
    }

    public static void assembleNRDFSR() {
        if (!isPresetAvailable(Presets.RT_NRD_FSR.key)) {
            assembleBestAvailablePreset("NRD+FSR preset is unavailable.");
            return;
        }
        PresetAssembly.assembleNRDFSRInternal();
    }

    public static native void buildNative(long params);

    public static native void collectNativeModules();

    public static native boolean isNativeModuleAvailable(String name);

    public static native String getAttributes(String name, String[] attributes, String language);

    /**
     * Loads and compiles a shader pack as a build would, without touching the running pipeline: its configs.json,
     * the pack staged over its base, and every shader of both stages with these ray tracing attributes (name, value,
     * name, value...). It works in folders of its own, so it may run on any thread while frames are drawn. JSON:
     * {@code {"ok": bool, "error": text, "errors": [text], "shaders": n, "compiled": n}}.
     */
    public static native String validateShaderPackNative(String packPath, String[] attributes, String language);

    /**
     * The ray tracing module's attributes as {@link #validateShaderPackNative} takes them, with {@code packPath} as the
     * pack: the values in use when that is the active pack, else the module's own settings (the pack's attributes then
     * get their defaults). Reads the pipeline, so call it on the client thread.
     */
    public static String[] shaderPackValidationAttributes(String packPath) {
        Module module = getRayTracingModule();
        boolean active = ShaderPackLibrary.samePack(packPath, shaderPackPath());
        List<AttributeConfig> source = module == null ? List.of()
            : active || module.staticAttributeConfigs == null ? module.attributeConfigs : module.staticAttributeConfigs;
        List<String> values = new ArrayList<>();
        if (source != null) {
            for (AttributeConfig attribute : source) {
                if (attribute == null || attribute.name == null
                    || attribute.name.equals(RAY_TRACING_SHADER_PACK_PATH_ATTRIBUTE)) {
                    continue;
                }
                values.add(attribute.name);
                values.add(attribute.value != null ? attribute.value : "");
            }
        }
        values.add(RAY_TRACING_SHADER_PACK_PATH_ATTRIBUTE);
        values.add(packPath);
        if (active && !DEBUG_VIEW_OFF.equals(debugView)) {
            values.add(DEBUG_VIEW_ATTRIBUTE);
            values.add(debugView);
        }
        return values.toArray(String[]::new);
    }

    /** The language code the renderer reads pack names in, for {@link #validateShaderPackNative}. */
    public static String languageCode() {
        return ModuleAttributes.getCurrentLanguageCode();
    }

    /**
     * Asks for the pipeline to be built again although no value changed: the active folder pack's files did
     * ({@code ShaderPackHotReload}). {@link #build()} reads the pack afresh.
     */
    public static void requestRebuild() {
        INSTANCE.structuralChange = true;
    }

    /**
     * Shows a debug view as the picture instead of the lit world: one the shader pack lists in its configs.json
     * ({@code debug_views}), such as Radiante PT's guide images ("albedo", "normal"...), which packs that extend it
     * inherit. Not saved: a session's debugging never outlives it. The renderer reads it as an attribute of the ray
     * tracing module that no file lists.
     */
    public static final String DEBUG_VIEW_ATTRIBUTE = "render_pipeline.module.ray_tracing.attribute.debug_view";
    public static final String DEBUG_VIEW_OFF = "off";
    private static String debugView = DEBUG_VIEW_OFF;
    /** The active pack's debug views, as the renderer read them from it at the last build. */
    static volatile List<String> shaderPackDebugViews = List.of();

    public static String debugView() {
        return debugView;
    }

    /** Sets the debug view; true when it changed, which takes a build. */
    public static boolean setDebugView(String view) {
        String value = view == null || view.isBlank() ? DEBUG_VIEW_OFF : view;
        if (value.equals(debugView)) {
            return false;
        }
        logSettingChange(DEBUG_VIEW_ATTRIBUTE, debugView, value);
        debugView = value;
        INSTANCE.structuralChange = true;
        return true;
    }

    /** Every view {@link #setDebugView} takes for the active pack, in the pack's order. */
    public static List<String> availableDebugViews() {
        return shaderPackDebugViews;
    }

    /**
     * Sets a runtime attribute in the running renderer, from the next frame. False when it was not applied (no
     * pipeline yet, or the attribute is compiled into the shader pack): the value then needs a full build.
     */
    public static native boolean setRuntimeAttribute(String name, String value);

    /**
     * Starts compiling, in the background, the shaders and ray tracing pipelines the described pipeline will need
     * (the same block {@link #buildNative(long)} takes), while frames keep using the current pipeline. False when
     * there is nothing it can warm (no ray tracing pipeline running, another shader pack, SHaRC switched).
     */
    public static native boolean prewarmNative(long params);

    /**
     * Writes the newest prewarm's state into {@code out} (length 5): {@link #PREWARM_IDLE} and the other states, then
     * shaders compiled, shaders to compile, pipelines built and pipelines to build.
     */
    public static native void prewarmStatus(int[] out);

    /** GPU time of the world pipeline over the last 60 frames, with its main stages; empty before the first. */
    public static native String gpuStats();

    /** TLAS instances, entity BLAS built last frame, the renderer's device memory used and its budget in MB. */
    public static native long[] sceneStats();

    public static final int PREWARM_IDLE = 0;
    public static final int PREWARM_RUNNING = 1;
    public static final int PREWARM_DONE = 2;
    public static final int PREWARM_FAILED = 3;
    public static final int PREWARM_CANCELLED = 4;


    public static void getModuleAttributes() {
        for (Module module : INSTANCE.modules) {
            ModuleAttributes.getModuleAttributes(module);
        }
    }

    public String getActivePresetName() {
        return activePresetName;
    }

    /** Assembles a preset, keeping the module settings it shares with the current one; the caller builds it. */
    public static void switchToPresetMode(String presetName) {
        List<PresetStoredModule> carryOverModules = PipelineStore.capturePresetModules();

        INSTANCE.mode = PipelineMode.PRESET;
        assemblePreset(processPresetName(presetName));

        PipelineConfigStorage storage = PipelineStore.loadConfigStorage();
        if (storage != null && Objects.equals(storage.mode, PipelineMode.PRESET.name())
                && Objects.equals(storage.presetName, INSTANCE.activePresetName)) {
            PipelineStore.applyPresetModuleOverrides(storage.presetModules);
            normaliseRayReconstruction();
        }

        PipelineStore.applyPresetModuleOverrides(carryOverModules);
        normaliseRayReconstruction();
    }

    public static void assemblePreset(String presetName) {
        String processedPresetName = processPresetName(presetName);
        if (processedPresetName == null) {
            assembleBestAvailablePreset("Requested preset is unavailable.");
            return;
        }

        PresetAssembly.assemblePresetByKeyInternal(processedPresetName);
    }

    public static String processPresetName(String presetName) {
        String requestedPresetName = presetName;
        if (requestedPresetName == null || requestedPresetName.isEmpty()) {
            requestedPresetName = Presets.RT_DLSSRR.key;
        }

        // Checked against the enum rather than a hand written list: a preset missing from that list was silently
        // rewritten to DLSS_RR, which is how a newly added one looks like it was never selected at all.
        boolean known = false;
        for (Presets preset : Presets.values()) {
            if (Objects.equals(requestedPresetName, preset.key)) {
                known = true;
                break;
            }
        }
        if (!known) {
            requestedPresetName = Presets.RT_DLSSRR.key;
        }

        if (isPresetAvailable(requestedPresetName)) {
            return requestedPresetName;
        }

        return getBestAvailablePresetName();
    }

    /**
     * Which NGX feature the DLSS module creates is decided by the preset, not by the player and not by whatever a
     * previous pipeline happened to save. Stored attribute overrides are applied after a preset is assembled, so
     * without this a pipeline saved under ray reconstruction would drag it into the preset that must not have it -
     * DLSS would then denoise a picture NRD had already cleaned.
     */
    private static void normaliseRayReconstruction() {
        boolean wanted = Objects.equals(Presets.RT_DLSSRR.key, INSTANCE.activePresetName);
        for (Module module : INSTANCE.modules) {
            if (module != null && Objects.equals(module.name, DLSS_MODULE_NAME)) {
                setRayReconstructionOn(module, wanted);
            }
        }
    }

    public static void savePipeline() {
        PipelineStore.savePipeline();
    }

    /**
     * Raised whenever a release changes the defaults of stored pipeline or shader pack settings (the tuning towards
     * Bedrock RTX, for one). Settings saved under an older one are forgotten once, so an update brings the new
     * defaults instead of the values a previous version wrote down; see Options.settingsDefaultsVersion.
     */
    public static final int SETTINGS_DEFAULTS_VERSION = 3;

    /** Forgets settings stored under older defaults, once; called before the pipeline is first loaded. */
    public static void migrateStoredDefaults() {
        if (Options.settingsDefaultsVersion >= SETTINGS_DEFAULTS_VERSION) {
            return;
        }
        PipelineStore.forgetStoredSettings();
        Options.settingsDefaultsVersion = SETTINGS_DEFAULTS_VERSION;
        Options.overwriteConfig();
    }

    /** Every pipeline and shader pack setting back to a fresh install's, and the pipeline rebuilt with them. */
    public static void resetToDefaults() {
        PipelineStore.forgetStoredSettings();
        loadPipeline();
    }

    public static void loadPipeline() {
        PipelineConfigStorage storage = PipelineStore.loadConfigStorage();

        if (storage == null) {
            INSTANCE.mode = PipelineMode.PRESET;
            assembleDefault();
            savePipeline();
            return;
        }

        PipelineMode loadedMode = PipelineMode.fromString(storage.mode);
        INSTANCE.mode = loadedMode;
        if (storage.presetName != null && !storage.presetName.isEmpty()) {
            INSTANCE.activePresetName = processPresetName(storage.presetName);
        }

        if (loadedMode == PipelineMode.PRESET) {
            assemblePreset(INSTANCE.activePresetName);
            PipelineStore.applyPresetModuleOverrides(storage.presetModules);
            normaliseRayReconstruction();
            build();
            return;
        }

        if (storage.pipeline != null) {
            PipelineStore.applyPipelineStorage(storage.pipeline);
            build();
            return;
        }

        // fallback
        assembleDefault();
        build();
    }
    public enum PipelineMode {
        PIPELINE,
        PRESET;

        static PipelineMode fromString(String s) {
            if (s == null) {
                return PRESET;
            }
            try {
                return PipelineMode.valueOf(s.toUpperCase(Locale.ROOT));
            } catch (Exception ignored) {
                return PRESET;
            }
        }
    }

    public static class PipelineConfigStorage {
        // new format
        public String mode;
        public String presetName;
        public PipelineStorage pipeline;
        public List<PresetStoredModule> presetModules;

        public List<StoredModule> modules;
        public List<StoredConnection> moduleConnections;
        public String finalOutputModuleId;
        public String finalOutputImageName;

        void migrateLegacyFields() {
            if (pipeline != null) {
                return;
            }
            if (modules == null || modules.isEmpty()) {
                return;
            }

            PipelineStorage ps = new PipelineStorage();
            ps.modules = modules;
            ps.moduleConnections = moduleConnections != null ? moduleConnections : new ArrayList<>();
            ps.finalOutputModuleId = finalOutputModuleId;
            ps.finalOutputImageName = finalOutputImageName;

            pipeline = ps;

            // default mode for legacy, pipeline
            if (mode == null) {
                mode = "PIPELINE";
            }
        }
    }

    public static class PresetStoredModule {
        public String entryName;
        public List<StoredAttribute> attributes;
    }

    public static class PipelineStorage {
        public List<StoredModule> modules;
        public List<StoredConnection> moduleConnections;
        public String finalOutputModuleId;
        public String finalOutputImageName;
    }

    public static class StoredModule {
        public String id;
        public String entryName;
        public double x;
        public double y;
        public List<StoredAttribute> attributes;
    }

    public static class StoredAttribute {
        public String type;
        public String name;
        public String value;
    }

    public static class StoredConnection {
        public String srcModuleId;
        public String srcImageName;
        public String dstModuleId;
        public String dstImageName;
    }
}
