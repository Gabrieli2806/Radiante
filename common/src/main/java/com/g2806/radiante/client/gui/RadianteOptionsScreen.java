package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.download.UpscalerDownloads;
import com.g2806.radiante.client.render.RadianteRenderer;
import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.pipeline.Pipeline;
import com.g2806.radiante.client.pipeline.PipelineSettings;
import com.g2806.radiante.client.pipeline.RtAttributes;
import com.g2806.radiante.client.pipeline.Presets;
import com.g2806.radiante.compat.VersionCompat;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.client.OptionInstance;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;

/**
 * Settings for the ray tracer, laid out the way Sodium lays out its own: a tab per section along the top, the section's
 * options in a scrolling column on the left, and on the right what the option under the mouse does and what it costs.
 * Changes are collected while the screen is open and applied with Apply or Done (or on leaving), so the pipeline is
 * rebuilt at most once. The screen is written as three classes, each on top of the last: what is chosen and
 * applying it is {@link SettingsChoices}, the rows of each section are {@link SettingsRows}, and this is the screen
 * itself, which shows them, says which are greyed and why, and leaves for the next screen. The layout is
 * {@link SettingsLayout}.
 */
public class RadianteOptionsScreen extends SettingsRows {

    public static final Component TITLE = Component.translatable("options.radiante.title");

    /** The part of the settings shown; kept while the game runs, so the screen reopens where it was left. */
    enum Category {
        QUALITY, IMAGE, LIGHTING, SKY_AND_WATER, PERFORMANCE, OTHER,
        /** The settings of the shader pack in use (PackSettingsPage), named after it; only with a pack other than Radiante PT. */
        PACK,
        /** Other mods' options (RadianteAddons); only there when one registered. */
        ADDONS,
        /** Developer utilities; only while Advanced is on. */
        DEBUG;

        String key() {
            return "options.radiante.category." + name().toLowerCase(java.util.Locale.ROOT);
        }
    }

    private final Screen lastScreen;
    private SettingsLayout layout;
    /** The search text, kept when the screen is laid out again after a change. */
    private String search = "";
    /** Where the option column was scrolled to, kept when the screen is laid out again after a change. */
    private double scroll;

    public RadianteOptionsScreen(Screen lastScreen, net.minecraft.client.Options options) {
        super(TITLE, options);
        this.lastScreen = lastScreen;
        SettingsLayout.clearFocus();
    }

    /** Carries every choice made so far into a fresh screen. */
    private RadianteOptionsScreen(RadianteOptionsScreen previous) {
        this(previous.lastScreen, previous.options);
        carryOver(previous);
        this.scroll = previous.layout != null ? previous.layout.scroll() : previous.scroll;
        this.search = previous.layout != null ? previous.layout.searchText() : previous.search;
    }

    /**
     * Lays the screen out again after a change that adds or removes options. Rebuilding this screen in place does
     * not work: the layout an options screen holds is created once with the screen and appended to on every init,
     * so a second pass leaves the first pass's widgets in it - they come back greyed out and stale, including
     * options the new pipeline does not even have. A new screen gets a new layout.
     */
    @Override
    void reopenWithSameChoices() {
        if (this.minecraft == null) {
            return;
        }
        // The replacement screen carries the choices, so this one must not write them on the way out.
        this.applied = true;
        this.minecraft.gui.setScreen(new RadianteOptionsScreen(this));
    }

    /**
     * The settings screen, or, when the native renderer is not loaded, the screen that says why: every page of the
     * settings reaches the renderer. Everything that opens the settings from outside (the video settings button, the
     * key binding, the mod list) goes through here.
     */
    /** The Share Settings screen, as the settings' Share button opens it; back to the settings when left. */
    public static Screen createShare(Screen parent, net.minecraft.client.Options options) {
        return new ShareSettingsScreen(parent, () -> new RadianteOptionsScreen(parent, options));
    }

    public static Screen create(Screen parent, net.minecraft.client.Options options) {
        if (!com.g2806.radiante.client.RadianteClient.isNativeLoaded()) {
            return new UnsupportedHardwareScreen(parent);
        }
        return new RadianteOptionsScreen(parent, options);
    }

