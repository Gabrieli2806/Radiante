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
import static com.g2806.radiante.client.pipeline.PipelineShaderPack.*;

/**
 * Building the pipeline: putting the modules in order, handing them to the native renderer, warming the shaders up
 * ahead of a build, pushing the values that can change live, and falling back to a pipeline this machine can run
 * when the stored one cannot be built.
 *
 * <p>These were part of {@code Pipeline}, which keeps the modules and connections themselves and the native methods.
 */
public final class PipelineBuild {

    private PipelineBuild() {
    }

    /** Why the last {@link #build()} failed, for the progress toast; null when it succeeded. */
    static volatile String lastBuildError;

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

    static void connectFogTransmission() {
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
    static void syncDenoiserSignals() {
        boolean nrd = false;
        for (Module module : INSTANCE.modules) {
            nrd |= module != null && Objects.equals(module.name, NRD_MODULE_NAME);
        }
        setShaderPackValue(RtAttributes.NRD_SIGNALS_ATTRIBUTE, nrd ? "render_pipeline.true" : "render_pipeline.false");
    }

    static void buildInternal() {
        buildInternal(false);
    }

    /** With prewarmOnly, hands the pipeline to {@link #prewarmNative} instead and returns what it says. */
    static boolean buildInternal(boolean prewarmOnly) {
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
    static void publishFrameGenerationImages(List<Module> modules, Map<ImageConfig, Integer> imageIds) {
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

    static boolean isPipelineCompatibilityFailure(Exception e) {
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

    static boolean tryRebuildCompatiblePipeline(Exception cause) {
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

    static void topologicalSort(Module current,
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
}
