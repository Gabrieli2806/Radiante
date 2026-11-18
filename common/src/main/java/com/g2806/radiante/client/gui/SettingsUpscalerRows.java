package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.download.UpscalerDownloads;
import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.pipeline.Pipeline;
import com.g2806.radiante.client.pipeline.PipelineSettings;
import com.g2806.radiante.client.pipeline.Presets;
import com.g2806.radiante.client.pipeline.RtAttributes;
import com.g2806.radiante.client.render.FrameGeneration;
import com.g2806.radiante.client.render.OutputResolution;
import com.g2806.radiante.client.render.RadianteRenderer;
import com.mojang.serialization.Codec;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import net.minecraft.client.OptionInstance;
import net.minecraft.network.chat.Component;

/**
 * Pipeline, upscaler, resolution, frame-generation and ReSTIR option builders for {@link SettingsRows}.
 */
abstract class SettingsUpscalerRows extends SettingsChoices {

    boolean refreshPending;

    SettingsUpscalerRows(Component title, net.minecraft.client.Options options) {
        super(title, options);
    }

    /**
     * Lays the screen out again with the choices made so far, after one that adds or removes rows
     * (RadianteOptionsScreen, which makes the new screen).
     */
    abstract void reopenWithSameChoices();

    /** A setting the quality level covers was changed by hand: the level shown above it has to follow. */
    void refreshQualityLater() {
        // Once the mouse is let go: laying the screen out again under a slider being dragged dropped the drag, so
        // those sliders only moved by clicking where the value should be.
        this.refreshPending = true;
    }

    static <T> OptionInstance.TooltipSupplier<T> tooltip(String key) {
        return OptionInstance.cachedConstantTooltip(Component.translatable(key + ".tooltip"));
    }

    OptionInstance<Presets> presetOption() {
        String activeKey = Pipeline.INSTANCE.getActivePresetName();
        if (this.pendingRayReconstruction == null) {
            this.pendingRayReconstruction = !Objects.equals(activeKey, Presets.RT_NRD_DLSS.key);
        }
        // DLSS without Ray Reconstruction is not an entry of its own: it is the DLSS entry with that option off.
        if (Objects.equals(activeKey, Presets.RT_NRD_DLSS.key)) {
            activeKey = Presets.RT_DLSSRR.key;
        }
        if (this.pendingPreset == Presets.RT_NRD_DLSS) {
            this.pendingPreset = Presets.RT_DLSSRR;
            this.pendingRayReconstruction = false;
        }
        List<Presets> available = new ArrayList<>();
        for (Presets preset : Presets.values()) {
            if (preset != Presets.RT_NRD_DLSS && entryAvailable(preset)) {
                available.add(preset);
            }
        }
        // DLSS and XeSS are downloaded on request: they are listed even when missing, and picking one offers it.
        List<Presets> listed = new ArrayList<>(available);
        for (Presets preset : Presets.values()) {
            UpscalerDownloads.Component needed = downloadFor(preset);
            if (preset != Presets.RT_NRD_DLSS && !listed.contains(preset) && needed != null
                && offersDownload(needed)) {
                listed.add(preset);
            }
        }
        if (available.isEmpty()) {
            return null;
        }

        Presets active = available.getFirst();
        for (Presets preset : available) {
            if (Objects.equals(preset.key, activeKey)) {
                active = preset;
            }
        }
        // The list is rebuilt whenever the preset changes, so a choice already made has to survive that.
        if (this.pendingPreset != null && available.contains(this.pendingPreset)) {
            active = this.pendingPreset;
        }
        this.pendingPreset = active;
        if (this.shownPreset != null && listed.contains(this.shownPreset) && !available.contains(this.shownPreset)) {
            active = this.shownPreset;
        } else {
            this.shownPreset = null;
        }

        return new OptionInstance<>("options.radiante.preset", tooltip("options.radiante.preset"),
            (caption, value) -> available.contains(value) ? Component.translatable(value.key)
                : Component.translatable(value.key).append(" ").append(Component.translatable(
                    UpscalerDownloads.isInstalled(downloadFor(value)) ? "options.radiante.download.restart_needed"
                        : "options.radiante.download.needed")),
            new OptionInstance.Enum<>(listed, Codec.STRING.xmap(Presets::valueOf, Presets::name)), active,
            value -> {
                if (!available.contains(value)) {
                    // Not usable yet: shown with Install (or Restart) at the bottom; the applied choice stays.
                    this.shownPreset = value;
                    if (this.minecraft != null) {
                        this.minecraft.execute(this::reopenWithSameChoices);
                    }
                    return;
                }
                boolean changed = value != this.pendingPreset || this.shownPreset != null;
                this.shownPreset = null;
                this.pendingPreset = value;
                // Which of the settings below make sense depends on this one, so the screen has to be laid out
                // again. Doing it straight away would edit the widget list that is handling this very click.
                if (changed && this.minecraft != null) {
                    this.minecraft.execute(this::reopenWithSameChoices);
                }
            });
    }

