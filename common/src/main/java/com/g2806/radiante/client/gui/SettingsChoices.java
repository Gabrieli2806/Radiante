package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.addon.AddonImpl;
import com.g2806.radiante.client.addon.AddonRegistry;
import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.render.FrameGeneration;
import com.g2806.radiante.client.render.OutputResolution;
import com.g2806.radiante.client.pipeline.Pipeline;
import com.g2806.radiante.client.pipeline.RtAttributes;
import com.g2806.radiante.client.pipeline.Presets;
import com.g2806.radiante.compat.VersionCompat;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import net.minecraft.network.chat.Component;

/**
 * What is chosen on the settings screen and not applied yet: every value a row holds until Apply or Done, read from
 * what is applied the first time it is needed. The quality levels and Reset to Defaults, which set several of them
 * at once, are here, and so is applying them all. This is the lowest of the three classes the settings screen is
 * written as, each on top of the last; the rows that show and change these values are {@link SettingsRows}, and the
 * screen itself is {@link RadianteOptionsScreen}.
 */
abstract class SettingsChoices extends VersionCompat.DrawnScreen {

    Presets pendingPreset;
    /** A pipeline picked that needs a download or a restart first: shown, with Install / Restart below, not applied. */
    Presets shownPreset;
    String pendingDlssMode;
    /**
     * DLSS Ray Reconstruction on or off; null until read. The selector has one DLSS entry: with this off it applies
     * as RT_NRD_DLSS, where NRD denoises and DLSS only upscales.
     */
    Boolean pendingRayReconstruction;
    /** FSR or XeSS render resolution mode; null when neither is in the pipeline. */
    String pendingUpscalerMode;
    /** The Custom upscaling mode is chosen: the render resolution is the percentage below, not a mode's. */
    boolean pendingCustomScale;
    /** Render resolution of the Custom mode, in percent of the screen. */
    Integer pendingRenderScale;
    /** Entry of the mode lists that stands for the Custom mode; it is never written to the pipeline. */
    static final String CUSTOM_MODE = "options.radiante.upscaler_mode.custom";
    int pendingGeneratedFrames = Options.frameGeneration ? Options.generatedFrames : 0;
    int pendingFrameGenerationBackend = Options.frameGenerationBackend;
    String pendingCloudMode;
    int pendingChunkThreads = Options.chunkBuildingThreads;
    int pendingChunkBatchSize = Options.chunkBuildingBatchSize;
    int pendingChunkTotalBatches = Options.chunkBuildingTotalBatches;
    boolean pendingChunkAuto = Options.chunkBuildingAuto;
    boolean pendingCollectEmission = Options.collectChunkEmission;
    boolean pendingDebugLogging = Options.debugLogging;
    /** The debug view chosen on the Debug section and not applied yet; null when untouched. */
    String pendingDebugView;
    /** The GPU pass timings on the F3 screen as chosen on the Debug section and not applied yet; null when untouched. */
    Boolean pendingGpuTimings;
    boolean pendingBiomeAmbiance = Options.biomeAmbiance;
    boolean pendingFirstPersonShadow = Options.firstPersonShadow;
    int pendingBiomeAmbianceStrength = Options.biomeAmbianceStrength;
    Boolean pendingVolumetricFog;
    Boolean pendingMotionBlur;
    static final String SEAMLESS_SCOPE = "render_pipeline.module.ray_tracing.attribute.seamless_glass_scope";
    Boolean pendingCloudShadows;
    String pendingSeamlessScope;
    Boolean pendingRestir;
    Boolean pendingRestirGi;
    /**
     * radiante-pt's default for restir_gi (configs.json), what Reset puts back. Off: behind DLSS Ray Reconstruction its
     * reuse leaves dark lines along corners and blotches while the camera moves.
     */
    private static final Boolean RESTIR_GI_DEFAULT = Boolean.FALSE;
    Boolean pendingSer;
    Boolean pendingFroxelFog;
    Boolean pendingSeamlessGlass;
    // Plain on/off shader pack settings (light grid, cached deep bounces), by attribute; absent until read.
    java.util.Map<String, Boolean> pendingPackToggles = new java.util.HashMap<>();
    // Shader pack settings that cost frame time; null until read from the pipeline, or when the pack lacks them.
    Integer pendingBounces;
    Boolean pendingParallax;
    Boolean pendingBedrockAtmosphere;
    Integer pendingFogSamples;
    Boolean pendingDepthOfField;
    boolean pendingReflex = Options.reflex;
    /** Numeric pipeline settings, read once per screen; a setting the pipeline lacks has no entry. */
    java.util.EnumMap<Tunable, Integer> pendingTunables;
    PictureStyle.ToneMethod pendingToneMethod;
    boolean applied;

