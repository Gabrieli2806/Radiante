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
import static com.g2806.radiante.client.pipeline.PipelineSettings.*;
import static com.g2806.radiante.client.pipeline.PipelineBuild.*;
import static com.g2806.radiante.client.pipeline.PipelineShaderPack.*;

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
    static final String SHADER_PACK_CONFIG_FILE = "configs.json";
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
    final Map<String, String> pendingRuntimeChanges = new LinkedHashMap<>();
    /** Something changed since the last build that only a build applies (see takeStructuralChange). */
    boolean structuralChange;

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

    static boolean areModulesAvailable(String... moduleNames) {
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
    static boolean rayReconstructionAvailable() {
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
    static String getBestAvailablePresetName() {
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

    static void assembleBestAvailablePreset(String reason) {
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

    static AttributeConfig findAttribute(Module module, String name) {
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
    static String debugView = DEBUG_VIEW_OFF;
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
    static void normaliseRayReconstruction() {
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
