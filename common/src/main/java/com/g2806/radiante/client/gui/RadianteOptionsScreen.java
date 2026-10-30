package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.addon.AddonImpl;
import com.g2806.radiante.client.addon.AddonRegistry;
import com.g2806.radiante.client.download.UpscalerDownloads;
import com.g2806.radiante.client.render.RadianteRenderer;
import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.render.FrameGeneration;
import com.g2806.radiante.client.render.OutputResolution;
import com.g2806.radiante.client.pipeline.Pipeline;
import com.g2806.radiante.client.pipeline.Presets;
import com.mojang.serialization.Codec;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import net.minecraft.client.OptionInstance;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;

/**
 * Settings for the ray tracer, laid out the way Sodium lays out its own: a tab per section along the top, the section's
 * options in a scrolling column on the left, and on the right what the option under the mouse does and what it costs.
 * Changes are collected while the screen is open and applied with Apply or Done (or on leaving), so the pipeline is
 * rebuilt at most once. What each section holds is the option builders below; the layout is {@link SettingsLayout}.
 */
public class RadianteOptionsScreen extends Screen {

    public static final Component TITLE = Component.translatable("options.radiante.title");

    private Presets pendingPreset;
    /** A pipeline picked that needs a download or a restart first: shown, with Install / Restart below, not applied. */
    private Presets shownPreset;
    private String pendingDlssMode;
    /**
     * DLSS Ray Reconstruction on or off; null until read. The selector has one DLSS entry: with this off it applies
     * as RT_NRD_DLSS, where NRD denoises and DLSS only upscales.
     */
    private Boolean pendingRayReconstruction;
    /** FSR or XeSS render resolution mode; null when neither is in the pipeline. */
    private String pendingUpscalerMode;
    /** The Custom upscaling mode is chosen: the render resolution is the percentage below, not a mode's. */
    private boolean pendingCustomScale;
    /** Render resolution of the Custom mode, in percent of the screen. */
    private Integer pendingRenderScale;
    /** Entry of the mode lists that stands for the Custom mode; it is never written to the pipeline. */
    private static final String CUSTOM_MODE = "options.radiante.upscaler_mode.custom";
    private int pendingGeneratedFrames = Options.frameGeneration ? Options.generatedFrames : 0;
    private int pendingFrameGenerationBackend = Options.frameGenerationBackend;
    private String pendingCloudMode;
    private int pendingChunkThreads = Options.chunkBuildingThreads;
    private int pendingChunkBatchSize = Options.chunkBuildingBatchSize;
    private int pendingChunkTotalBatches = Options.chunkBuildingTotalBatches;
    private boolean pendingChunkAuto = Options.chunkBuildingAuto;
    private boolean pendingCollectEmission = Options.collectChunkEmission;
    private boolean pendingDebugLogging = Options.debugLogging;
    private boolean pendingBiomeAmbiance = Options.biomeAmbiance;
    private boolean pendingFirstPersonShadow = Options.firstPersonShadow;
    private int pendingBiomeAmbianceStrength = Options.biomeAmbianceStrength;
    private Boolean pendingVolumetricFog;
    private Boolean pendingMotionBlur;
    private static final String SEAMLESS_SCOPE = "render_pipeline.module.ray_tracing.attribute.seamless_glass_scope";
    private Boolean pendingCloudShadows;
    private String pendingSeamlessScope;
    private Boolean pendingRestir;
    private Boolean pendingRestirGi;
    /**
     * radiante-pt's default for restir_gi (configs.json), what Reset puts back. Off: behind DLSS Ray Reconstruction it
     * took only a little noise off and read a few percent darker.
     */
    private static final Boolean RESTIR_GI_DEFAULT = Boolean.FALSE;
    private Boolean pendingSer;
    private Boolean pendingFroxelFog;
    private Boolean pendingSeamlessGlass;
    // Plain on/off shader pack settings (light grid, cached deep bounces), by attribute; absent until read.
    private java.util.Map<String, Boolean> pendingPackToggles = new java.util.HashMap<>();
    // Shader pack settings that cost frame time; null until read from the pipeline, or when the pack lacks them.
    private Integer pendingBounces;
    private Boolean pendingParallax;
    private Boolean pendingBedrockAtmosphere;
    private Integer pendingFogSamples;
    private Boolean pendingDepthOfField;
    private boolean pendingReflex = Options.reflex;
    /** Numeric pipeline settings, read once per screen; a setting the pipeline lacks has no entry. */
    private java.util.EnumMap<Tunable, Integer> pendingTunables;
    private PictureStyle.ToneMethod pendingToneMethod;
    private boolean applied;

    /** The part of the settings shown; kept while the game runs, so the screen reopens where it was left. */
    enum Category {
        QUALITY, IMAGE, LIGHTING, SKY_AND_WATER, PERFORMANCE, OTHER,
        /** The settings of the shader pack in use (PackSettingsPage), named after it; only with a pack other than Radiante PT. */
        PACK,
        /** Other mods' options (RadianteAddons); only there when one registered. */
        ADDONS;

        String key() {
            return "options.radiante.category." + name().toLowerCase(java.util.Locale.ROOT);
        }
    }

    /** Reset to Defaults was pressed: the stored pipeline and shader pack settings go too, on apply. */
    private boolean forgetStoredSettings;

    /** Advanced options changed and not applied yet, by AdvancedOptions.key; shared by the screens of one visit. */
    private java.util.Map<String, String> pendingAdvanced = new java.util.HashMap<>();

    /**
     * Plain options (Options fields) changed on this screen, by field name, held until Apply or Done like every other
     * choice here. Written straight to Options they took effect while the screen was still open.
     */
    private final java.util.Map<String, Object> staged = new java.util.HashMap<>();

    /** Addon options changed on this screen, held until Apply or Done like the rest. */
    private final java.util.Map<AddonImpl.SettingImpl<?>, Object> pendingAddons = new java.util.LinkedHashMap<>();
    /** The caption keys of the addon options on this screen: nothing of Radiante's own greys them out. */
    private final java.util.Set<String> addonCaptionKeys = new java.util.HashSet<>();
    /** The cost the shader pack declares for its rows on the pack's page (performance_impact). */
    private final java.util.Map<OptionInstance<?>, OptionImpact> packImpacts = new java.util.IdentityHashMap<>();

    @SuppressWarnings("unchecked")
    private <T> T staged(String field, T current) {
        Object value = this.staged.get(field);
        return value != null ? (T) value : current;
    }