    /** Reset to Defaults was pressed: the stored pipeline and shader pack settings go too, on apply. */
    private boolean forgetStoredSettings;

    /** Advanced options changed and not applied yet, by AdvancedOptions.key; shared by the screens of one visit. */
    java.util.Map<String, String> pendingAdvanced = new java.util.HashMap<>();
    java.util.Map<String, com.g2806.radiante.client.option.EmissionColor> pendingBlockEmissions = new java.util.TreeMap<>(
        com.g2806.radiante.client.render.BlockEmissionControls.values());

    /**
     * Plain options (Options fields) changed on this screen, by field name, held until Apply or Done like every other
     * choice here. Written straight to Options they took effect while the screen was still open.
     */
    private final java.util.Map<String, Object> staged = new java.util.HashMap<>();

    /** Addon options changed on this screen, held until Apply or Done like the rest. */
    final java.util.Map<AddonImpl.SettingImpl<?>, Object> pendingAddons = new java.util.LinkedHashMap<>();

    @SuppressWarnings("unchecked")
    <T> T staged(String field, T current) {
        Object value = this.staged.get(field);
        return value != null ? (T) value : current;
    }

    void stage(String field, Object value) {
        this.staged.put(field, value);
    }

    /** Writes the staged plain options into Options. */
    private void applyStaged() {
        for (var entry : this.staged.entrySet()) {
            if (entry.getKey().equals("renderDistance")) {
                // Minecraft's own option, not one of ours.
                this.options.renderDistance().set((Integer) entry.getValue());
                continue;
            }
            try {
                Options.class.getField(entry.getKey()).set(null, entry.getValue());
            } catch (ReflectiveOperationException e) {
                com.g2806.radiante.client.RadianteClient.LOGGER.warn("Could not apply option {}", entry.getKey(), e);
            }
        }
        this.staged.clear();
    }

    final net.minecraft.client.Options options;

    SettingsChoices(Component title, net.minecraft.client.Options options) {
        super(title);
        this.options = options;
    }

    /** Carries every choice made so far over from the screen this one replaces. */
    void carryOver(SettingsChoices previous) {
        this.pendingPreset = previous.pendingPreset;
        this.staged.putAll(previous.staged);
        this.pendingAddons.putAll(previous.pendingAddons);
        this.shownPreset = previous.shownPreset;
        this.pendingDlssMode = previous.pendingDlssMode;
        this.pendingRayReconstruction = previous.pendingRayReconstruction;
        this.pendingUpscalerMode = previous.pendingUpscalerMode;
        this.pendingRenderScale = previous.pendingRenderScale;
        this.pendingCustomScale = previous.pendingCustomScale;
        this.pendingAdvanced = previous.pendingAdvanced;
        this.pendingBlockEmissions = previous.pendingBlockEmissions;
        this.pendingGeneratedFrames = previous.pendingGeneratedFrames;
        this.pendingCloudMode = previous.pendingCloudMode;
        this.pendingChunkThreads = previous.pendingChunkThreads;
        this.pendingChunkBatchSize = previous.pendingChunkBatchSize;
        this.pendingChunkTotalBatches = previous.pendingChunkTotalBatches;
        this.pendingChunkAuto = previous.pendingChunkAuto;
        this.pendingCollectEmission = previous.pendingCollectEmission;
        this.pendingDebugLogging = previous.pendingDebugLogging;
        this.pendingBiomeAmbiance = previous.pendingBiomeAmbiance;
        this.pendingFirstPersonShadow = previous.pendingFirstPersonShadow;
        this.pendingBiomeAmbianceStrength = previous.pendingBiomeAmbianceStrength;
        this.pendingVolumetricFog = previous.pendingVolumetricFog;
        this.pendingMotionBlur = previous.pendingMotionBlur;
        this.pendingCloudShadows = previous.pendingCloudShadows;
        this.pendingSeamlessScope = previous.pendingSeamlessScope;
        this.pendingRestir = previous.pendingRestir;
        this.pendingRestirGi = previous.pendingRestirGi;
        this.pendingSer = previous.pendingSer;
        this.pendingFroxelFog = previous.pendingFroxelFog;
        this.pendingSeamlessGlass = previous.pendingSeamlessGlass;
        this.pendingPackToggles = new java.util.HashMap<>(previous.pendingPackToggles);
        this.pendingFrameGenerationBackend = previous.pendingFrameGenerationBackend;
        this.pendingBounces = previous.pendingBounces;
        this.pendingParallax = previous.pendingParallax;
        this.forgetStoredSettings = previous.forgetStoredSettings;
        this.pendingBedrockAtmosphere = previous.pendingBedrockAtmosphere;
        this.pendingFogSamples = previous.pendingFogSamples;
        this.pendingDepthOfField = previous.pendingDepthOfField;
        this.pendingReflex = previous.pendingReflex;
        this.pendingDebugLogging = previous.pendingDebugLogging;
        this.pendingDebugView = previous.pendingDebugView;
        this.pendingGpuTimings = previous.pendingGpuTimings;
        this.pendingTunables = previous.pendingTunables;
        this.pendingToneMethod = previous.pendingToneMethod;
    }