    /**
     * Ray tracing on or off, at once and exactly as the key binding does it (RadianteClient.toggleRayTracing): not
     * held for Apply, since the world is handed to the other renderer straight away.
     */
    OptionInstance<Boolean> rayTracingToggle() {
        if (!RadianteRenderer.isActive()) {
            return null;
        }
        return OptionInstance.createBoolean("options.radiante.ray_tracing",
            OptionInstance.cachedConstantTooltip(Component.translatable("options.radiante.ray_tracing.tooltip")),
            Options.rayTracingEnabled, value -> {
                if (value != Options.rayTracingEnabled && this.minecraft != null) {
                    RadianteClient.toggleRayTracing(this.minecraft);
                }
            });
    }

    /** The download a preset needs, or null when it needs none. */
    static UpscalerDownloads.Component downloadFor(Presets preset) {
        return switch (preset) {
            case RT_DLSSRR, RT_NRD_DLSS -> UpscalerDownloads.Component.DLSS;
            case RT_NRD_XESS -> UpscalerDownloads.Component.XESS;
            default -> null;
        };
    }

    /** DLSS only on NVIDIA GPUs; XeSS wherever its runtime exists (Windows). */
    static boolean offersDownload(UpscalerDownloads.Component component) {
        if (!component.offeredHere()) {
            return false;
        }
        return component != UpscalerDownloads.Component.DLSS || RadianteRenderer.isNvidiaGpu();
    }

    /**
     * Whether Bedrock's own data files (Content/data/ray_tracing) can be found, where the renderer looks for them: in
     * radiante/bedrock, or in the Bedrock install. The Bedrock atmosphere and caustics need them and fall back
     * without.
     */
    static boolean bedrockFilesPresent(String... names) {
        java.nio.file.Path local = RadianteClient.radianceDir == null ? null : RadianteClient.radianceDir.resolve("bedrock");
        java.nio.file.Path install = java.nio.file.Path.of("C:/XboxGames/Minecraft for Windows/Content/data/ray_tracing");
        for (String name : names) {
            boolean found = (local != null && java.nio.file.Files.isRegularFile(local.resolve(name)))
                || java.nio.file.Files.isRegularFile(install.resolve(name));
            if (!found) {
                return false;
            }
        }
        return true;
    }

    /** Clouds come from the shader pack, which ray marches them, so nothing has to be submitted for them. */
    OptionInstance<String> cloudModeOption() {
        if (!PipelineSettings.supportsClouds()) {
            return null;
        }

        // A choice carried over from the previous screen (a quality level, say) is kept; only a fresh screen reads
        // the pipeline. Reading it every time undid the quality level's clouds, so it always came back as Custom.
        if (this.pendingCloudMode == null) {
            String current = PipelineSettings.getCloudMode();
            this.pendingCloudMode = current != null && PipelineSettings.CLOUD_MODES.contains(current) ? current
                : PipelineSettings.CLOUD_MODES.get(0);
        }

        return new OptionInstance<>("options.radiante.cloud_mode", tooltip("options.radiante.cloud_mode"),
            (caption, value) -> Component.translatable(value),
            new OptionInstance.Enum<>(PipelineSettings.CLOUD_MODES, Codec.STRING), this.pendingCloudMode,
            value -> {
                this.pendingCloudMode = value;
                refreshQualityLater();
            });
    }