    private void stage(String field, Object value) {
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

    private final Screen lastScreen;
    private final net.minecraft.client.Options options;
    /** The options of the section shown, filled by the add*Options builders. */
    private final List<OptionInstance<?>> page = new ArrayList<>();
    private final List<SettingsLayout.Section> sections = new ArrayList<>();
    private SettingsLayout layout;
    /** The search text, kept when the screen is laid out again after a change. */
    private String search = "";
    /** Where the option column was scrolled to, kept when the screen is laid out again after a change. */
    private double scroll;

    public RadianteOptionsScreen(Screen lastScreen, net.minecraft.client.Options options) {
        super(TITLE);
        this.lastScreen = lastScreen;
        this.options = options;
        SettingsLayout.clearFocus();
    }

    /** Carries every choice made so far into a fresh screen. */
    private RadianteOptionsScreen(RadianteOptionsScreen previous) {
        this(previous.lastScreen, previous.options);
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
        this.pendingTunables = previous.pendingTunables;
        this.pendingToneMethod = previous.pendingToneMethod;
        this.scroll = previous.layout != null ? previous.layout.scroll() : previous.scroll;
        this.search = previous.layout != null ? previous.layout.searchText() : previous.search;
    }

    /**
     * Lays the screen out again after a change that adds or removes options. Rebuilding this screen in place does
     * not work: the layout an options screen holds is created once with the screen and appended to on every init,
     * so a second pass leaves the first pass's widgets in it - they come back greyed out and stale, including
     * options the new pipeline does not even have. A new screen gets a new layout.
     */
    private void reopenWithSameChoices() {
        if (this.minecraft == null) {
            return;
        }
        // The replacement screen carries the choices, so this one must not write them on the way out.
        this.applied = true;
        this.minecraft.gui.setScreen(new RadianteOptionsScreen(this));
    }

    /** A 0-400 % slider that applies as it moves; 100 % is the shader pack's own brightness. */
    private static OptionInstance<Integer> brightnessSlider(String key, int current,
        java.util.function.Consumer<Integer> onChange) {
        return new OptionInstance<Integer>(key, RadianteOptionsScreen.<Integer>tooltip(key),
            (caption, value) -> Component.translatable("options.percent_value", caption, value),
            new OptionInstance.IntRange(0, 400, false), current, onChange::accept);
    }

    private static <T> OptionInstance.TooltipSupplier<T> tooltip(String key) {
        return OptionInstance.cachedConstantTooltip(Component.translatable(key + ".tooltip"));
    }

    private static OptionInstance<Integer> slider(String key, int min, int max, int initial,
        OptionInstance.ValueUpdateListener<Integer> onUpdate) {
        return new OptionInstance<Integer>(key, RadianteOptionsScreen.<Integer>tooltip(key),
            (caption, value) -> Component.translatable("options.generic_value", caption, value),
            new OptionInstance.IntRange(min, max, false), Math.max(min, Math.min(max, initial)), onUpdate);
    }

    private OptionInstance<Presets> presetOption() {
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
    private OptionInstance<Boolean> rayTracingToggle() {
        if (!RadianteRenderer.isActive()) {
            return null;
        }
        return OptionInstance.createBoolean("options.radiante.ray_tracing",
            OptionInstance.cachedConstantTooltip(Component.translatable("options.radiante.ray_tracing.tooltip")),
            Options.rayTracingEnabled, value -> {
                if (value != Options.rayTracingEnabled && this.minecraft != null) {
                    com.g2806.radiante.client.RadianteClient.toggleRayTracing(this.minecraft);
                }
            });
    }

    /** While chunk building is automatic the pending values are the CPU's (Options.autoChunkBuilding). */
    private void syncChunkAuto() {
        if (this.pendingChunkAuto) {
            int[] auto = Options.autoChunkBuilding();
            this.pendingChunkThreads = auto[0];
            this.pendingChunkBatchSize = auto[1];
            this.pendingChunkTotalBatches = auto[2];
        }
    }

    /** The download a preset needs, or null when it needs none. */
    private static UpscalerDownloads.Component downloadFor(Presets preset) {
        return switch (preset) {
            case RT_DLSSRR, RT_NRD_DLSS -> UpscalerDownloads.Component.DLSS;
            case RT_NRD_XESS -> UpscalerDownloads.Component.XESS;
            default -> null;
        };
    }

    /** DLSS only on NVIDIA GPUs; XeSS wherever its runtime exists (Windows). */
    private static boolean offersDownload(UpscalerDownloads.Component component) {
        if (!component.offeredHere()) {
            return false;
        }
        return component != UpscalerDownloads.Component.DLSS || RadianteRenderer.isNvidiaGpu();
    }

    /**
     * The settings screen, or, when the native renderer is not loaded, the screen that says why: every page of the
     * settings reaches the renderer. Everything that opens the settings from outside (the video settings button, the
     * key binding, the mod list) goes through here.
     */
    public static Screen create(Screen parent, net.minecraft.client.Options options) {
        if (!com.g2806.radiante.client.RadianteClient.isNativeLoaded()) {
            return new UnsupportedHardwareScreen(parent);
        }
        return new RadianteOptionsScreen(parent, options);
    }

    /** Development: opens the settings looking at a pipeline, as if picked in the selector. */
    public static RadianteOptionsScreen showingPreset(Screen parent, net.minecraft.client.Options options, Presets preset) {
        RadianteOptionsScreen screen = new RadianteOptionsScreen(parent, options);
        // The selector lists DLSS once; without Ray Reconstruction it is that entry with the option off.
        if (preset == Presets.RT_NRD_DLSS) {
            preset = Presets.RT_DLSSRR;
            screen.pendingRayReconstruction = false;
        }
        screen.shownPreset = preset;
        return screen;
    }

    /**
     * The bottom-bar button for the upscaler of the pipeline in the selector: Install when it is not downloaded,
     * Restart when it is downloaded but not loaded yet, Update when it is in use and this build has a newer version,
     * Delete when it is in use. None for FSR and the others. Always
     * shown: picking a pipeline reopens the screen, which drops the focus a row-anchored button would wait for.
     */
    private SettingsLayout.Extra upscalerFooterButton() {
        Presets shown = this.shownPreset != null ? this.shownPreset : this.pendingPreset;
        UpscalerDownloads.Component component = shown == null ? null : downloadFor(shown);
        if (component == null || !offersDownload(component)) {
            return null;
        }
        String name = component == UpscalerDownloads.Component.DLSS ? "DLSS" : "XeSS";
        // Installing or deleting swaps libraries the renderer loads at start: done from the menus, then a restart.
        boolean inWorld = this.minecraft != null && this.minecraft.level != null;
        Component manageTooltip = Component.translatable(inWorld ? "options.radiante.download.leave_world"
            : "options.radiante.download.tooltip");
        var progress = UpscalerDownloads.running(component);
        if (progress != null && !progress.finished) {
            return new SettingsLayout.Extra(Component.translatable("options.radiante.download.button.downloading",
                name, Math.round(progress.fraction() * 100.0f)), null, () -> openUpscalerScreen(component), true, null);
        }
        if (!UpscalerDownloads.isInstalled(component)) {
            return new SettingsLayout.Extra(Component.translatable("options.radiante.download.button.install", name,
                Math.round(component.downloadBytes / 1_000_000.0)),
                manageTooltip, () -> openUpscalerScreen(component), !inWorld, null);
        }
        if (!entryAvailable(shown)) {
            return new SettingsLayout.Extra(Component.translatable("options.radiante.download.button.restart", name),
                null, () -> {
                    this.applied = true;
                    this.minecraft.gui.setScreen(new RestartRequiredScreen(new RadianteOptionsScreen(this),
                        List.of(Component.literal(name))));
                }, true, null);
        }
        if (UpscalerDownloads.isOutdated(component)) {
            // Skipping the start-up offer only hides that prompt: the way to update stays here.
            return new SettingsLayout.Extra(Component.translatable("options.radiante.download.button.update", name,
                Math.round(component.downloadBytes / 1_000_000.0)),
                manageTooltip, () -> openUpscalerScreen(component), !inWorld, null);
        }
        return new SettingsLayout.Extra(Component.translatable("options.radiante.download.button.delete", name),
            manageTooltip, () -> openUpscalerScreen(component), !inWorld, null);
    }

    /** Downloads, deletes or reports on an upscaler, coming back to these settings afterwards. */
    private void openUpscalerScreen(UpscalerDownloads.Component component) {
        if (this.minecraft == null) {
            return;
        }
        this.applied = true;
        Screen back = new RadianteOptionsScreen(this);
        UpscalerDownloadScreen.Mode mode = !UpscalerDownloads.isInstalled(component)
            ? UpscalerDownloadScreen.Mode.CONFIRM
            : UpscalerDownloads.isOutdated(component) ? UpscalerDownloadScreen.Mode.UPDATE
            : UpscalerDownloadScreen.Mode.DELETE;
        this.minecraft.gui.setScreen(new UpscalerDownloadScreen(back, component, mode));
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
    private OptionInstance<String> cloudModeOption() {
        if (!Pipeline.supportsClouds()) {
            return null;
        }

        // A choice carried over from the previous screen (a quality level, say) is kept; only a fresh screen reads
        // the pipeline. Reading it every time undid the quality level's clouds, so it always came back as Custom.
        if (this.pendingCloudMode == null) {
            String current = Pipeline.getCloudMode();
            this.pendingCloudMode = current != null && Pipeline.CLOUD_MODES.contains(current) ? current
                : Pipeline.CLOUD_MODES.get(0);
        }

        return new OptionInstance<>("options.radiante.cloud_mode", tooltip("options.radiante.cloud_mode"),
            (caption, value) -> Component.translatable(value),
            new OptionInstance.Enum<>(Pipeline.CLOUD_MODES, Codec.STRING), this.pendingCloudMode,
            value -> {
                this.pendingCloudMode = value;
                refreshQualityLater();
            });
    }

    /**
     * DLSS is one vendor's upscaler and denoiser together; its quality modes and its frame generation belong to it
     * alone. Offering them next to FSR or XeSS suggests they can be combined, and they cannot.
     */
    private boolean usingDlss() {
        return this.pendingPreset != null && this.pendingPreset.usesDlss();
    }

    /**
     * Whether a pipeline entry can be picked. The DLSS entry stands for DLSS with or without Ray Reconstruction, so it
     * is there too where the GPU runs DLSS super resolution alone (Apply then builds the NRD + DLSS pipeline).
     */
    private static boolean entryAvailable(Presets preset) {
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

    /**
     * Ray Reconstruction is DLSS's own denoiser; off, NRD denoises and DLSS only upscales. Offered only with DLSS,
     * and only where the NRD pipeline that replaces it can be built.
     */
    private OptionInstance<Boolean> rayReconstructionOption() {
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

    private OptionInstance<String> dlssModeOption() {
        if (!usingDlss()) {
            return null;
        }

        if (this.pendingDlssMode == null) {
            String current = Pipeline.getDlssMode();
            this.pendingDlssMode = current != null && Pipeline.DLSS_MODES.contains(current) ? current
                : "render_pipeline.module.dlss.attribute.mode.ultra_performance";
        }

        loadRenderScale();
        return new OptionInstance<>("options.radiante.dlss_mode", tooltip("options.radiante.dlss_mode"),
            (caption, value) -> Component.translatable(value),
            new OptionInstance.Enum<>(withCustom(Pipeline.DLSS_MODES), Codec.STRING),
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
    private OptionInstance<Integer> frameGenerationOption() {
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

    /** Shader Execution Reordering, where the shader pack has it. */
    private OptionInstance<Boolean> serOption() {
        if (!Pipeline.supportsShaderPackToggle(Pipeline.SER_ATTRIBUTE)) {
            return null;
        }
        if (this.pendingSer == null) {
            this.pendingSer = Pipeline.isShaderPackToggleOn(Pipeline.SER_ATTRIBUTE);
        }
        return OptionInstance.createBoolean("options.radiante.ser", tooltip("options.radiante.ser"),
            this.pendingSer, value -> this.pendingSer = value);
    }

    /** ReSTIR for block lights, where the shader pack has it. */
    private OptionInstance<Boolean> restirOption() {
        if (!Pipeline.supportsShaderPackToggle(Pipeline.RESTIR_ATTRIBUTE)) {
            return null;
        }
        if (this.pendingRestir == null) {
            this.pendingRestir = Pipeline.isShaderPackToggleOn(Pipeline.RESTIR_ATTRIBUTE);
        }
        return OptionInstance.createBoolean("options.radiante.restir", tooltip("options.radiante.restir"),
            this.pendingRestir, value -> this.pendingRestir = value);
    }

    /** ReSTIR GI for the first bounce of light, where the shader pack has it. */
    private OptionInstance<Boolean> restirGiOption() {
        if (!Pipeline.supportsShaderPackToggle(Pipeline.RESTIR_GI_ATTRIBUTE)) {
            return null;
        }
        if (this.pendingRestirGi == null) {
            this.pendingRestirGi = Pipeline.isShaderPackToggleOn(Pipeline.RESTIR_GI_ATTRIBUTE);
        }
        return OptionInstance.createBoolean("options.radiante.restir_gi", tooltip("options.radiante.restir_gi"),
            this.pendingRestirGi, value -> this.pendingRestirGi = value);
    }

    /** DLSS or FSR frame generation, offered where the GPU runs both (FSR works next to any upscaler). */
    private OptionInstance<Integer> frameGenerationBackendOption() {
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

    // The one option that only takes effect after a restart; kept to mark it on the list.
    private OptionInstance<?> hdrToggle;

    /** HDR output chosen differently from what the window was created with. */
    private boolean hdrPending() {
        return staged("hdrOutput", Options.hdrOutput) != com.g2806.radiante.client.hdr.HdrDisplay.isActive();
    }

    /** What the player changed that needs the game started again, for the notice after leaving. */
    private List<Component> restartChanges() {
        List<Component> changes = new ArrayList<>();
        if (hdrPending()) {
            changes.add(Component.translatable("options.radiante.hdr_output"));
        }
        return changes;
    }

    /** The FSR or XeSS modes when the chosen pipeline has one of those upscalers; null otherwise. */
    private List<String> upscalerModes() {
        if (this.pendingPreset == Presets.RT_NRD_FSR) {
            return Pipeline.FSR_MODES;
        }
        if (this.pendingPreset == Presets.RT_NRD_XESS) {
            return Pipeline.XESS_MODES;
        }
        return null;
    }

    private String upscalerModule() {
        return this.pendingPreset == Presets.RT_NRD_FSR ? Pipeline.FSR_MODULE_NAME : Pipeline.XESS_MODULE_NAME;
    }

    private String upscalerAttribute() {
        return this.pendingPreset == Presets.RT_NRD_FSR ? Pipeline.FSR_MODE_ATTRIBUTE : Pipeline.XESS_MODE_ATTRIBUTE;
    }

    /** Render resolution for FSR and XeSS, as DLSS Mode is for DLSS. */
    private OptionInstance<String> upscalerModeOption() {
        List<String> modes = upscalerModes();
        if (modes == null) {
            return null;
        }
        if (this.pendingUpscalerMode == null || !modes.contains(this.pendingUpscalerMode)) {
            String current = Pipeline.getModuleValue(upscalerModule(), upscalerAttribute());
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

    /** The module and attribute holding the custom render resolution of the chosen pipeline's upscaler. */
    private String[] renderScaleTarget() {
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

    private static List<String> withCustom(List<String> modes) {
        List<String> all = new ArrayList<>(modes);
        all.add(CUSTOM_MODE);
        return all;
    }

    /**
     * Applies a pick in a mode list: true when it was Custom (the render resolution slider then shows), false when
     * it was a real mode, which also leaves Custom.
     */
    private boolean chooseCustomMode(String value) {
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
    private int renderScaleMin() {
        return usingDlss() ? 20 : 33;
    }

    /** Reads the render resolution stored in the pipeline once: a value above 0 means the Custom mode was in use. */
    private void loadRenderScale() {
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

    /** The render resolution slider of the Custom upscaling mode; absent in any other mode. */
    private OptionInstance<Integer> renderScaleOption() {
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
    private OptionInstance<OutputResolution> outputResolutionOption() {
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
    private OptionInstance<Integer> outputScaleOption() {
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

    /** The FSR / XeSS mode a quality level uses: the same step as its DLSS mode. */
    private String upscalerModeFor(QualityPreset quality) {
        List<String> modes = upscalerModes();
        return modes == null ? null : modes.get(Math.min(quality.dlssMode, modes.size() - 1));
    }

    /** The quality level the current choices match, or Custom. */
    private QualityPreset currentQuality() {
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

    private void applyQuality(QualityPreset quality) {
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
    private void resetToDefaults() {
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
        this.pendingRestir = Boolean.TRUE;
        this.pendingRestirGi = Pipeline.supportsShaderPackToggle(Pipeline.RESTIR_GI_ATTRIBUTE) ? RESTIR_GI_DEFAULT : null;
        this.pendingSer = Boolean.FALSE;
        this.pendingFroxelFog = Pipeline.supportsShaderPackToggle(Pipeline.FROXEL_FOG_ATTRIBUTE) ? Boolean.TRUE : null;
        this.pendingSeamlessGlass =
            Pipeline.supportsShaderPackToggle(Pipeline.SEAMLESS_GLASS_ATTRIBUTE) ? Boolean.FALSE : null;
        this.pendingPackToggles.clear();
        for (String attribute : List.of(Pipeline.CACHE_DEEP_BOUNCES_ATTRIBUTE, Pipeline.BEDROCK_MATERIALS_ATTRIBUTE)) {
            if (Pipeline.supportsShaderPackToggle(attribute)) {
                this.pendingPackToggles.put(attribute, Boolean.TRUE);
            }
        }
        if (Pipeline.supportsShaderPackToggle(Pipeline.WIND_ATTRIBUTE)) {
            this.pendingPackToggles.put(Pipeline.WIND_ATTRIBUTE, Boolean.FALSE);
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

    private void readTunables() {
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

    private PictureStyle currentStyle() {
        for (PictureStyle style : PictureStyle.values()) {
            if (style.matches(this.pendingToneMethod, this.pendingTunables)) {
                return style;
            }
        }
        return PictureStyle.CUSTOM;
    }

    /** A slider for a numeric pipeline setting, or null when the pipeline lacks it. */
    private OptionInstance<Integer> tunable(Tunable tunable, boolean affectsStyle) {
        Integer current = this.pendingTunables.get(tunable);
        if (current == null) {
            return null;
        }
        return new OptionInstance<Integer>(tunable.key(), RadianteOptionsScreen.<Integer>tooltip(tunable.key()),
            (caption, value) -> switch (tunable.format) {
                case EV -> Component.translatable("options.radiante.ev_value", caption,
                    String.format(java.util.Locale.ROOT, "%+.1f", value * tunable.unit));
                case NUMBER -> Component.translatable("options.generic_value", caption,
                    String.valueOf(Math.round(value * tunable.unit)));
                case BLOCKS -> Component.translatable("options.radiante.blocks_value", caption,
                    String.valueOf(Math.round(value * tunable.unit)));
                case SECONDS -> Component.translatable("options.radiante.seconds_value", caption,
                    String.valueOf(Math.round(value * tunable.unit)));
                case PERCENT -> Component.translatable("options.percent_value", caption, value);
            },
            new OptionInstance.IntRange(tunable.min, tunable.max, false), current, value -> {
                this.pendingTunables.put(tunable, value);
                if (affectsStyle) {
                    refreshQualityLater();
                }
            });
    }

    /** Adds options two to a row, skipping the ones the pipeline lacks. */
    private void addRows(OptionInstance<?>... options) {
        List<OptionInstance<?>> present = new ArrayList<>();
        for (OptionInstance<?> option : options) {
            if (option != null) {
                present.add(option);
            }
        }
        this.page.addAll(present);
    }

    /** Builds every section's options, in order, into {@link #sections}. */
    private void buildPage() {

        // Filled in first so the quality level below can tell which one the current settings match.
        OptionInstance<Presets> preset = presetOption();
        OptionInstance<String> dlssMode = dlssModeOption();
        OptionInstance<Integer> frameGeneration = frameGenerationOption();
        OptionInstance<String> clouds = cloudModeOption();
        if (Pipeline.supportsVolumetricFog() && this.pendingVolumetricFog == null) {
            this.pendingVolumetricFog = Pipeline.isVolumetricFog();
        }
        if (this.pendingBounces == null && Pipeline.getShaderPackValue(Pipeline.RAY_BOUNCES_ATTRIBUTE) != null) {
            this.pendingBounces = Math.max(1, Math.min(128, Pipeline.getShaderPackInt(Pipeline.RAY_BOUNCES_ATTRIBUTE, 4)));
        }
        if (this.pendingParallax == null && Pipeline.supportsShaderPackToggle(Pipeline.PARALLAX_ATTRIBUTE)) {
            this.pendingParallax = Pipeline.isShaderPackToggleOn(Pipeline.PARALLAX_ATTRIBUTE);
        }
        if (this.pendingBedrockAtmosphere == null
            && Pipeline.supportsShaderPackToggle(Pipeline.BEDROCK_ATMOSPHERE_ATTRIBUTE)) {
            this.pendingBedrockAtmosphere = Pipeline.isShaderPackToggleOn(Pipeline.BEDROCK_ATMOSPHERE_ATTRIBUTE);
        }
        if (this.pendingFogSamples == null
            && Pipeline.getShaderPackValue(Pipeline.VOLUMETRIC_SAMPLES_ATTRIBUTE) != null) {
            this.pendingFogSamples = Math.max(4, Math.min(32,
                Pipeline.getShaderPackInt(Pipeline.VOLUMETRIC_SAMPLES_ATTRIBUTE, 16)));
        }

        readTunables();

        List<QualityPreset> levels = List.of(QualityPreset.values());
        OptionInstance<QualityPreset> quality = new OptionInstance<>("options.radiante.quality",
            tooltip("options.radiante.quality"), (caption, value) -> Component.translatable(value.key),
            new OptionInstance.Enum<>(levels, Codec.STRING.xmap(QualityPreset::valueOf, QualityPreset::name)), currentQuality(),
            value -> {
                if (value == QualityPreset.CUSTOM || value == currentQuality()) {
                    return;
                }
                applyQuality(value);
                if (this.minecraft != null) {
                    this.minecraft.execute(this::reopenWithSameChoices);
                }
            });
        this.sections.clear();
        this.addonCaptionKeys.clear();
        this.packImpacts.clear();
        for (Category section : Category.values()) {
            this.page.clear();
            if (section == Category.PACK) {
                // A pack whose settings cannot be laid out loses its page, not the screen.
                try {
                    PackSettingsPage.Page pack = PackSettingsPage.build(this.pendingAdvanced, this::refreshQualityLater);
                    if (pack != null) {
                        this.sections.add(pack.section());
                        this.packImpacts.putAll(pack.impacts());
                    }
                } catch (RuntimeException e) {
                    RadianteClient.LOGGER.warn("The shader pack's settings could not be shown", e);
                }
                continue;
            }
            if (section == Category.ADDONS) {
                List<SettingsLayout.Block> blocks = addonBlocks();
                if (!blocks.isEmpty()) {
                    this.sections.add(new SettingsLayout.Section(section, List.of(), List.of(), blocks));
                }
                continue;
            }
            switch (section) {
                case QUALITY -> {
                    addRows(rayTracingToggle());
                    addRows(quality);
                    addQualityOptions(preset, dlssMode, frameGeneration);
                }
                case IMAGE -> addImageOptions();
                case LIGHTING -> addLightingOptions();
                case SKY_AND_WATER -> addSkyAndWaterOptions(clouds);
                case PERFORMANCE -> addPerformanceOptions();
                case OTHER -> addOtherOptions();
                case PACK, ADDONS -> {
                }
            }
            List<SettingsLayout.Group> groups = new ArrayList<>(AdvancedOptions.groups(section, this.pendingAdvanced));
            if (section == Category.SKY_AND_WATER) {
                groups.add(0, skyPathGroup());
            }
            this.sections.add(new SettingsLayout.Section(section, List.copyOf(this.page), groups));
        }
    }

    /** A heading and the options under it per addon registered (RadianteAddons); none when there are none. */
    private List<SettingsLayout.Block> addonBlocks() {
        List<SettingsLayout.Block> blocks = new ArrayList<>();
        for (AddonImpl addon : AddonRegistry.addons()) {
            // An addon whose options cannot be laid out loses its rows, not the screen.
            try {
                List<OptionInstance<?>> rows = new ArrayList<>();
                for (AddonImpl.SettingImpl<?> setting : addon.settings()) {
                    rows.add(addonOption(setting));
                    if (setting.title().getContents()
                        instanceof net.minecraft.network.chat.contents.TranslatableContents translatable) {
                        this.addonCaptionKeys.add(translatable.getKey());
                    }
                }
                net.minecraft.network.chat.MutableComponent heading = addon.title().copy();
                if (addon.version() != null) {
                    heading.append(Component.literal("  " + addon.version()).withColor(0xFF808888));
                }
                blocks.add(new SettingsLayout.Block(addon.id(), heading, AddonIcons.of(addon), List.copyOf(rows),
                    Component.translatable("options.radiante.addons.no_settings")));
            } catch (RuntimeException | LinkageError e) {
                RadianteClient.LOGGER.warn("Addon '{}': its settings could not be shown", addon.id(), e);
            }
        }
        return blocks;
    }

    /** One addon option as a row, staged until Apply like Radiante's own. */
    @SuppressWarnings("unchecked")
    private OptionInstance<?> addonOption(AddonImpl.SettingImpl<?> setting) {
        // Made with a placeholder key, then named with the addon's own component (which may have no key at all).
        String placeholder = "radiante.addon." + setting.addon().id() + "." + setting.key();
        Object current = this.pendingAddons.containsKey(setting) ? this.pendingAddons.get(setting) : setting.get();
        OptionInstance<?> row = switch (setting.kind()) {
            case TOGGLE -> new OptionInstance<Boolean>(placeholder, addonTooltip(setting),
                (caption, value) -> Component.translatable(value ? "options.on" : "options.off"),
                OptionInstance.BOOLEAN_VALUES, (Boolean) current, value -> this.pendingAddons.put(setting, value));
            case CYCLE -> {
                AddonImpl.SettingImpl<String> cycle = (AddonImpl.SettingImpl<String>) setting;
                yield new OptionInstance<String>(placeholder, addonTooltip(setting),
                    (caption, value) -> cycle.label(value), new OptionInstance.Enum<>(cycle.values(), Codec.STRING),
                    (String) current, value -> this.pendingAddons.put(setting, value));
            }
            case SLIDER -> {
                // The slider runs over the steps; the value shown and kept is the setting's own.
                int min = setting.min();
                int step = setting.step();
                yield new OptionInstance<Integer>(placeholder, addonTooltip(setting),
                    (caption, index) -> Component.literal(String.valueOf(min + index * step)),
                    new OptionInstance.IntRange(0, setting.stepCount(), false),
                    Math.max(0, Math.min(setting.stepCount(), ((Integer) current - min) / step)),
                    index -> this.pendingAddons.put(setting, min + index * step));
            }
        };
        ((com.g2806.radiante.mixin.gui.OptionInstanceAccessor) (Object) row).radiante$setCaption(setting.title());
        return row;
    }

    private static <T> OptionInstance.TooltipSupplier<T> addonTooltip(AddonImpl.SettingImpl<?> setting) {
        return setting.tooltip() == null ? OptionInstance.noTooltip()
            : OptionInstance.cachedConstantTooltip(setting.tooltip());
    }

    private void addQualityOptions(OptionInstance<Presets> preset, OptionInstance<String> dlssMode,
        OptionInstance<Integer> frameGeneration) {
        OptionInstance<Boolean> reflex = null;
        // Reflex (VK_NV_low_latency2) is only there on NVIDIA GPUs; it applies at once.
        if (FrameGeneration.isReflexSupported()) {
            reflex = OptionInstance.createBoolean("options.radiante.reflex",
                OptionInstance.cachedConstantTooltip(Component.translatable("options.radiante.reflex.tooltip")),
                this.pendingReflex, value -> {
                    this.pendingReflex = value;
                    refreshQualityLater();
                });
        }
        addRows(preset, dlssMode, rayReconstructionOption(), upscalerModeOption(), renderScaleOption(),
            outputResolutionOption(), outputScaleOption(), frameGenerationBackendOption(), frameGeneration, reflex);
    }

    private void addImageOptions() {
        OptionInstance<PictureStyle> style = null;
        if (this.pendingTunables.containsKey(Tunable.SATURATION)) {
            style = new OptionInstance<>("options.radiante.picture_style", tooltip("options.radiante.picture_style"),
                (caption, value) -> Component.translatable(value.key),
                new OptionInstance.Enum<>(List.of(PictureStyle.values()),
                    Codec.STRING.xmap(PictureStyle::valueOf, PictureStyle::name)),
                currentStyle(), value -> {
                    if (value == PictureStyle.CUSTOM || value == currentStyle()) {
                        return;
                    }
                    value.applyTo(this.pendingTunables);
                    if (this.pendingToneMethod != null) {
                        this.pendingToneMethod = value.method;
                    }
                    refreshQualityLater();
                });
        }
        OptionInstance<PictureStyle.ToneMethod> method = this.pendingToneMethod == null ? null
            : new OptionInstance<>("options.radiante.tone_method", tooltip("options.radiante.tone_method"),
                (caption, value) -> Component.translatable(value.key()),
                new OptionInstance.Enum<>(List.of(PictureStyle.ToneMethod.values()),
                    Codec.STRING.xmap(PictureStyle.ToneMethod::valueOf, PictureStyle.ToneMethod::name)),
                this.pendingToneMethod, value -> {
                    this.pendingToneMethod = value;
                    refreshQualityLater();
                });
        addRows(style, method, tunable(Tunable.SATURATION, true), tunable(Tunable.TEXTURE_CONTRAST, false),
            tunable(Tunable.EXPOSURE_ADAPTATION, false),
            tunable(Tunable.EXPOSURE_SPEED, false), tunable(Tunable.EXPOSURE_BIAS, false),
            tunable(Tunable.LOW_LIGHT_BOOST, false), tunable(Tunable.EXPOSURE_DAY, false),
            tunable(Tunable.EXPOSURE_NIGHT, false), tunable(Tunable.DAY_LOW_LIGHT_LIMIT, false),
            tunable(Tunable.FSR_SHARPNESS, false));
        addHdrOptions();
        if (Pipeline.supportsShaderPackToggle(Pipeline.MOTION_BLUR_ATTRIBUTE)
            && Pipeline.supportsShaderPackToggle(Pipeline.DEPTH_OF_FIELD_ATTRIBUTE)) {
            if (this.pendingMotionBlur == null) {
                this.pendingMotionBlur = Pipeline.isShaderPackToggleOn(Pipeline.MOTION_BLUR_ATTRIBUTE);
            }
            if (this.pendingDepthOfField == null) {
                this.pendingDepthOfField = Pipeline.isShaderPackToggleOn(Pipeline.DEPTH_OF_FIELD_ATTRIBUTE);
            }
            addRows(
                OptionInstance.createBoolean("options.radiante.motion_blur", tooltip("options.radiante.motion_blur"),
                    this.pendingMotionBlur, value -> this.pendingMotionBlur = value),
                OptionInstance.createBoolean("options.radiante.depth_of_field",
                    tooltip("options.radiante.depth_of_field"), this.pendingDepthOfField,
                    value -> this.pendingDepthOfField = value),
                tunable(Tunable.MOTION_BLUR_STRENGTH, false),
                tunable(Tunable.DOF_STRENGTH, false), tunable(Tunable.DOF_RANGE, false),
                packToggle(Pipeline.DOF_AUTO_FOCUS_ATTRIBUTE, "options.radiante.dof_auto_focus"),
                tunable(Tunable.DOF_FOCUS_DISTANCE, false));
        }
    }

    private void addLightingOptions() {
        addRows(brightnessSlider("options.radiante.day_brightness", staged("dayBrightness", Options.dayBrightness),
                value -> stage("dayBrightness", value)),
            brightnessSlider("options.radiante.night_brightness", staged("nightBrightness", Options.nightBrightness),
                value -> stage("nightBrightness", value)),
            brightnessSlider("options.radiante.emission_brightness", staged("emissionBrightness", Options.emissionBrightness),
                value -> stage("emissionBrightness", value)),
            brightnessSlider("options.radiante.held_light_brightness", staged("heldLightBrightness", Options.heldLightBrightness),
                value -> stage("heldLightBrightness", value)),
            tunable(Tunable.BOUNCE_LIGHT, true), tunable(Tunable.SKY_LIGHT, true), tunable(Tunable.TRANSLUCENCY, true),
            OptionInstance.createBoolean("options.radiante.block_light_sampling",
                tooltip("options.radiante.block_light_sampling"), staged("blockLightSampling", Options.blockLightSampling),
                value -> {
                    stage("blockLightSampling", value);
                    refreshQualityLater();
                }),
            restirOption(), restirGiOption(), serOption(),
            OptionInstance.createBoolean("options.radiante.glowing_pixel_light",
                tooltip("options.radiante.glowing_pixel_light"),
                staged("glowingPixelLight", Options.glowingPixelLight), value -> stage("glowingPixelLight", value)),
            OptionInstance.createBoolean("options.radiante.held_item_light",
                tooltip("options.radiante.held_item_light"), staged("heldItemLight", Options.heldItemLight),
                value -> stage("heldItemLight", value)),
            OptionInstance.createBoolean("options.radiante.first_person_shadow",
                OptionInstance.cachedConstantTooltip(Component.translatable("options.radiante.first_person_shadow.tooltip")),
                this.pendingFirstPersonShadow, value -> this.pendingFirstPersonShadow = value));
    }

    private void addSkyAndWaterOptions(OptionInstance<String> clouds) {
        // Without Bedrock's sky tables the renderer draws the Java sky whatever this says: shown as Java, greyed,
        // with the reason, and the stored choice left alone for when the files turn up.
        boolean bedrockSky = bedrockFilesPresent("sky.png", "look_up_tables.png");
        OptionInstance<Boolean> atmosphere = this.pendingBedrockAtmosphere == null ? null
            : OptionInstance.createBoolean("options.radiante.atmosphere_style",
                tooltip("options.radiante.atmosphere_style"),
                (caption, value) -> Component.translatable(!bedrockSky ? "options.radiante.atmosphere_style.java_fallback"
                    : value ? "options.radiante.atmosphere_style.bedrock" : "options.radiante.atmosphere_style.java"),
                bedrockSky && this.pendingBedrockAtmosphere, value -> {
                    if (!bedrockSky) {
                        return;
                    }
                    this.pendingBedrockAtmosphere = value;
                });
        OptionInstance<Integer> cloudHeight = new OptionInstance<Integer>("options.radiante.cloud_height",
            RadianteOptionsScreen.<Integer>tooltip("options.radiante.cloud_height"),
            (caption, value) -> Component.translatable("options.radiante.blocks_value", caption,
                (value > 0 ? "+" : "") + value),
            new OptionInstance.IntRange(Options.MIN_CLOUD_HEIGHT_OFFSET, Options.MAX_CLOUD_HEIGHT_OFFSET, false),
            staged("cloudHeightOffset", Options.cloudHeightOffset), value -> stage("cloudHeightOffset", value));
        OptionInstance<Boolean> volumetricFog = null;
        OptionInstance<Integer> volumetricStrength = null;
        OptionInstance<Boolean> fogStyle = null;
        if (Pipeline.supportsVolumetricFog()) {
            volumetricFog = OptionInstance.createBoolean("options.radiante.volumetric_fog",
                OptionInstance.cachedConstantTooltip(Component.translatable("options.radiante.volumetric_fog.tooltip")),
                this.pendingVolumetricFog, value -> {
                    this.pendingVolumetricFog = value;
                    refreshQualityLater();
                });
            // Bedrock's fog is always its froxel volume: the style is only a choice with Bedrock's fog off.
            if (Pipeline.supportsShaderPackToggle(Pipeline.FROXEL_FOG_ATTRIBUTE) && !bedrockFog()) {
                if (this.pendingFroxelFog == null) {
                    this.pendingFroxelFog = Pipeline.isShaderPackToggleOn(Pipeline.FROXEL_FOG_ATTRIBUTE);
                }
                fogStyle = OptionInstance.createBoolean("options.radiante.fog_style", tooltip("options.radiante.fog_style"),
                    (caption, value) -> Component.translatable(value ? "options.radiante.fog_style.froxel"
                        : "options.radiante.fog_style.march"),
                    this.pendingFroxelFog, value -> this.pendingFroxelFog = value);
            }
            volumetricStrength = brightnessSlider("options.radiante.volumetric_fog_strength",
                staged("volumetricFogStrength", Options.volumetricFogStrength), value -> stage("volumetricFogStrength", value));
        }
        OptionInstance<Boolean> cloudShadows = null;
        if (Pipeline.supportsShaderPackToggle(Pipeline.CLOUD_SHADOWS_ATTRIBUTE)) {
            if (this.pendingCloudShadows == null) {
                this.pendingCloudShadows = Pipeline.isShaderPackToggleOn(Pipeline.CLOUD_SHADOWS_ATTRIBUTE);
            }
            cloudShadows = OptionInstance.createBoolean("options.radiante.cloud_shadows",
                tooltip("options.radiante.cloud_shadows"), this.pendingCloudShadows,
                value -> this.pendingCloudShadows = value);
        }
        OptionInstance<Boolean> seamlessGlass = null;
        if (Pipeline.supportsShaderPackToggle(Pipeline.SEAMLESS_GLASS_ATTRIBUTE)) {
            if (this.pendingSeamlessGlass == null) {
                this.pendingSeamlessGlass = Pipeline.isShaderPackToggleOn(Pipeline.SEAMLESS_GLASS_ATTRIBUTE);
            }
            seamlessGlass = OptionInstance.createBoolean("options.radiante.seamless_glass",
                tooltip("options.radiante.seamless_glass"), this.pendingSeamlessGlass, value -> {
                    this.pendingSeamlessGlass = value;
                });
        }
        // Which glass seamless glass joins: blocks and panes, blocks only or panes only.
        OptionInstance<String> seamlessScope = null;
        String scopeValue = Pipeline.getShaderPackValue(SEAMLESS_SCOPE);
        if (scopeValue != null) {
            if (this.pendingSeamlessScope == null) {
                this.pendingSeamlessScope = scopeValue;
            }
            seamlessScope = new OptionInstance<>(SEAMLESS_SCOPE, tooltip(SEAMLESS_SCOPE),
                (caption, value) -> Component.translatable("options.generic_value", caption,
                    Component.translatable(value)),
                new OptionInstance.Enum<>(List.of(SEAMLESS_SCOPE + ".both", SEAMLESS_SCOPE + ".blocks",
                    SEAMLESS_SCOPE + ".panes"), com.mojang.serialization.Codec.STRING),
                this.pendingSeamlessScope, value -> this.pendingSeamlessScope = value);
        }
        addRows(atmosphere, clouds, cloudHeight, cloudShadows, tunable(Tunable.CLOUD_COVERAGE, false),
            tunable(Tunable.CLOUD_DENSITY, false), tunable(Tunable.CLOUD_QUALITY, false), tunable(Tunable.STARS, false),
            brightnessSlider("options.radiante.star_altitude", staged("starAltitude", Options.starAltitude),
                value -> stage("starAltitude", value)),
            tunable(Tunable.SUN_GLOW, false),
            tunable(Tunable.LIGHT_SHAFTS, false),
            tunable(Tunable.AERIAL_PERSPECTIVE, false), tunable(Tunable.AERIAL_PERSPECTIVE_START, false),
            sunPathOption(),
            OptionInstance.createBoolean("options.radiante.vanilla_celestial_orientation",
                OptionInstance.cachedConstantTooltip(
                    Component.translatable("options.radiante.vanilla_celestial_orientation.tooltip")),
                staged("vanillaCelestialOrientation", Options.vanillaCelestialOrientation), value -> stage("vanillaCelestialOrientation", value)),
            OptionInstance.createBoolean("options.radiante.rain_wetness",
                tooltip("options.radiante.rain_wetness"), staged("rainWetness", Options.rainWetness),
                value -> stage("rainWetness", value)),
            tunable(Tunable.RAIN_WETNESS, false), tunable(Tunable.RAIN_WET_WALLS, false),
            tunable(Tunable.PUDDLES, false), tunable(Tunable.RAIN_WET_UP_TIME, false),
            tunable(Tunable.RAIN_DRYING_TIME, false), tunable(Tunable.LIGHTNING_FLASH, false),
            packToggle(Pipeline.WIND_ATTRIBUTE, "options.radiante.wind"), tunable(Tunable.WIND_STRENGTH, false),
            seamlessGlass, seamlessScope,
            OptionInstance.createBoolean("options.radiante.biome_ambiance",
                OptionInstance.cachedConstantTooltip(Component.translatable("options.radiante.biome_ambiance.tooltip")),
                this.pendingBiomeAmbiance, value -> this.pendingBiomeAmbiance = value),
            new OptionInstance<>("options.radiante.biome_ambiance_strength",
                tooltip("options.radiante.biome_ambiance_strength"),
                (caption, value) -> Component.translatable("options.percent_value", caption, value),
                new OptionInstance.IntRange(0, 400, false), this.pendingBiomeAmbianceStrength,
                value -> this.pendingBiomeAmbianceStrength = value),
            volumetricFog, fogStyle, volumetricStrength, tunable(Tunable.FOG_DISTANCE, false),
            tunable(Tunable.WATER_WAVES, false), tunable(Tunable.WATER_DENSITY, false),
            tunable(Tunable.WATER_GOD_RAYS, false));
    }

    /** The Sun Path choices: vanilla's path, the one leaning south, or what the Sun & Moon Path group sets. */
    enum SunPath {
        VANILLA(0), TILTED(Options.SUN_PATH_TILTED_DEGREES), CUSTOM(0);

        final int tilt;

        SunPath(int tilt) {
            this.tilt = tilt;
        }

        String key() {
            return "options.radiante.sun_path." + name().toLowerCase(java.util.Locale.ROOT);
        }

        static SunPath of(int tilt, int azimuth) {
            return azimuth != 0 ? CUSTOM : tilt == VANILLA.tilt ? VANILLA : tilt == TILTED.tilt ? TILTED : CUSTOM;
        }
    }

    /**
     * The sun's path as one choice: vanilla's or the tilted one, which set the path settings of the Sun & Moon Path
     * group. Custom is only listed while those settings hold something else, and picking it changes nothing.
     */
    private OptionInstance<SunPath> sunPathOption() {
        SunPath current = SunPath.of(staged("sunPathTilt", Options.sunPathTilt),
            staged("sunPathAzimuth", Options.sunPathAzimuth));
        List<SunPath> values = current == SunPath.CUSTOM ? List.of(SunPath.values())
            : List.of(SunPath.VANILLA, SunPath.TILTED);
        return new OptionInstance<>("options.radiante.sun_path", tooltip("options.radiante.sun_path"),
            (caption, value) -> Component.translatable(value.key()),
            new OptionInstance.Enum<>(values, Codec.STRING.xmap(SunPath::valueOf, SunPath::name)), current,
            value -> {
                if (value == SunPath.CUSTOM) {
                    return;
                }
                stage("sunPathTilt", value.tilt);
                stage("sunPathAzimuth", 0);
                // The path sliders below show the values just set.
                refreshQualityLater();
            });
    }

    /** A slider in whole degrees from -limit to limit, held until Apply like the other plain options. */
    private OptionInstance<Integer> degreesSlider(String key, String field, int current, int limit, boolean refresh) {
        return new OptionInstance<Integer>(key, RadianteOptionsScreen.<Integer>tooltip(key),
            (caption, value) -> Component.translatable("options.generic_value", caption, value + "°"),
            new OptionInstance.IntRange(-limit, limit, false), staged(field, current), value -> {
                stage(field, value);
                if (refresh) {
                    // The Sun Path choice above names the path these make.
                    refreshQualityLater();
                }
            });
    }

    /**
     * The Sun & Moon Path group of the advanced settings: the sun's path leaned and turned by any amount, the sun's
     * and the moon's squares turned around their centres, and the moon on a path of its own instead of the sun's.
     * Everything that uses the sun or the moon (the sky, shadows, clouds, fog, water) follows them, live.
     */
    private SettingsLayout.Group skyPathGroup() {
        List<OptionInstance<?>> rows = new ArrayList<>();
        rows.add(degreesSlider("options.radiante.sun_path_tilt", "sunPathTilt", Options.sunPathTilt,
            Options.MAX_PATH_TILT, true));
        rows.add(degreesSlider("options.radiante.sun_path_azimuth", "sunPathAzimuth", Options.sunPathAzimuth,
            Options.MAX_ROTATION, true));
        rows.add(degreesSlider("options.radiante.sun_sprite_rotation", "sunSpriteRotation", Options.sunSpriteRotation,
            Options.MAX_ROTATION, false));
        boolean followsSun = staged("moonFollowsSun", Options.moonFollowsSun);
        rows.add(OptionInstance.createBoolean("options.radiante.moon_follows_sun",
            tooltip("options.radiante.moon_follows_sun"), followsSun, value -> {
                if (!value) {
                    // The moon starts its own path where it was, on the sun's, so nothing jumps.
                    stage("moonPathTilt", staged("sunPathTilt", Options.sunPathTilt));
                    stage("moonPathAzimuth", staged("sunPathAzimuth", Options.sunPathAzimuth));
                }
                stage("moonFollowsSun", value);
                refreshQualityLater();
            }));
        if (!followsSun) {
            rows.add(degreesSlider("options.radiante.moon_path_tilt", "moonPathTilt", Options.moonPathTilt,
                Options.MAX_PATH_TILT, false));
            rows.add(degreesSlider("options.radiante.moon_path_azimuth", "moonPathAzimuth", Options.moonPathAzimuth,
                Options.MAX_ROTATION, false));
        }
        rows.add(degreesSlider("options.radiante.moon_sprite_rotation", "moonSpriteRotation",
            Options.moonSpriteRotation, Options.MAX_ROTATION, false));
        return new SettingsLayout.Group("options.radiante.advanced.group.sky_path", List.copyOf(rows));
    }

    /** An on/off shader pack setting applied with the rest; null where the pack lacks it. */
    private OptionInstance<Boolean> packToggle(String attribute, String key) {
        if (!Pipeline.supportsShaderPackToggle(attribute)) {
            return null;
        }
        boolean value = this.pendingPackToggles.computeIfAbsent(attribute, Pipeline::isShaderPackToggleOn);
        return OptionInstance.createBoolean(key, tooltip(key), value, newValue -> {
            this.pendingPackToggles.put(attribute, newValue);
        });
    }

    private void addPerformanceOptions() {
        OptionInstance<Integer> bounces = this.pendingBounces == null ? null
            : slider("options.radiante.ray_bounces", 1, 128, this.pendingBounces, value -> {
                this.pendingBounces = value;
                refreshQualityLater();
            });
        OptionInstance<Boolean> parallax = this.pendingParallax == null ? null
            : OptionInstance.createBoolean("options.radiante.parallax", tooltip("options.radiante.parallax"),
                this.pendingParallax, value -> {
                    this.pendingParallax = value;
                    refreshQualityLater();
                });
        OptionInstance<Integer> fogSamples = this.pendingFogSamples == null || !Pipeline.supportsVolumetricFog() ? null
            : slider("options.radiante.volumetric_fog_samples", 4, 32, this.pendingFogSamples, value -> {
                this.pendingFogSamples = value;
                refreshQualityLater();
            });
        addRows(bounces, tunable(Tunable.MIRROR_BOUNCES, false), tunable(Tunable.MIRROR_SMOOTHNESS, false),
            tunable(Tunable.GLASS_REFLECTIONS, false),
            tunable(Tunable.WATER_TEXTURE, false),
            tunable(Tunable.WATER_CLEAR_TEAL, false),
            tunable(Tunable.PORTAL_TRANSPARENCY, false),
            tunable(Tunable.RAIN_OPACITY, false), tunable(Tunable.RAIN_STREAKS, false),
            packToggle(Pipeline.CACHE_DEEP_BOUNCES_ATTRIBUTE, "options.radiante.cache_deep_bounces"),
            packToggle(Pipeline.BEDROCK_ONE_BOUNCE_ATTRIBUTE, "options.radiante.bedrock_one_bounce"),
            packToggle(Pipeline.TRANSPARENT_CHECKERBOARD_ATTRIBUTE, "options.radiante.transparent_checkerboard"),
            parallax, packToggle(Pipeline.BEDROCK_MATERIALS_ATTRIBUTE, "options.radiante.bedrock_materials"),
            fogSamples,
            OptionInstance.createBoolean("options.radiante.chunk_building_auto",
                OptionInstance.cachedConstantTooltip(Component.translatable("options.radiante.chunk_building_auto.tooltip",
                    Runtime.getRuntime().availableProcessors(), Options.autoChunkBuilding()[0],
                    Options.autoChunkBuilding()[1], Options.autoChunkBuilding()[2])),
                (caption, value) -> Component.translatable(value ? "options.radiante.chunk_building_auto.on"
                    : "options.radiante.chunk_building_auto.off"),
                this.pendingChunkAuto, value -> {
                    this.pendingChunkAuto = value;
                    syncChunkAuto();
                    // The three sliders below are only shown (and only count) while this is off.
                    if (this.minecraft != null) {
                        this.minecraft.execute(this::reopenWithSameChoices);
                    }
                }),
            this.pendingChunkAuto ? null : slider("options.radiante.chunk_building_threads", 1,
                Options.getMaxChunkBuildingThreads(), this.pendingChunkThreads,
                value -> this.pendingChunkThreads = value),
            this.pendingChunkAuto ? null : slider("options.radiante.chunk_building_batch_size", 1, 64,
                this.pendingChunkBatchSize, value -> this.pendingChunkBatchSize = value),
            this.pendingChunkAuto ? null : slider("options.radiante.chunk_building_total_batches", 1, 64,
                this.pendingChunkTotalBatches, value -> this.pendingChunkTotalBatches = value),
            OptionInstance.createBoolean("options.radiante.collect_chunk_emission",
                tooltip("options.radiante.collect_chunk_emission"), this.pendingCollectEmission,
                value -> this.pendingCollectEmission = value));
    }

    private void addOtherOptions() {
        addRows(OptionInstance.createBoolean("options.radiante.block_outline",
                tooltip("options.radiante.block_outline"), staged("blockOutline", Options.blockOutline),
                value -> stage("blockOutline", value)),
            OptionInstance.createBoolean("options.radiante.mod_overlays",
                tooltip("options.radiante.mod_overlays"), staged("modOverlays", Options.modOverlays),
                value -> stage("modOverlays", value)),
            OptionInstance.createBoolean("options.radiante.text_backgrounds",
                tooltip("options.radiante.text_backgrounds"), staged("textBackgrounds", Options.textBackgrounds),
                value -> stage("textBackgrounds", value)),
            OptionInstance.createBoolean("options.radiante.parallax_transparent_edges",
                tooltip("options.radiante.parallax_transparent_edges"), staged("parallaxTransparentEdges", Options.parallaxTransparentEdges),
                value -> stage("parallaxTransparentEdges", value)),
            OptionInstance.createBoolean("options.radiante.debug_logging",
                tooltip("options.radiante.debug_logging"), this.pendingDebugLogging,
                value -> this.pendingDebugLogging = value));
    }

    /**
     * HDR display output. The window's format is picked when it is created, so the toggle takes effect after a
     * restart; its tooltip says whether HDR is running now. The brightness sliders apply at once.
     */
    private void addHdrOptions() {
        String tooltipKey = com.g2806.radiante.client.hdr.HdrDisplay.isActive() ? "options.radiante.hdr_output.tooltip"
            : staged("hdrOutput", Options.hdrOutput) ? "options.radiante.hdr_output.tooltip_pending"
                : "options.radiante.hdr_output.tooltip";
        OptionInstance<Boolean> toggle = OptionInstance.createBoolean("options.radiante.hdr_output",
            OptionInstance.cachedConstantTooltip(Component.translatable(tooltipKey)), staged("hdrOutput", Options.hdrOutput), value -> {
                stage("hdrOutput", value);
                refreshQualityLater();
            });
        this.hdrToggle = toggle;
        if (!staged("hdrOutput", Options.hdrOutput)) {
            addRows(toggle);
            return;
        }
        addRows(toggle, nitsSlider("options.radiante.hdr_peak", 400, 4000, staged("hdrPeakNits", Options.hdrPeakNits),
                value -> stage("hdrPeakNits", value)),
            nitsSlider("options.radiante.hdr_paper_white", 80, 400, staged("hdrPaperWhiteNits", Options.hdrPaperWhiteNits),
                value -> stage("hdrPaperWhiteNits", value)),
            OptionInstance.createBoolean("options.radiante.hdr_debug_view",
                tooltip("options.radiante.hdr_debug_view"), staged("hdrDebugView", Options.hdrDebugView),
                value -> stage("hdrDebugView", value)));
    }

    private static OptionInstance<Integer> nitsSlider(String key, int min, int max, int current,
        java.util.function.Consumer<Integer> onChange) {
        return new OptionInstance<Integer>(key, RadianteOptionsScreen.<Integer>tooltip(key),
            (caption, value) -> Component.translatable("options.radiante.nits_value", caption, value),
            new OptionInstance.IntRange(min, max, false), Math.max(min, Math.min(max, current)), onChange::accept);
    }

    /** A setting the quality level covers was changed by hand: the level shown above it has to follow. */
    private void refreshQualityLater() {
        // Once the mouse is let go: laying the screen out again under a slider being dragged dropped the drag, so
        // those sliders only moved by clicking where the value should be.
        this.refreshPending = true;
    }

    private boolean refreshPending;

    @Override
    protected void init() {
        buildPage();
        this.layout = new SettingsLayout(this.font, this.width, this.height, this.sections,
            new SettingsLayout.Actions(
                () -> {
                    resetToDefaults();
                    reopenWithSameChoices();
                },
                () -> {
                    // Undo: a fresh screen reads everything back from what is applied.
                    this.applied = true;
                    RadianteOptionsScreen fresh = new RadianteOptionsScreen(this.lastScreen, this.options);
                    fresh.scroll = this.layout.scroll();
                    this.minecraft.gui.setScreen(fresh);
                },
                () -> {
                    // The fresh screen reads the settings back when it is built, so only once they are applied:
                    // built before, it showed (and on leaving wrote back) the old frame generation, Reflex and the
                    // other values kept in Options, so a change had to be applied twice to stick.
                    double scroll = this.layout.scroll();
                    applyAndOpen(() -> {
                        RadianteOptionsScreen fresh = new RadianteOptionsScreen(this.lastScreen, this.options);
                        fresh.scroll = scroll;
                        return fresh;
                    });
                },
                this::onClose, upscalerFooterButton(),
                () -> {
                    // Leaving through Share must not apply what is only staged here.
                    this.applied = true;
                    Screen last = this.lastScreen;
                    this.minecraft.gui.setScreen(new ShareSettingsScreen(this,
                        () -> new RadianteOptionsScreen(last, this.options)));
                },
                Options.advancedSettings,
                () -> {
                    // Only what is shown changes: advanced values stay as they are, visible or not.
                    Options.advancedSettings = !Options.advancedSettings;
                    Options.overwriteConfig();
                    reopenWithSameChoices();
                },
                this::rebuildShaders,
                () -> {
                    // As Share: what is only staged here is not applied by leaving for the pack list, and the
                    // settings screen comes back fresh, reading the settings of whichever pack is in use then.
                    this.applied = true;
                    Screen last = this.lastScreen;
                    this.minecraft.gui.setScreen(new ShaderPacksScreen(
                        () -> new RadianteOptionsScreen(last, this.options)));
                }),
            value -> this.scroll = value);
        this.layout.restartInfo(new SettingsLayout.RestartInfo() {
            @Override
            public boolean needsRestart(OptionInstance<?> option) {
                return option == hdrToggle;
            }

            @Override
            public boolean pending(OptionInstance<?> option) {
                if (option == hdrToggle) {
                    return hdrPending();
                }
                return false;
            }
        });
        this.layout.availability(new SettingsLayout.Availability() {
            @Override
            public Component blocked(String optionKey) {
                return blockedReason(optionKey);
            }

            @Override
            public Component notice(String optionKey) {
                if (optionKey == null) {
                    return null;
                }
                return null;
            }

            @Override
            public OptionImpact impact(OptionInstance<?> option) {
                return packImpacts.get(option);
            }

            @Override
            public List<Component> affects(String optionKey) {
                List<String> keys = optionKey == null ? null : AFFECTS.get(optionKey);
                if (keys == null) {
                    return List.of();
                }
                List<Component> names = new ArrayList<>();
                for (String key : keys) {
                    names.add(Component.translatable(key));
                }
                return names;
            }
        });
        net.minecraft.client.gui.components.EditBox searchField = this.layout.init(this.scroll, this.search);
        // The list goes in ahead of the search field: a controller mod's first focus lands on the options.
        addWidget(this.layout.focusTarget());
        addRenderableWidget(searchField);
    }

    /** For a controller integration (SettingsControls). */
    SettingsLayout controllerLayout() {
        return this.layout;
    }

    private static final String O = "options.radiante.";
    private static final String T = "options.radiante.tunable.";

    /** The options an option switches off or takes the place of, named under its description. */
    private static final java.util.Map<String, List<String>> AFFECTS = java.util.Map.ofEntries(
        java.util.Map.entry(O + "volumetric_fog", List.of(O + "fog_style", O + "volumetric_fog_strength",
            T + "fog_distance", T + "light_shafts", O + "volumetric_fog_samples")),
        java.util.Map.entry(O + "fog_style", List.of(O + "volumetric_fog_samples")),
        java.util.Map.entry(O + "motion_blur", List.of(T + "motion_blur_strength")),
        java.util.Map.entry(O + "depth_of_field", List.of(T + "dof_strength", T + "dof_range", O + "dof_auto_focus",
            T + "dof_focus_distance")),
        java.util.Map.entry(O + "dof_auto_focus", List.of(T + "dof_focus_distance")),
        java.util.Map.entry(O + "collect_chunk_emission", List.of(O + "block_light_sampling", O + "restir")),
        java.util.Map.entry(O + "block_light_sampling", List.of(O + "restir")),
        java.util.Map.entry(O + "held_item_light", List.of(O + "held_light_brightness")),
        java.util.Map.entry(O + "biome_ambiance", List.of(O + "biome_ambiance_strength")),
        java.util.Map.entry(O + "seamless_glass", List.of(SEAMLESS_SCOPE)),
        java.util.Map.entry(O + "cloud_mode", List.of(O + "cloud_height", O + "cloud_shadows", T + "cloud_coverage",
            T + "cloud_density", T + "cloud_quality")),
        java.util.Map.entry(O + "parallax", List.of(O + "parallax_transparent_edges")),
        java.util.Map.entry(O + "bedrock_one_bounce", List.of(O + "ray_bounces")),
        java.util.Map.entry(O + "preset", List.of(T + "fsr_sharpness")),
        java.util.Map.entry(O + "quality", List.of(O + "dlss_mode", O + "upscaler_mode", O + "block_light_sampling",
            O + "volumetric_fog", O + "cloud_mode", O + "ray_bounces", O + "parallax", O + "volumetric_fog_samples")));

    /** Bedrock's fog volume is what fogs the air, as things are chosen here. */
    private boolean bedrockFog() {
        return packToggleOn("render_pipeline.module.ray_tracing.attribute.bedrock_volumetric_fog");
    }

    private static Component needs(String otherKey) {
        return Component.translatable("options.radiante.blocked.needs", Component.translatable(otherKey));
    }

    private static Component needsValue(String otherKey, String valueKey) {
        return Component.translatable("options.radiante.blocked.needs_value", Component.translatable(otherKey),
            Component.translatable(valueKey));
    }

    private static Component overriddenBy(String otherKey) {
        return Component.translatable("options.radiante.blocked.overridden", Component.translatable(otherKey));
    }

    private boolean packToggleOn(String attribute) {
        Boolean pending = this.pendingPackToggles.get(attribute);
        return pending != null ? pending
            : Pipeline.supportsShaderPackToggle(attribute) && Pipeline.isShaderPackToggleOn(attribute);
    }

    /**
     * Why an option does nothing as the screen stands (its row is grey and its description says this), or null when
     * it does. From the choices made here, applied or not, so a row greys the moment the setting it hangs on is
     * switched.
     */
    private Component blockedReason(String key) {
        if (key == null || this.addonCaptionKeys.contains(key)) {
            return null;
        }
        if (RadianteRenderer.isActive() && !Options.rayTracingEnabled && !key.equals(O + "ray_tracing")) {
            return Component.translatable("options.radiante.blocked.ray_tracing");
        }
        // Bedrock's fog volume has no use for the air's own controls: with it they do nothing at all.
        boolean bedrock = bedrockFog();
        Component bedrockFogReason = Component.translatable("options.radiante.blocked.bedrock_fog");
        boolean fog = Boolean.TRUE.equals(this.pendingVolumetricFog);
        boolean grid = bedrock || (this.pendingFroxelFog != null ? this.pendingFroxelFog
            : packToggleOn(Pipeline.FROXEL_FOG_ATTRIBUTE));
        boolean depthOfField = Boolean.TRUE.equals(this.pendingDepthOfField);
        boolean blockLights = staged("blockLightSampling", Options.blockLightSampling);
        boolean volumetricClouds = this.pendingCloudMode != null && this.pendingCloudMode.endsWith(".volumetric");
        String volumetricCloudMode = Pipeline.CLOUD_MODES.get(Pipeline.CLOUD_MODES.size() - 1);
        if (key.equals(SEAMLESS_SCOPE)) {
            return Boolean.TRUE.equals(this.pendingSeamlessGlass) ? null : needs(O + "seamless_glass");
        }
        if (key.equals("render_pipeline.module.ray_tracing.attribute.bedrock_caustics")) {
            return bedrockFilesPresent("caustics.png") ? null
                : Component.translatable("options.radiante.blocked.bedrock_files");
        }
        if (!key.startsWith(O)) {
            return null;
        }
        return switch (key.substring(O.length())) {
            // Bedrock's fog volume lights the air's shafts too, but keeps its own fixed reach.
            case "volumetric_fog_strength", "tunable.light_shafts" -> !fog ? needs(O + "volumetric_fog") : null;
            case "tunable.fog_distance" -> !fog ? needs(O + "volumetric_fog") : bedrock ? bedrockFogReason : null;
            case "volumetric_fog_samples" ->
                !fog ? needs(O + "volumetric_fog") : bedrock ? bedrockFogReason : grid ? overriddenBy(O + "fog_style") : null;
            case "fog_style" -> !fog ? needs(O + "volumetric_fog") : bedrock ? bedrockFogReason : null;
            case "tunable.motion_blur_strength" ->
                Boolean.TRUE.equals(this.pendingMotionBlur) ? null : needs(O + "motion_blur");
            case "tunable.dof_strength", "tunable.dof_range", "dof_auto_focus" ->
                depthOfField ? null : needs(O + "depth_of_field");
            case "tunable.dof_focus_distance" -> !depthOfField ? needs(O + "depth_of_field")
                : packToggleOn(Pipeline.DOF_AUTO_FOCUS_ATTRIBUTE) ? overriddenBy(O + "dof_auto_focus") : null;
            case "block_light_sampling" -> this.pendingCollectEmission ? null : needs(O + "collect_chunk_emission");
            case "restir" -> !this.pendingCollectEmission ? needs(O + "collect_chunk_emission")
                : blockLights ? null : needs(O + "block_light_sampling");
            case "held_light_brightness" ->
                staged("heldItemLight", Options.heldItemLight) ? null : needs(O + "held_item_light");
            case "biome_ambiance_strength" -> this.pendingBiomeAmbiance ? null : needs(O + "biome_ambiance");
            case "atmosphere_style" -> bedrockFilesPresent("sky.png", "look_up_tables.png") ? null
                : Component.translatable("options.radiante.blocked.bedrock_files");
            case "cloud_height" -> this.pendingCloudMode == null
                || this.pendingCloudMode.equals(Pipeline.CLOUD_MODES.get(1)) ? null
                : needsValue(O + "cloud_mode", Pipeline.CLOUD_MODES.get(1));
            case "cloud_shadows", "tunable.cloud_coverage", "tunable.cloud_density", "tunable.cloud_quality" ->
                volumetricClouds ? null : needsValue(O + "cloud_mode", volumetricCloudMode);
            case "parallax_transparent_edges" ->
                Boolean.FALSE.equals(this.pendingParallax) ? needs(O + "parallax") : null;
            case "ray_bounces" ->
                packToggleOn(Pipeline.BEDROCK_ONE_BOUNCE_ATTRIBUTE) ? overriddenBy(O + "bedrock_one_bounce") : null;
            case "tunable.fsr_sharpness" -> this.pendingPreset == Presets.RT_NRD_FSR ? null
                : needsValue(O + "preset", Presets.RT_NRD_FSR.key);
            default -> null;
        };
    }

    @Override
    public void extractRenderState(net.minecraft.client.gui.GuiGraphicsExtractor graphics, int mouseX, int mouseY,
        float partialTick) {
        super.extractRenderState(graphics, mouseX, mouseY, partialTick);
        if (this.layout != null) {
            this.layout.extract(graphics, mouseX, mouseY);
        }
        // Which build this is, for bug reports: the mod's version and the Bedrock pack converter's. In the strip
        // over the search box, lined up with it (SettingsLayout's margin): along the bottom it ran over the buttons.
        graphics.text(this.font, Component.translatable("options.radiante.version",
                com.g2806.radiante.platform.RadiantePlatform.INSTANCE.modVersion(),
                com.g2806.radiante.client.bedrock.BedrockPackConverter.converterVersion()),
            Math.max(8, this.width / 24), 2, 0xFF808080);
    }

    @Override
    public boolean mouseClicked(net.minecraft.client.input.MouseButtonEvent event, boolean doubleClick) {
        this.mouseHeld = true;
        this.refreshKey = -1;
        if (super.mouseClicked(event, doubleClick)) {
            return true;
        }
        return this.layout != null && this.layout.mouseClicked(event.x(), event.y(), event.button());
    }

    @Override
    public boolean mouseDragged(net.minecraft.client.input.MouseButtonEvent event, double dragX, double dragY) {
        if (this.layout != null && this.layout.mouseDragged(event.x())) {
            return true;
        }
        return super.mouseDragged(event, dragX, dragY);
    }

    @Override
    public boolean mouseReleased(net.minecraft.client.input.MouseButtonEvent event) {
        this.mouseHeld = false;
        if (this.layout != null) {
            this.layout.mouseReleased();
        }
        if (this.refreshPending) {
            this.refreshPending = false;
            reopenWithSameChoices();
            return true;
        }
        return super.mouseReleased(event);
    }

    @Override
    public boolean mouseScrolled(double mouseX, double mouseY, double scrollX, double scrollY) {
        if (this.layout != null && this.layout.scrollBy(mouseX, mouseY, scrollY)) {
            return true;
        }
        return super.mouseScrolled(mouseX, mouseY, scrollX, scrollY);
    }

    @Override
    public void onClose() {
        applyAndOpen(this.lastScreen);
    }

    /**
     * Applies what was changed; true when the pipeline has to be rebuilt (left to the caller, see applyAndOpen).
     * Changed values the shader pack reads live are handed to the renderer here instead.
     */
    private boolean applyChanges() {
        if (this.applied) {
            return false;
        }
        this.applied = true;
        applyStaged();
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
            changed |= Pipeline.setShaderPackValue(Pipeline.RAY_BOUNCES_ATTRIBUTE, String.valueOf(this.pendingBounces));
        }
        if (this.pendingBedrockAtmosphere != null) {
            changed |= Pipeline.setShaderPackToggle(Pipeline.BEDROCK_ATMOSPHERE_ATTRIBUTE, this.pendingBedrockAtmosphere);
        }
        if (this.pendingParallax != null) {
            changed |= Pipeline.setShaderPackToggle(Pipeline.PARALLAX_ATTRIBUTE, this.pendingParallax);
        }
        if (this.pendingFogSamples != null) {
            changed |= Pipeline.setShaderPackValue(Pipeline.VOLUMETRIC_SAMPLES_ATTRIBUTE,
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
            changed |= Pipeline.setShaderPackToggle(Pipeline.MOTION_BLUR_ATTRIBUTE, this.pendingMotionBlur);
        }
        if (this.pendingSer != null) {
            changed |= Pipeline.setShaderPackToggle(Pipeline.SER_ATTRIBUTE, this.pendingSer);
        }
        if (this.pendingFroxelFog != null) {
            changed |= Pipeline.setShaderPackToggle(Pipeline.FROXEL_FOG_ATTRIBUTE, this.pendingFroxelFog);
        }
        for (java.util.Map.Entry<String, Boolean> toggle : this.pendingPackToggles.entrySet()) {
            changed |= Pipeline.setShaderPackToggle(toggle.getKey(), toggle.getValue());
        }
        if (this.pendingSeamlessGlass != null) {
            changed |= Pipeline.setShaderPackToggle(Pipeline.SEAMLESS_GLASS_ATTRIBUTE, this.pendingSeamlessGlass);
        }
        if (this.pendingRestir != null) {
            changed |= Pipeline.setShaderPackToggle(Pipeline.RESTIR_ATTRIBUTE, this.pendingRestir);
        }
        if (this.pendingRestirGi != null) {
            changed |= Pipeline.setShaderPackToggle(Pipeline.RESTIR_GI_ATTRIBUTE, this.pendingRestirGi);
        }
        if (this.pendingSeamlessScope != null) {
            changed |= Pipeline.setShaderPackValue(SEAMLESS_SCOPE, this.pendingSeamlessScope);
        }
        if (this.pendingCloudShadows != null) {
            changed |= Pipeline.setShaderPackToggle(Pipeline.CLOUD_SHADOWS_ATTRIBUTE, this.pendingCloudShadows);
        }
        if (this.pendingDepthOfField != null) {
            changed |= Pipeline.setShaderPackToggle(Pipeline.DEPTH_OF_FIELD_ATTRIBUTE, this.pendingDepthOfField);
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

    /**
     * Ctrl + Reset to Defaults: what is changed here is applied, then the pipeline is built anew whether or not
     * anything asks for it, a way out of a renderer left in a bad state by a change (normally a restart).
     */
    private void rebuildShaders() {
        if (this.minecraft == null) {
            return;
        }
        double scroll = this.layout != null ? this.layout.scroll() : this.scroll;
        List<Component> restart = restartChanges();
        applyChanges();
        RadianteOptionsScreen fresh = new RadianteOptionsScreen(this.lastScreen, this.options);
        fresh.scroll = scroll;
        Screen next = restart.isEmpty() ? fresh : new RestartRequiredScreen(fresh, restart);
        BackgroundRebuild.open(this.minecraft, next, true);
    }

    @Override
    public boolean keyPressed(net.minecraft.client.input.KeyEvent event) {
        if (this.layout != null && !this.layout.searchFocused() && this.layout.keyPressed(event)) {
            // The list holds the screen's focus while the keys work in it, so the screen's own navigation (and a
            // controller mod's) goes on from there.
            if (getFocused() != this.layout.focusTarget()) {
                setFocused(this.layout.focusTarget());
            }
            this.refreshKey = event.key();
            return true;
        }
        return super.keyPressed(event);
    }

    /** The key whose change waits for it to be let go before the screen is laid out again; -1 for none. */
    private int refreshKey = -1;

    @Override
    public void tick() {
        super.tick();
        // A change from a controller has no key to let go of: controller mods press keys without releasing them, or
        // move the focus without a key at all. Laid out again once no key or mouse button holds it.
        if (this.refreshPending && !this.mouseHeld
            && (this.refreshKey < 0 || !com.mojang.blaze3d.platform.InputConstants.isKeyDown(this.refreshKey))) {
            this.refreshPending = false;
            reopenWithSameChoices();
        }
    }

    private boolean mouseHeld;

    @Override
    public boolean keyReleased(net.minecraft.client.input.KeyEvent event) {
        // A change made with the keys that lays the screen out again waits for the key to be let go, as a drag waits
        // for the mouse.
        if (this.refreshPending) {
            this.refreshPending = false;
            reopenWithSameChoices();
            return true;
        }
        return super.keyReleased(event);
    }

    /**
     * Opens the next screen. A change the pipeline has to be built again for is compiled in the background while the
     * game goes on (BackgroundRebuild), or behind the rebuild's wait screen where that cannot be done.
     */
    private void applyAndOpen(Screen next) {
        applyAndOpen(() -> next);
    }

    /** {@code next} is only made after the changes are applied, so a new settings screen shows them. */
    private void applyAndOpen(java.util.function.Supplier<Screen> nextAfterApply) {
        List<Component> restart = restartChanges();
        boolean rebuild = applyChanges();
        Screen next = nextAfterApply.get();
        if (!restart.isEmpty()) {
            next = new RestartRequiredScreen(next, restart);
        }
        BackgroundRebuild.open(this.minecraft, next, rebuild);
    }
}