    /** While chunk building is automatic the pending values are the CPU's (Options.autoChunkBuilding). */
    void syncChunkAuto() {
        if (this.pendingChunkAuto) {
            int[] auto = Options.autoChunkBuilding();
            this.pendingChunkThreads = auto[0];
            this.pendingChunkBatchSize = auto[1];
            this.pendingChunkTotalBatches = auto[2];
        }
    }

    /**
     * DLSS is one vendor's upscaler and denoiser together; its quality modes and its frame generation belong to it
     * alone. Offering them next to FSR or XeSS suggests they can be combined, and they cannot.
     */
    boolean usingDlss() {
        return this.pendingPreset != null && this.pendingPreset.usesDlss();
    }

    /**
     * Whether a pipeline entry can be picked. The DLSS entry stands for DLSS with or without Ray Reconstruction, so it
     * is there too where the GPU runs DLSS super resolution alone (Apply then builds the NRD + DLSS pipeline).
     */
    static boolean entryAvailable(Presets preset) {
        if (preset == Presets.RT_DLSSRR) {
            return Pipeline.isPresetAvailable(Presets.RT_DLSSRR.key) || Pipeline.isPresetAvailable(Presets.RT_NRD_DLSS.key);
        }
        return Pipeline.isPresetAvailable(preset.key);
    }

    /**
     * The pipeline Apply builds: the DLSS entry becomes RT_NRD_DLSS with Ray Reconstruction off, where NRD is there
     * to take over the denoising.
     */
    private Presets appliedPreset() {
        if (usingDlss()
            && (Boolean.FALSE.equals(this.pendingRayReconstruction) || !Pipeline.isPresetAvailable(Presets.RT_DLSSRR.key))
            && Pipeline.isPresetAvailable(Presets.RT_NRD_DLSS.key)) {
            return Presets.RT_NRD_DLSS;
        }
        return usingDlss() ? Presets.RT_DLSSRR : this.pendingPreset;
    }

    /** HDR output chosen differently from what the window was created with. */
    boolean hdrPending() {
        return staged("hdrOutput", Options.hdrOutput) != com.g2806.radiante.client.hdr.HdrDisplay.isActive();
    }

    /** What the player changed that needs the game started again, for the notice after leaving. */
    List<Component> restartChanges() {
        List<Component> changes = new ArrayList<>();
        if (hdrPending()) {
            changes.add(Component.translatable("options.radiante.hdr_output"));
        }
        return changes;
    }

    /** The FSR or XeSS modes when the chosen pipeline has one of those upscalers; null otherwise. */
    List<String> upscalerModes() {
        if (this.pendingPreset == Presets.RT_NRD_FSR) {
            return Pipeline.FSR_MODES;
        }
        if (this.pendingPreset == Presets.RT_NRD_XESS) {
            return Pipeline.XESS_MODES;
        }
        return null;
    }

    String upscalerModule() {
        return this.pendingPreset == Presets.RT_NRD_FSR ? Pipeline.FSR_MODULE_NAME : Pipeline.XESS_MODULE_NAME;
    }