    /**
     * Ray Reconstruction is DLSS's own denoiser; off, NRD denoises and DLSS only upscales. Offered only with DLSS,
     * and only where the NRD pipeline that replaces it can be built.
     */
    OptionInstance<Boolean> rayReconstructionOption() {
        // Not where the GPU runs DLSS without Ray Reconstruction only: there is nothing to turn on.
        if (!usingDlss() || !Pipeline.isPresetAvailable(Presets.RT_NRD_DLSS.key)
            || !Pipeline.isPresetAvailable(Presets.RT_DLSSRR.key)) {
            return null;
        }
        if (this.pendingRayReconstruction == null) {
            this.pendingRayReconstruction = !Objects.equals(Pipeline.INSTANCE.getActivePresetName(),
                Presets.RT_NRD_DLSS.key);
        }
        return OptionInstance.createBoolean("options.radiante.ray_reconstruction",
            tooltip("options.radiante.ray_reconstruction"), this.pendingRayReconstruction,
            value -> this.pendingRayReconstruction = value);
    }

    OptionInstance<String> dlssModeOption() {
        if (!usingDlss()) {
            return null;
        }

        if (this.pendingDlssMode == null) {
            String current = PipelineSettings.getDlssMode();
            this.pendingDlssMode = current != null && PipelineSettings.DLSS_MODES.contains(current) ? current
                : "render_pipeline.module.dlss.attribute.mode.ultra_performance";
        }

        loadRenderScale();
        return new OptionInstance<>("options.radiante.dlss_mode", tooltip("options.radiante.dlss_mode"),
            (caption, value) -> Component.translatable(value),
            new OptionInstance.Enum<>(withCustom(PipelineSettings.DLSS_MODES), Codec.STRING),
            this.pendingCustomScale ? CUSTOM_MODE : this.pendingDlssMode, value -> {
                if (!chooseCustomMode(value)) {
                    this.pendingDlssMode = value;
                }
                refreshQualityLater();
            });
    }

    /**
     * Frame generation: DLSS on NVIDIA cards that support it, FSR on the others. It runs in the renderer, so it
     * switches on and off without a restart. Not offered with HDR output, which presents its own way.
     */
    OptionInstance<Integer> frameGenerationOption() {
        int max = FrameGeneration.maxGeneratedFrames();
        String backend = FrameGeneration.backendName();
        if (FrameGeneration.hasBackendChoice() && this.pendingFrameGenerationBackend != Options.frameGenerationBackend) {
            // Not applied yet: show the range and name of the one picked.
            boolean fsr = this.pendingFrameGenerationBackend == 2;
            max = fsr ? 1 : FrameGeneration.dlssMaxGeneratedFrames();
            backend = fsr ? "FSR" : "DLSS";
        }
        if (max <= 0 || staged("hdrOutput", Options.hdrOutput)) {
            return null;
        }

        List<Integer> values = new ArrayList<>();
        for (int frames = 0; frames <= max; frames++) {
            values.add(frames);
        }

        String backendName = backend;
        return new OptionInstance<>("options.radiante.frame_generation",
            OptionInstance.cachedConstantTooltip(Component.translatable("options.radiante.frame_generation.tooltip",
                backendName == null ? "" : backendName)),
            (caption, value) -> value == 0 ? Component.translatable("options.off")
                : Component.literal((value + 1) + "x" + (backendName == null ? "" : " (" + backendName + ")")),
            new OptionInstance.Enum<>(values, Codec.intRange(0, max)),
            Math.min(this.pendingGeneratedFrames, max), value -> {
                this.pendingGeneratedFrames = value;
                refreshQualityLater();
            });
    }