    /** Development: opens the settings on a section. */
    public static RadianteOptionsScreen showingSection(Screen parent, net.minecraft.client.Options options,
        String section) {
        SettingsLayout.select(Category.valueOf(section));
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
            T + "fog_distance", T + "light_shafts", T + "fog_veil_dimming",
            O + "volumetric_fog_samples")),
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

    private static final String TONE = "render_pipeline.module.tone_mapping.attribute.";
    private static final String METERING_MODE = "render_pipeline.module.tone_mapping.attribute.exposure_metering_mode";
    private static final String METERING_BEDROCK = METERING_MODE + ".bedrock_original";
    private static final String METERING_INCIDENT = METERING_MODE + ".incident";
    private static final String WATER_SURFACE = "render_pipeline.module.ray_tracing.attribute.water_surface_mode";
    private static final String WATER_REALISTIC = WATER_SURFACE + ".realistic";

    /** A pipeline attribute as the screen stands: changed in the Advanced settings and not applied yet, or applied. */
    private String advancedValue(String module, String attribute) {
        String staged = this.pendingAdvanced.get(AdvancedOptions.key(module, attribute));
        return staged != null ? staged : PipelineSettings.getModuleValue(module, attribute);
    }

    /**
     * Why an Advanced tone mapping setting does nothing as the light meter and the curve stand, or null when it does.
     * Bedrock RTX's meter (the default) has limits and a speed of its own and reads no histogram; its curve has no
     * white point and clamps by itself.
     */
    private Component toneMappingBlocked(String name) {
        String meter = advancedValue(Pipeline.TONE_MAPPING_MODULE_NAME, METERING_MODE);
        boolean bedrockMeter = METERING_BEDROCK.equals(meter);
        boolean histogram = (METERING_MODE + ".global").equals(meter) || (METERING_MODE + ".center").equals(meter);
        boolean autoExposure = !"render_pipeline.false".equals(advancedValue(Pipeline.TONE_MAPPING_MODULE_NAME,
            TONE + "enable_auto_exposure"));
        String method = this.pendingToneMethod != null ? this.pendingToneMethod.value
            : PipelineSettings.getModuleValue(Pipeline.TONE_MAPPING_MODULE_NAME, TONE + "method");
        boolean bedrockCurve = (TONE + "method.bedrock_provisional").equals(method);
        boolean whitePoint = method != null && (method.endsWith(".reinhard_white_point")
            || method.endsWith(".aces_white_point") || method.endsWith(".uncharted2"));
        // The Bedrock curve's shadow half lies above the line where nothing is brightened (each bin's slope is at most
        // one, and the highlights already end there), so with Max Brightening at 0 it is that line, whatever its range
        // or least slope.
        boolean curveMayBrighten = parseFloat(advancedValue(Pipeline.TONE_MAPPING_MODULE_NAME,
            TONE + "curve_max_exposure_increase")) > 0.0f;
        return switch (name) {
            case "min_exposure", "max_exposure", "exposure_up_speed", "exposure_down_speed", "exposure_adaptation" ->
                !autoExposure ? needs(TONE + "enable_auto_exposure") : bedrockMeter ? overriddenBy(METERING_MODE) : null;
            case "manual_exposure" -> autoExposure && bedrockMeter ? overriddenBy(METERING_MODE) : null;
            case "curve_dynamic_range", "curve_shadow_min_slope" -> !bedrockCurve ? overriddenBy(O + "tone_method")
                : curveMayBrighten ? null
                : Component.translatable("options.radiante.blocked.needs_above_zero",
                    Component.translatable(TONE + "curve_max_exposure_increase"));
            case "curve_shift", "curve_max_exposure_increase" -> bedrockCurve ? null : overriddenBy(O + "tone_method");
            case "low_percent", "high_percent", "log2_luminance_min", "log2_luminance_max" ->
                !autoExposure ? needs(TONE + "enable_auto_exposure")
                    : histogram ? null : needsValue(METERING_MODE, METERING_MODE + ".global");
            case "center_metering_percent" -> !autoExposure ? needs(TONE + "enable_auto_exposure")
                : (METERING_MODE + ".center").equals(meter) ? null : needsValue(METERING_MODE, METERING_MODE + ".center");
            case "middle_grey" -> bedrockMeter && bedrockCurve ? overriddenBy(METERING_MODE) : null;
            case "white_point" -> whitePoint ? null : overriddenBy(O + "tone_method");
            case "clamp_output" -> bedrockCurve ? overriddenBy(O + "tone_method") : null;
            case "exposure_metering_mode" -> autoExposure ? null : needs(TONE + "enable_auto_exposure");
            default -> null;
        };
    }

    /** A setting's stored number, or 0 when it is missing or not one. */
    private static float parseFloat(String value) {
        if (value == null) {
            return 0.0f;
        }
        try {
            return Float.parseFloat(value.trim());
        } catch (NumberFormatException e) {
            return 0.0f;
        }
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
            : packToggleOn(RtAttributes.FROXEL_FOG_ATTRIBUTE));
        boolean depthOfField = Boolean.TRUE.equals(this.pendingDepthOfField);
        boolean blockLights = staged("blockLightSampling", Options.blockLightSampling);
        boolean volumetricClouds = this.pendingCloudMode != null && this.pendingCloudMode.endsWith(".volumetric");
        // Bedrock RTX's light meter (the default) has its own limits and speed; the exposure controls belong to the
        // other meters, which the Advanced exposure settings offer.
        boolean bedrockMeter = METERING_BEDROCK.equals(advancedValue(Pipeline.TONE_MAPPING_MODULE_NAME, METERING_MODE));
        boolean exactTextures = "render_pipeline.true".equals(advancedValue(Pipeline.RAY_TRACING_MODULE_NAME,
            "render_pipeline.module.ray_tracing.attribute.albedo_srgb_exact"));
        boolean radianceCache = !"render_pipeline.false".equals(advancedValue(Pipeline.RAY_TRACING_MODULE_NAME,
            "render_pipeline.module.ray_tracing.attribute.use_sharc"));
        String volumetricCloudMode = PipelineSettings.CLOUD_MODES.get(PipelineSettings.CLOUD_MODES.size() - 1);
        if (key.equals(SEAMLESS_SCOPE)) {
            return Boolean.TRUE.equals(this.pendingSeamlessGlass) ? null : needs(O + "seamless_glass");
        }
        if (key.equals("render_pipeline.module.ray_tracing.attribute.bedrock_caustics")) {
            return bedrockFilesPresent("caustics.png") ? null
                : Component.translatable("options.radiante.blocked.bedrock_files");
        }
        if (key.startsWith(TONE)) {
            return toneMappingBlocked(key.substring(TONE.length()));
        }
        // The light a lamp casts is on Bedrock's own scale while its pack's materials are read, whatever this says.
        if (key.equals("render_pipeline.module.ray_tracing.attribute.indirect_light_strength")) {
            return packToggleOn(RtAttributes.BEDROCK_MATERIALS_ATTRIBUTE) ? overriddenBy(O + "bedrock_materials") : null;
        }
        if (!key.startsWith(O)) {
            return null;
        }
        return switch (key.substring(O.length())) {
            // Bedrock's fog volume lights the air's shafts too, but keeps its own fixed reach.
            case "volumetric_fog_strength", "tunable.light_shafts", "tunable.fog_veil_dimming" -> !fog ? needs(O + "volumetric_fog") : null;
            case "tunable.fog_distance" -> !fog ? needs(O + "volumetric_fog") : bedrock ? bedrockFogReason : null;
            case "volumetric_fog_samples" ->
                !fog ? needs(O + "volumetric_fog") : bedrock ? bedrockFogReason : grid ? overriddenBy(O + "fog_style") : null;
            case "fog_style" -> !fog ? needs(O + "volumetric_fog") : bedrock ? bedrockFogReason : null;
            case "tunable.motion_blur_strength" ->
                Boolean.TRUE.equals(this.pendingMotionBlur) ? null : needs(O + "motion_blur");
            case "tunable.dof_strength", "tunable.dof_range", "dof_auto_focus" ->
                depthOfField ? null : needs(O + "depth_of_field");
            case "tunable.dof_focus_distance" -> !depthOfField ? needs(O + "depth_of_field")
                : packToggleOn(RtAttributes.DOF_AUTO_FOCUS_ATTRIBUTE) ? overriddenBy(O + "dof_auto_focus") : null;
            case "block_light_sampling" -> this.pendingCollectEmission ? null : needs(O + "collect_chunk_emission");
            case "restir" -> !this.pendingCollectEmission ? needs(O + "collect_chunk_emission")
                : blockLights ? null : needs(O + "block_light_sampling");
            case "held_light_brightness" ->
                staged("heldItemLight", Options.heldItemLight) ? null : needs(O + "held_item_light");
            case "biome_ambiance_strength" -> this.pendingBiomeAmbiance ? null : needs(O + "biome_ambiance");
            case "atmosphere_style" -> bedrockFilesPresent("sky.png", "look_up_tables.png") ? null
                : Component.translatable("options.radiante.blocked.bedrock_files");
            case "cloud_height" -> this.pendingCloudMode == null
                || this.pendingCloudMode.equals(PipelineSettings.CLOUD_MODES.get(1)) ? null
                : needsValue(O + "cloud_mode", PipelineSettings.CLOUD_MODES.get(1));
            case "cloud_shadows", "tunable.cloud_coverage", "tunable.cloud_density", "tunable.cloud_quality" ->
                volumetricClouds ? null : needsValue(O + "cloud_mode", volumetricCloudMode);
            case "parallax_transparent_edges" ->
                Boolean.FALSE.equals(this.pendingParallax) ? needs(O + "parallax") : null;
            case "ray_bounces" ->
                packToggleOn(RtAttributes.BEDROCK_ONE_BOUNCE_ATTRIBUTE) ? overriddenBy(O + "bedrock_one_bounce") : null;
            case "tunable.fsr_sharpness" -> this.pendingPreset == Presets.RT_NRD_FSR ? null
                : needsValue(O + "preset", Presets.RT_NRD_FSR.key);
            case "tunable.exposure_adaptation", "tunable.exposure_speed", "tunable.low_light_boost" ->
                bedrockMeter ? overriddenBy(METERING_MODE) : null;
            // Only the incident light meter has a limit of its own by day.
            case "tunable.day_low_light_limit" -> METERING_INCIDENT.equals(advancedValue(
                Pipeline.TONE_MAPPING_MODULE_NAME, METERING_MODE)) ? null : needsValue(METERING_MODE, METERING_INCIDENT);
            case "tunable.aerial_perspective_start" -> this.pendingTunables != null
                && Integer.valueOf(0).equals(this.pendingTunables.get(Tunable.AERIAL_PERSPECTIVE))
                    ? needs(T + "aerial_perspective") : null;
            // Only the Realistic water surface has waves of its own: Bedrock's is its water texture, vanilla's flat.
            case "tunable.water_waves" -> WATER_REALISTIC.equals(advancedValue(Pipeline.RAY_TRACING_MODULE_NAME,
                WATER_SURFACE)) ? null : needsValue(WATER_SURFACE, WATER_REALISTIC);
            case "tunable.texture_contrast" -> exactTextures
                ? overriddenBy("render_pipeline.module.ray_tracing.attribute.albedo_srgb_exact") : null;
            case "cache_deep_bounces", "bedrock_one_bounce" -> radianceCache ? null
                : needs("render_pipeline.module.ray_tracing.attribute.use_sharc");
            default -> null;
        };
    }

    @Override
    protected void extractContents(net.minecraft.client.gui.GuiGraphicsExtractor graphics, int mouseX, int mouseY) {
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
            && (this.refreshKey < 0 || !VersionCompat.isKeyDown(this.refreshKey))) {
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