    String upscalerAttribute() {
        return this.pendingPreset == Presets.RT_NRD_FSR ? Pipeline.FSR_MODE_ATTRIBUTE : Pipeline.XESS_MODE_ATTRIBUTE;
    }

    /** The module and attribute holding the custom render resolution of the chosen pipeline's upscaler. */
    String[] renderScaleTarget() {
        if (usingDlss()) {
            return new String[] {Pipeline.DLSS_MODULE_NAME, Pipeline.DLSS_RENDER_SCALE_ATTRIBUTE};
        }
        if (this.pendingPreset == Presets.RT_NRD_FSR) {
            return new String[] {Pipeline.FSR_MODULE_NAME, Pipeline.FSR_RENDER_SCALE_ATTRIBUTE};
        }
        if (this.pendingPreset == Presets.RT_NRD_XESS) {
            return new String[] {Pipeline.XESS_MODULE_NAME, Pipeline.XESS_RENDER_SCALE_ATTRIBUTE};
        }
        return null;
    }

    /**
     * Applies a pick in a mode list: true when it was Custom (the render resolution slider then shows), false when
     * it was a real mode, which also leaves Custom.
     */
    boolean chooseCustomMode(String value) {
        boolean custom = CUSTOM_MODE.equals(value);
        if (custom && !this.pendingCustomScale) {
            this.pendingRenderScale = Math.max(renderScaleMin(), Math.min(100,
                this.pendingRenderScale == null ? 50 : this.pendingRenderScale));
        }
        this.pendingCustomScale = custom;
        return custom;
    }

    /**
     * The lowest render resolution offered, in percent. DLSS copes with very low ones (its Ray Reconstruction has
     * its own floor in height); FSR and XeSS are designed for at most 3x, which is 33%, and fall apart below.
     */
    int renderScaleMin() {
        return usingDlss() ? 20 : 33;
    }

    /** Reads the render resolution stored in the pipeline once: a value above 0 means the Custom mode was in use. */
    void loadRenderScale() {
        String[] target = renderScaleTarget();
        if (target == null || this.pendingRenderScale != null) {
            return;
        }
        int stored = 0;
        String value = Pipeline.getModuleValue(target[0], target[1]);
        try {
            stored = value == null ? 0 : (int) Math.round(Double.parseDouble(value.trim()));
        } catch (NumberFormatException ignored) {
            // Left to the mode.
        }
        this.pendingCustomScale = stored > 0;
        this.pendingRenderScale = Math.max(renderScaleMin(), Math.min(100, stored > 0 ? stored : 50));
    }

    /** The FSR / XeSS mode a quality level uses: the same step as its DLSS mode. */
    private String upscalerModeFor(QualityPreset quality) {
        List<String> modes = upscalerModes();
        return modes == null ? null : modes.get(Math.min(quality.dlssMode, modes.size() - 1));
    }

    /** The quality level the current choices match, or Custom. */
    QualityPreset currentQuality() {
        for (QualityPreset quality : QualityPreset.values()) {
            if (quality == QualityPreset.CUSTOM) {
                continue;
            }
            boolean dlssMatches = !this.pendingCustomScale && (!usingDlss() || Objects.equals(this.pendingDlssMode,
                Pipeline.DLSS_MODES.get(quality.dlssMode)));
            dlssMatches &= upscalerModes() == null || this.pendingUpscalerMode == null
                || Objects.equals(this.pendingUpscalerMode, upscalerModeFor(quality));
            boolean fogMatches = this.pendingVolumetricFog == null || this.pendingVolumetricFog == quality.volumetricFog;
            boolean cloudsMatch = this.pendingCloudMode == null
                || Objects.equals(this.pendingCloudMode, Pipeline.CLOUD_MODES.get(quality.cloudMode));
            boolean shaderMatches = (this.pendingBounces == null || this.pendingBounces == quality.bounces)
                && (this.pendingParallax == null || this.pendingParallax == quality.parallax)
                && (this.pendingFogSamples == null || this.pendingFogSamples == quality.fogSamples);
            if (dlssMatches && fogMatches && cloudsMatch && shaderMatches
                && staged("blockLightSampling", Options.blockLightSampling) == quality.blockLights
                && staged("renderDistance", this.options.renderDistance().get()) == quality.renderDistance) {
                return quality;
            }
        }
        return QualityPreset.CUSTOM;
    }