    /** ReSTIR for block lights, where the shader pack has it. */
    OptionInstance<Boolean> restirOption() {
        if (!PipelineSettings.supportsShaderPackToggle(RtAttributes.RESTIR_ATTRIBUTE)) {
            return null;
        }
        if (this.pendingRestir == null) {
            this.pendingRestir = PipelineSettings.isShaderPackToggleOn(RtAttributes.RESTIR_ATTRIBUTE);
        }
        return OptionInstance.createBoolean("options.radiante.restir", tooltip("options.radiante.restir"),
            this.pendingRestir, value -> this.pendingRestir = value);
    }

    /** ReSTIR GI for the first bounce of light, where the shader pack has it. */
    OptionInstance<Boolean> restirGiOption() {
        if (!PipelineSettings.supportsShaderPackToggle(RtAttributes.RESTIR_GI_ATTRIBUTE)) {
            return null;
        }
        if (this.pendingRestirGi == null) {
            this.pendingRestirGi = PipelineSettings.isShaderPackToggleOn(RtAttributes.RESTIR_GI_ATTRIBUTE);
        }
        return OptionInstance.createBoolean("options.radiante.restir_gi", tooltip("options.radiante.restir_gi"),
            this.pendingRestirGi, value -> this.pendingRestirGi = value);
    }

    /** DLSS or FSR frame generation, offered where the GPU runs both (FSR works next to any upscaler). */
    OptionInstance<Integer> frameGenerationBackendOption() {
        if (!FrameGeneration.hasBackendChoice() || staged("hdrOutput", Options.hdrOutput)) {
            return null;
        }
        return new OptionInstance<>("options.radiante.frame_generation_backend",
            tooltip("options.radiante.frame_generation_backend"),
            (caption, value) -> Component.translatable(switch (value) {
                case 1 -> "options.radiante.frame_generation_backend.dlss";
                case 2 -> "options.radiante.frame_generation_backend.fsr";
                default -> "options.radiante.frame_generation_backend.auto";
            }),
            new OptionInstance.Enum<>(List.of(0, 1, 2), Codec.intRange(0, 2)), this.pendingFrameGenerationBackend,
            value -> {
                this.pendingFrameGenerationBackend = value;
                // The multiplier's range follows the backend (FSR: 2x only).
                refreshQualityLater();
            });
    }

    /** Texture Detail: how much of a high resolution pack's detail the ray tracer keeps (TracedTextureDetail). */
    OptionInstance<Integer> textureDetailOption() {
        return new OptionInstance<>("options.radiante.texture_detail", tooltip("options.radiante.texture_detail"),
            (caption, value) -> Component.translatable(switch (value) {
                case 1 -> "options.radiante.texture_detail.full";
                case 2 -> "options.radiante.texture_detail.half";
                case 3 -> "options.radiante.texture_detail.quarter";
                default -> "options.radiante.texture_detail.auto";
            }),
            new OptionInstance.Enum<>(List.of(0, 1, 2, 3), Codec.intRange(0, 3)),
            staged("tracedTextureDetail", Options.tracedTextureDetail), value -> stage("tracedTextureDetail", value));
    }

    /** Render resolution for FSR and XeSS, as DLSS Mode is for DLSS. */
    OptionInstance<String> upscalerModeOption() {
        List<String> modes = upscalerModes();
        if (modes == null) {
            return null;
        }
        if (this.pendingUpscalerMode == null || !modes.contains(this.pendingUpscalerMode)) {
            String current = PipelineSettings.getModuleValue(upscalerModule(), upscalerAttribute());
            this.pendingUpscalerMode = current != null && modes.contains(current) ? current : modes.get(0);
        }
        loadRenderScale();
        return new OptionInstance<>("options.radiante.upscaler_mode", tooltip("options.radiante.upscaler_mode"),
            (caption, value) -> Component.translatable(value),
            new OptionInstance.Enum<>(withCustom(modes), Codec.STRING),
            this.pendingCustomScale ? CUSTOM_MODE : this.pendingUpscalerMode, value -> {
                if (!chooseCustomMode(value)) {
                    this.pendingUpscalerMode = value;
                }
                refreshQualityLater();
            });
    }