    void applyQuality(QualityPreset quality) {
        if (quality == QualityPreset.CUSTOM) {
            return;
        }
        if (usingDlss()) {
            this.pendingDlssMode = Pipeline.DLSS_MODES.get(quality.dlssMode);
        }
        if (upscalerModes() != null) {
            this.pendingUpscalerMode = upscalerModeFor(quality);
        }
        // A quality level picks the upscaler's mode; the Custom mode would hide that.
        this.pendingCustomScale = false;
        if (Pipeline.supportsVolumetricFog()) {
            this.pendingVolumetricFog = quality.volumetricFog;
        }
        if (Pipeline.supportsClouds()) {
            this.pendingCloudMode = Pipeline.CLOUD_MODES.get(quality.cloudMode);
        }
        stage("blockLightSampling", quality.blockLights);
        stage("renderDistance", quality.renderDistance);
        if (this.pendingBounces != null) {
            this.pendingBounces = quality.bounces;
        }
        if (this.pendingParallax != null) {
            this.pendingParallax = quality.parallax;
        }
        if (this.pendingFogSamples != null) {
            this.pendingFogSamples = quality.fogSamples;
        }
    }

    /** Every setting on this screen back to how a fresh install has it. */
    void resetToDefaults() {
        // Staged like every other change: Reset to Defaults then Undo leaves the options as they were.
        stage("blockLightSampling", true);
        stage("heldItemLight", true);
        stage("rainWetness", true);
        stage("blockOutline", false);
        stage("parallaxTransparentEdges", false);
        stage("dayBrightness", 25);
        stage("nightBrightness", 35);
        stage("emissionBrightness", 12);
        stage("heldLightBrightness", 12);
        stage("volumetricFogStrength", 100);
        stage("starAltitude", 100);
        stage("cloudHeightOffset", 0);
        stage("sunPathTilt", 0);
        stage("sunPathAzimuth", 0);
        stage("sunSpriteRotation", 0);
        stage("moonFollowsSun", true);
        stage("moonPathTilt", 0);
        stage("moonPathAzimuth", 0);
        stage("moonSpriteRotation", 0);
        stage("vanillaCelestialOrientation", true);
        stage("outputResolution", OutputResolution.NATIVE.name());
        stage("outputResolutionPercent", OutputResolution.DEFAULT_PERCENT);
        this.forgetStoredSettings = true;
        // DLSS where the GPU has it, otherwise FSR, which every GPU runs: never back to no upscaler at all.
        this.pendingPreset = entryAvailable(Presets.RT_DLSSRR) ? Presets.RT_DLSSRR
            : Pipeline.isPresetAvailable(Presets.RT_NRD_FSR.key) ? Presets.RT_NRD_FSR : null;
        this.pendingDlssMode = "render_pipeline.module.dlss.attribute.mode.ultra_performance";
        this.pendingRayReconstruction = Boolean.TRUE;
        this.pendingGeneratedFrames = 0;
        this.pendingCloudMode = Pipeline.supportsClouds() ? Pipeline.CLOUD_MODES.get(1) : null;
        this.pendingChunkAuto = true;
        syncChunkAuto();
        this.pendingCollectEmission = true;
        this.pendingDebugLogging = true;
        this.pendingBiomeAmbiance = false;
        this.pendingFirstPersonShadow = true;
        this.pendingBiomeAmbianceStrength = 100;
        this.pendingVolumetricFog = Pipeline.supportsVolumetricFog() ? Boolean.TRUE : null;
        this.pendingMotionBlur = Boolean.FALSE;
        this.pendingCloudShadows = Boolean.FALSE;
        // radiante-pt's default (configs.json). Off: the plain sampler's four lights a pixel are quicker and keep the
        // shadows of a room with several lights sharp, which the reuse softens.
        this.pendingRestir = Boolean.FALSE;
        this.pendingRestirGi = Pipeline.supportsShaderPackToggle(RtAttributes.RESTIR_GI_ATTRIBUTE)
            ? RESTIR_GI_DEFAULT : null;
        this.pendingSer = Boolean.FALSE;
        this.pendingFroxelFog = Pipeline.supportsShaderPackToggle(RtAttributes.FROXEL_FOG_ATTRIBUTE)
            ? Boolean.TRUE : null;
        this.pendingSeamlessGlass =
            Pipeline.supportsShaderPackToggle(RtAttributes.SEAMLESS_GLASS_ATTRIBUTE) ? Boolean.FALSE : null;
        this.pendingPackToggles.clear();
        for (String attribute : List.of(RtAttributes.CACHE_DEEP_BOUNCES_ATTRIBUTE,
            RtAttributes.BEDROCK_MATERIALS_ATTRIBUTE)) {
            if (Pipeline.supportsShaderPackToggle(attribute)) {
                this.pendingPackToggles.put(attribute, Boolean.TRUE);
            }
        }
        if (Pipeline.supportsShaderPackToggle(RtAttributes.WIND_ATTRIBUTE)) {
            this.pendingPackToggles.put(RtAttributes.WIND_ATTRIBUTE, Boolean.FALSE);
        }
        this.pendingDepthOfField = Boolean.FALSE;
        this.pendingReflex = false;
        // The shader pack's own defaults.
        this.pendingBounces = this.pendingBounces == null ? null : 4;
        this.pendingParallax = this.pendingParallax == null ? null : Boolean.TRUE;
        this.pendingBedrockAtmosphere = this.pendingBedrockAtmosphere == null ? null : Boolean.TRUE;
        this.pendingFogSamples = this.pendingFogSamples == null ? null : 16;
        this.pendingUpscalerMode = upscalerModes() == null ? null : upscalerModes().get(0);
        this.pendingCustomScale = false;
        this.pendingRenderScale = null;
        this.pendingAdvanced.clear();
        this.pendingBlockEmissions.clear();
        for (AddonImpl addon : AddonRegistry.addons()) {
            for (AddonImpl.SettingImpl<?> setting : addon.settings()) {
                this.pendingAddons.put(setting, setting.defaultValue());
            }
        }
        if (this.pendingTunables != null) {
            this.pendingTunables.replaceAll((tunable, value) -> tunable.defaultSteps);
        }
        if (this.pendingToneMethod != null) {
            this.pendingToneMethod = PictureStyle.ToneMethod.BEDROCK;
        }
    }