    private static List<String> withCustom(List<String> modes) {
        List<String> all = new ArrayList<>(modes);
        all.add(CUSTOM_MODE);
        return all;
    }

    /** The render resolution slider of the Custom upscaling mode; absent in any other mode. */
    OptionInstance<Integer> renderScaleOption() {
        if (renderScaleTarget() == null) {
            return null;
        }
        loadRenderScale();
        if (!this.pendingCustomScale) {
            return null;
        }
        return new OptionInstance<Integer>("options.radiante.render_scale",
            tooltip(usingDlss() ? "options.radiante.render_scale" : "options.radiante.render_scale.limited"),
            (caption, value) -> Component.translatable("options.percent_value", caption, value),
            new OptionInstance.IntRange(renderScaleMin(), 100, false),
            Math.max(renderScaleMin(), this.pendingRenderScale), value -> this.pendingRenderScale = value);
    }

    /**
     * The output resolution: the size the world is rendered and upscaled to before FSR 1 brings it to the window.
     * Only the fixed sizes below the window's are offered, and Custom, whose share of the window is set on the slider
     * below it; the one chosen is kept in the list even when the window has shrunk under it since (it then renders
     * at the window's size, as Native).
     */
    OptionInstance<OutputResolution> outputResolutionOption() {
        OutputResolution current = OutputResolution.byName(staged("outputResolution", Options.outputResolution));
        int[] size = RadianteRenderer.lastOutputSize();
        List<OutputResolution> values = size[2] > 0 ? new ArrayList<>(OutputResolution.offeredFor(size[2], size[3]))
            : new ArrayList<>(List.of(OutputResolution.values()));
        if (!values.contains(current)) {
            values.add(current);
            values.sort(null);
        }
        return new OptionInstance<>("options.radiante.output_resolution", tooltip("options.radiante.output_resolution"),
            (caption, value) -> {
                // Custom names its size on the slider below it.
                if (value == OutputResolution.NATIVE || value == OutputResolution.CUSTOM || size[2] <= 0) {
                    return Component.translatable(value.key());
                }
                int[] output = value.outputSize(size[2], size[3], 0);
                return Component.translatable(value.key()).append(" (" + output[0] + "x" + output[1] + ")");
            },
            new OptionInstance.Enum<>(values, Codec.STRING.xmap(OutputResolution::byName, OutputResolution::name)),
            current, value -> {
                boolean wasCustom = OutputResolution.byName(staged("outputResolution", Options.outputResolution))
                    == OutputResolution.CUSTOM;
                stage("outputResolution", value.name());
                // The share slider shows only with Custom chosen.
                if (wasCustom != (value == OutputResolution.CUSTOM)) {
                    refreshQualityLater();
                }
            });
    }

    /**
     * The share of the window the Custom output resolution renders at, as the DLSS and FSR Custom modes have their
     * render resolution slider; absent with any other output resolution. It names the size that comes out.
     */
    OptionInstance<Integer> outputScaleOption() {
        if (OutputResolution.byName(staged("outputResolution", Options.outputResolution)) != OutputResolution.CUSTOM) {
            return null;
        }
        int[] size = RadianteRenderer.lastOutputSize();
        return new OptionInstance<Integer>("options.radiante.output_scale", tooltip("options.radiante.output_scale"),
            (caption, value) -> {
                Component text = Component.translatable("options.percent_value", caption, value);
                if (size[2] <= 0) {
                    return text;
                }
                int[] output = OutputResolution.CUSTOM.outputSize(size[2], size[3], value);
                return text.copy().append(" (" + output[0] + "x" + output[1] + ")");
            },
            new OptionInstance.IntRange(OutputResolution.MIN_PERCENT, OutputResolution.MAX_PERCENT, false),
            OutputResolution.clampPercent(staged("outputResolutionPercent", Options.outputResolutionPercent)),
            value -> stage("outputResolutionPercent", value));
    }
}