    void readTunables() {
        if (this.pendingTunables != null) {
            return;
        }
        this.pendingTunables = new java.util.EnumMap<>(Tunable.class);
        for (Tunable tunable : Tunable.ALL) {
            Integer value = tunable.read();
            if (value != null) {
                this.pendingTunables.put(tunable, value);
            }
        }
        this.pendingToneMethod = PictureStyle.ToneMethod.of(Pipeline.getModuleValue(Pipeline.TONE_MAPPING_MODULE_NAME,
            "render_pipeline.module.tone_mapping.attribute.method"));
    }

    PictureStyle currentStyle() {
        for (PictureStyle style : PictureStyle.values()) {
            if (style.matches(this.pendingToneMethod, this.pendingTunables)) {
                return style;
            }
        }
        return PictureStyle.CUSTOM;
    }

    /** Bedrock's fog volume is what fogs the air, as things are chosen here. */
    boolean bedrockFog() {
        return packToggleOn("render_pipeline.module.ray_tracing.attribute.bedrock_volumetric_fog");
    }

    boolean packToggleOn(String attribute) {
        Boolean pending = this.pendingPackToggles.get(attribute);
        return pending != null ? pending
            : Pipeline.supportsShaderPackToggle(attribute) && Pipeline.isShaderPackToggleOn(attribute);
    }

    /**
     * Applies what was changed; true when the pipeline has to be rebuilt (left to the caller, see applyAndOpen).
     * Changed values the shader pack reads live are handed to the renderer here instead.
     */
    boolean applyChanges() {
        if (this.applied) {
            return false;
        }
        this.applied = true;
        applyStaged();
        com.g2806.radiante.client.render.BlockEmissionControls.apply(this.pendingBlockEmissions);
        if (!this.pendingAddons.isEmpty()) {
            try {
                AddonRegistry.apply(this.pendingAddons);
            } catch (RuntimeException e) {
                RadianteClient.LOGGER.warn("Could not apply the addons' settings", e);
            }
            this.pendingAddons.clear();
        }
        Options.chunkBuildingAuto = this.pendingChunkAuto;
        syncChunkAuto();

        if (this.pendingChunkThreads != Options.chunkBuildingThreads) {
            Options.setChunkBuildingThreads(this.pendingChunkThreads, false);
        }
        if (this.pendingChunkBatchSize != Options.chunkBuildingBatchSize) {
            Options.setChunkBuildingBatchSize(this.pendingChunkBatchSize, false);
        }
        if (this.pendingChunkTotalBatches != Options.chunkBuildingTotalBatches) {
            Options.setChunkBuildingTotalBatches(this.pendingChunkTotalBatches, false);
        }
        if (this.pendingCollectEmission != Options.collectChunkEmission) {
            Options.setCollectChunkEmission(this.pendingCollectEmission, false);
        }
        Options.biomeAmbiance = this.pendingBiomeAmbiance;
        Options.firstPersonShadow = this.pendingFirstPersonShadow;
        if (this.pendingReflex != Options.reflex) {
            Options.reflex = this.pendingReflex;
            FrameGeneration.applyReflex();
        }
        Options.biomeAmbianceStrength = this.pendingBiomeAmbianceStrength;
        if (this.pendingDebugLogging != Options.debugLogging) {
            Options.setDebugLogging(this.pendingDebugLogging, false);
        }
        if (this.pendingGpuTimings != null) {
            RadianteDebugEntries.setGpuTimingsShown(this.pendingGpuTimings);
        }
        if (FrameGeneration.maxGeneratedFrames() <= 0) {
            this.pendingGeneratedFrames = 0;
        }
        Options.frameGenerationBackend = this.pendingFrameGenerationBackend;
        FrameGeneration.applyBackend();
        this.pendingGeneratedFrames = Math.min(this.pendingGeneratedFrames, FrameGeneration.maxGeneratedFrames());
        boolean wantsFrameGeneration = this.pendingGeneratedFrames > 0;
        if (wantsFrameGeneration != Options.frameGeneration || this.pendingGeneratedFrames != Options.generatedFrames) {
            Options.frameGeneration = wantsFrameGeneration;
            Options.generatedFrames = Math.max(1, this.pendingGeneratedFrames);
            FrameGeneration.setGeneratedFrames(this.pendingGeneratedFrames);
        }
        Options.overwriteConfig();

        // Whether a pipeline setting was written (then it is saved), and whether one needs the pipeline built anew;
        // the values the shader pack reads live do not, they go to the renderer at the end.
        boolean changed = false;
        boolean structural = false;
        if (this.forgetStoredSettings) {
            // Every shader pack setting back to the pack's defaults, those without a place on this screen included;
            // the ones on it are applied over them below.
            Pipeline.resetToDefaults();
            changed = true;
            structural = true;
        }
        Presets appliedPreset = appliedPreset();
        if (appliedPreset != null
            && !Objects.equals(appliedPreset.key, Pipeline.INSTANCE.getActivePresetName())) {
            if (Options.debugLogging) {
                RadianteClient.LOGGER.info("[settings] preset: {} -> {}", Pipeline.INSTANCE.getActivePresetName(),
                    appliedPreset.key);
            }
            Pipeline.switchToPresetMode(appliedPreset.key);
            changed = true;
            structural = true;
        }
        if (this.pendingVolumetricFog != null) {
            changed |= Pipeline.setVolumetricFog(this.pendingVolumetricFog);
        }
        if (this.pendingBounces != null) {
            changed |= Pipeline.setShaderPackValue(RtAttributes.RAY_BOUNCES_ATTRIBUTE,
                String.valueOf(this.pendingBounces));
        }
        if (this.pendingBedrockAtmosphere != null) {
            changed |= Pipeline.setShaderPackToggle(RtAttributes.BEDROCK_ATMOSPHERE_ATTRIBUTE,
                this.pendingBedrockAtmosphere);
        }
        if (this.pendingParallax != null) {
            changed |= Pipeline.setShaderPackToggle(RtAttributes.PARALLAX_ATTRIBUTE, this.pendingParallax);
        }
        if (this.pendingFogSamples != null) {
            changed |= Pipeline.setShaderPackValue(RtAttributes.VOLUMETRIC_SAMPLES_ATTRIBUTE,
                String.valueOf(this.pendingFogSamples));
        }
        if (this.pendingTunables != null) {
            for (java.util.Map.Entry<Tunable, Integer> entry : this.pendingTunables.entrySet()) {
                changed |= entry.getKey().write(entry.getValue());
            }
        }
        if (this.pendingToneMethod != null) {
            changed |= Pipeline.setModuleValue(Pipeline.TONE_MAPPING_MODULE_NAME,
                "render_pipeline.module.tone_mapping.attribute.method", this.pendingToneMethod.value);
        }
        if (this.pendingMotionBlur != null) {
            changed |= Pipeline.setShaderPackToggle(RtAttributes.MOTION_BLUR_ATTRIBUTE, this.pendingMotionBlur);
        }
        if (this.pendingSer != null) {
            changed |= Pipeline.setShaderPackToggle(RtAttributes.SER_ATTRIBUTE, this.pendingSer);
        }
        if (this.pendingFroxelFog != null) {
            changed |= Pipeline.setShaderPackToggle(RtAttributes.FROXEL_FOG_ATTRIBUTE, this.pendingFroxelFog);
        }
        for (java.util.Map.Entry<String, Boolean> toggle : this.pendingPackToggles.entrySet()) {
            changed |= Pipeline.setShaderPackToggle(toggle.getKey(), toggle.getValue());
        }
        if (this.pendingSeamlessGlass != null) {
            changed |= Pipeline.setShaderPackToggle(RtAttributes.SEAMLESS_GLASS_ATTRIBUTE, this.pendingSeamlessGlass);
        }
        if (this.pendingRestir != null) {
            changed |= Pipeline.setShaderPackToggle(RtAttributes.RESTIR_ATTRIBUTE, this.pendingRestir);
        }
        if (this.pendingRestirGi != null) {
            changed |= Pipeline.setShaderPackToggle(RtAttributes.RESTIR_GI_ATTRIBUTE, this.pendingRestirGi);
        }
        if (this.pendingSeamlessScope != null) {
            changed |= Pipeline.setShaderPackValue(SEAMLESS_SCOPE, this.pendingSeamlessScope);
        }
        if (this.pendingCloudShadows != null) {
            changed |= Pipeline.setShaderPackToggle(RtAttributes.CLOUD_SHADOWS_ATTRIBUTE, this.pendingCloudShadows);
        }
        if (this.pendingDepthOfField != null) {
            changed |= Pipeline.setShaderPackToggle(RtAttributes.DEPTH_OF_FIELD_ATTRIBUTE, this.pendingDepthOfField);
        }
        if (this.pendingCloudMode != null && !Objects.equals(this.pendingCloudMode, Pipeline.getCloudMode())) {
            changed |= Pipeline.setCloudMode(this.pendingCloudMode);
        }
        // The mode lives on the DLSS module, which only exists once the DLSS pipeline is assembled.
        if (this.pendingDlssMode != null) {
            changed |= Pipeline.setDlssMode(this.pendingDlssMode);
        }
        if (this.pendingUpscalerMode != null && upscalerModes() != null) {
            changed |= Pipeline.setModuleValue(upscalerModule(), upscalerAttribute(), this.pendingUpscalerMode);
        }
        if (this.pendingDebugView != null) {
            changed |= Pipeline.setDebugView(this.pendingDebugView);
        }
        for (java.util.Map.Entry<String, String> advanced : this.pendingAdvanced.entrySet()) {
            String[] target = advanced.getKey().split("\n", 2);
            changed |= Pipeline.setModuleValue(target[0], target[1], advanced.getValue());
        }
        this.pendingAdvanced.clear();
        String[] renderScale = renderScaleTarget();
        if (this.pendingRenderScale != null && renderScale != null) {
            changed |= Pipeline.setModuleValue(renderScale[0], renderScale[1],
                this.pendingCustomScale ? this.pendingRenderScale + ".0" : "0.0");
        }
        if (changed) {
            Pipeline.savePipeline();
        }
        if (!changed) {
            return false;
        }
        // Only what is compiled into the pipeline waits for a build; the rest reaches the renderer now, or is built
        // after all when the renderer turns it down. Saved after the push, as a build would have saved it.
        boolean rebuild = structural || Pipeline.takeStructuralChange() || !Pipeline.pushRuntimeChanges();
        if (!rebuild) {
            Pipeline.savePipeline();
        }
        return rebuild;
    }
}
