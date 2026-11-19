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
import com.g2806.radiante.client.pipeline.PipelineSettings;
import com.g2806.radiante.client.pipeline.RtAttributes;
import com.g2806.radiante.client.pipeline.Presets;
import com.mojang.serialization.Codec;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import net.minecraft.client.OptionInstance;
import net.minecraft.network.chat.Component;
import com.g2806.radiante.client.gui.RadianteOptionsScreen.Category;
import com.g2806.radiante.compat.VersionCompat;

/**
 * The rows of the settings screen: a builder per option, the add*Options methods that put them into their section,
 * and {@link #buildPage()}, which makes every section. A new row goes here. Rows read and write the choices held in
 * {@link SettingsChoices}; {@link RadianteOptionsScreen}, the screen itself, lays the sections out and says which
 * rows are greyed.
 */
abstract class SettingsRows extends SettingsUpscalerRows {

    /** The caption keys of the addon options on this screen: nothing of Radiante's own greys them out. */
    final java.util.Set<String> addonCaptionKeys = new java.util.HashSet<>();
    /** The cost the shader pack declares for its rows on the pack's page (performance_impact). */
    final java.util.Map<OptionInstance<?>, OptionImpact> packImpacts = new java.util.IdentityHashMap<>();

    /** The options of the section shown, filled by the add*Options builders. */
    private final List<OptionInstance<?>> page = new ArrayList<>();
    final List<SettingsLayout.Section> sections = new ArrayList<>();

    // The one option that only takes effect after a restart; kept to mark it on the list.
    OptionInstance<?> hdrToggle;

    SettingsRows(Component title, net.minecraft.client.Options options) {
        super(title, options);
    }

    /** A 0-400 % slider that applies as it moves; 100 % is the shader pack's own brightness. */
    private static OptionInstance<Integer> brightnessSlider(String key, int current,
        java.util.function.Consumer<Integer> onChange) {
        return new OptionInstance<Integer>(key, SettingsRows.<Integer>tooltip(key),
            (caption, value) -> Component.translatable("options.percent_value", caption, value),
            new OptionInstance.IntRange(0, 400, false), current, onChange::accept);
    }

    private static OptionInstance<Integer> slider(String key, int min, int max, int initial,
        OptionInstance.ValueUpdateListener<Integer> onUpdate) {
        return new OptionInstance<Integer>(key, SettingsRows.<Integer>tooltip(key),
            (caption, value) -> Component.translatable("options.generic_value", caption, value),
            new OptionInstance.IntRange(min, max, false), Math.max(min, Math.min(max, initial)), onUpdate);
    }

    /** A slider for a numeric pipeline setting, or null when the pipeline lacks it. */
    private OptionInstance<Integer> tunable(Tunable tunable, boolean affectsStyle) {
        Integer current = this.pendingTunables.get(tunable);
        if (current == null) {
            return null;
        }
        return new OptionInstance<Integer>(tunable.key(), SettingsRows.<Integer>tooltip(tunable.key()),
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
    void buildPage() {

        // Filled in first so the quality level below can tell which one the current settings match.
        OptionInstance<Presets> preset = presetOption();
        OptionInstance<String> dlssMode = dlssModeOption();
        OptionInstance<Integer> frameGeneration = frameGenerationOption();
        OptionInstance<String> clouds = cloudModeOption();
        if (PipelineSettings.supportsVolumetricFog() && this.pendingVolumetricFog == null) {
            this.pendingVolumetricFog = PipelineSettings.isVolumetricFog();
        }
        if (this.pendingBounces == null && PipelineSettings.getShaderPackValue(RtAttributes.RAY_BOUNCES_ATTRIBUTE) != null) {
            this.pendingBounces = Math.max(1, Math.min(128,
                PipelineSettings.getShaderPackInt(RtAttributes.RAY_BOUNCES_ATTRIBUTE, 4)));
        }
        if (this.pendingParallax == null && PipelineSettings.supportsShaderPackToggle(RtAttributes.PARALLAX_ATTRIBUTE)) {
            this.pendingParallax = PipelineSettings.isShaderPackToggleOn(RtAttributes.PARALLAX_ATTRIBUTE);
        }
        if (this.pendingBedrockAtmosphere == null
            && PipelineSettings.supportsShaderPackToggle(RtAttributes.BEDROCK_ATMOSPHERE_ATTRIBUTE)) {
            this.pendingBedrockAtmosphere = PipelineSettings.isShaderPackToggleOn(RtAttributes.BEDROCK_ATMOSPHERE_ATTRIBUTE);
        }
        if (this.pendingFogSamples == null
            && PipelineSettings.getShaderPackValue(RtAttributes.VOLUMETRIC_SAMPLES_ATTRIBUTE) != null) {
            this.pendingFogSamples = Math.max(4, Math.min(32,
                PipelineSettings.getShaderPackInt(RtAttributes.VOLUMETRIC_SAMPLES_ATTRIBUTE, 16)));
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
            if (section == Category.DEBUG && !Options.advancedSettings) {
                continue;
            }
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
                case DEBUG -> addDebugOptions();
                case PACK, ADDONS -> {
                }
            }
            // The Debug section only opens with Advanced on, so its attributes are plain rows (addDebugOptions).
            List<SettingsLayout.Group> groups = section == Category.DEBUG ? new ArrayList<>()
                : new ArrayList<>(AdvancedOptions.groups(section, this.pendingAdvanced));
            if (section == Category.SKY_AND_WATER) {
                groups.add(0, skyPathGroup());
            }
            if (section == Category.LIGHTING) {
                groups.add(BlockEmissionSettings.group(this.pendingBlockEmissions));
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
            tunable(Tunable.LOW_LIGHT_BOOST, false), tunable(Tunable.DAY_LOW_LIGHT_LIMIT, false),
            tunable(Tunable.FSR_SHARPNESS, false));
        addHdrOptions();
        if (PipelineSettings.supportsShaderPackToggle(RtAttributes.MOTION_BLUR_ATTRIBUTE)
            && PipelineSettings.supportsShaderPackToggle(RtAttributes.DEPTH_OF_FIELD_ATTRIBUTE)) {
            if (this.pendingMotionBlur == null) {
                this.pendingMotionBlur = PipelineSettings.isShaderPackToggleOn(RtAttributes.MOTION_BLUR_ATTRIBUTE);
            }
            if (this.pendingDepthOfField == null) {
                this.pendingDepthOfField = PipelineSettings.isShaderPackToggleOn(RtAttributes.DEPTH_OF_FIELD_ATTRIBUTE);
            }
            addRows(
                OptionInstance.createBoolean("options.radiante.motion_blur", tooltip("options.radiante.motion_blur"),
                    this.pendingMotionBlur, value -> this.pendingMotionBlur = value),
                OptionInstance.createBoolean("options.radiante.depth_of_field",
                    tooltip("options.radiante.depth_of_field"), this.pendingDepthOfField,
                    value -> this.pendingDepthOfField = value),
                tunable(Tunable.MOTION_BLUR_STRENGTH, false),
                tunable(Tunable.DOF_STRENGTH, false), tunable(Tunable.DOF_RANGE, false),
                packToggle(RtAttributes.DOF_AUTO_FOCUS_ATTRIBUTE, "options.radiante.dof_auto_focus"),
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
            tunable(Tunable.TRANSLUCENCY, false),
            OptionInstance.createBoolean("options.radiante.block_light_sampling",
                tooltip("options.radiante.block_light_sampling"), staged("blockLightSampling", Options.blockLightSampling),
                value -> {
                    stage("blockLightSampling", value);
                    refreshQualityLater();
                }),
            packToggle(RtAttributes.ADAPTIVE_LIGHT_GRID_ATTRIBUTE, "options.radiante.adaptive_light_grid"),
            restirOption(), restirGiOption(),
            OptionInstance.createBoolean("options.radiante.glowing_pixel_light",
                tooltip("options.radiante.glowing_pixel_light"),
                staged("glowingPixelLight", Options.glowingPixelLight), value -> stage("glowingPixelLight", value)),
            OptionInstance.createBoolean("options.radiante.unlit_block_light",
                tooltip("options.radiante.unlit_block_light"),
                staged("unlitBlockLight", Options.unlitBlockLight), value -> stage("unlitBlockLight", value)),
            OptionInstance.createBoolean("options.radiante.held_item_light",
                tooltip("options.radiante.held_item_light"), staged("heldItemLight", Options.heldItemLight),
                value -> stage("heldItemLight", value)),
            new OptionInstance<Integer>("options.radiante.entity_light_reach",
                SettingsRows.<Integer>tooltip("options.radiante.entity_light_reach"),
                (caption, value) -> Component.translatable("options.radiante.blocks_value", caption, value),
                new OptionInstance.IntRange(8, 256, false), this.pendingEntityLightReach,
                value -> this.pendingEntityLightReach = value),
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
            SettingsRows.<Integer>tooltip("options.radiante.cloud_height"),
            (caption, value) -> Component.translatable("options.radiante.blocks_value", caption,
                (value > 0 ? "+" : "") + value),
            new OptionInstance.IntRange(Options.MIN_CLOUD_HEIGHT_OFFSET, Options.MAX_CLOUD_HEIGHT_OFFSET, false),
            staged("cloudHeightOffset", Options.cloudHeightOffset), value -> stage("cloudHeightOffset", value));
        OptionInstance<Boolean> volumetricFog = null;
        OptionInstance<Integer> volumetricStrength = null;
        OptionInstance<Boolean> fogStyle = null;
        if (PipelineSettings.supportsVolumetricFog()) {
            volumetricFog = OptionInstance.createBoolean("options.radiante.volumetric_fog",
                OptionInstance.cachedConstantTooltip(Component.translatable("options.radiante.volumetric_fog.tooltip")),
                this.pendingVolumetricFog, value -> {
                    this.pendingVolumetricFog = value;
                    refreshQualityLater();
                });
            // Bedrock's fog is always its froxel volume: the style is only a choice with Bedrock's fog off.
            if (PipelineSettings.supportsShaderPackToggle(RtAttributes.FROXEL_FOG_ATTRIBUTE) && !bedrockFog()) {
                if (this.pendingFroxelFog == null) {
                    this.pendingFroxelFog = PipelineSettings.isShaderPackToggleOn(RtAttributes.FROXEL_FOG_ATTRIBUTE);
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
        if (PipelineSettings.supportsShaderPackToggle(RtAttributes.CLOUD_SHADOWS_ATTRIBUTE)) {
            if (this.pendingCloudShadows == null) {
                this.pendingCloudShadows = PipelineSettings.isShaderPackToggleOn(RtAttributes.CLOUD_SHADOWS_ATTRIBUTE);
            }
            cloudShadows = OptionInstance.createBoolean("options.radiante.cloud_shadows",
                tooltip("options.radiante.cloud_shadows"), this.pendingCloudShadows,
                value -> this.pendingCloudShadows = value);
        }
        OptionInstance<Boolean> seamlessGlass = null;
        if (PipelineSettings.supportsShaderPackToggle(RtAttributes.SEAMLESS_GLASS_ATTRIBUTE)) {
            if (this.pendingSeamlessGlass == null) {
                this.pendingSeamlessGlass = PipelineSettings.isShaderPackToggleOn(RtAttributes.SEAMLESS_GLASS_ATTRIBUTE);
            }
            seamlessGlass = OptionInstance.createBoolean("options.radiante.seamless_glass",
                tooltip("options.radiante.seamless_glass"), this.pendingSeamlessGlass, value -> {
                    this.pendingSeamlessGlass = value;
                });
        }
        // Which glass seamless glass joins: blocks and panes, blocks only or panes only.
        OptionInstance<String> seamlessScope = null;
        String scopeValue = PipelineSettings.getShaderPackValue(SEAMLESS_SCOPE);
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
            tunable(Tunable.LIGHT_SHAFTS, false), tunable(Tunable.FOG_VEIL_DIMMING, false),
            tunable(Tunable.AERIAL_PERSPECTIVE, false), tunable(Tunable.AERIAL_PERSPECTIVE_START, false),
            sunPathOption(),
            OptionInstance.createBoolean("options.radiante.vanilla_celestial_orientation",
                OptionInstance.cachedConstantTooltip(
                    Component.translatable("options.radiante.vanilla_celestial_orientation.tooltip")),
                staged("vanillaCelestialOrientation", Options.vanillaCelestialOrientation), value -> stage("vanillaCelestialOrientation", value)),
            OptionInstance.createBoolean("options.radiante.rain_wetness",
                tooltip("options.radiante.rain_wetness"), staged("rainWetness", Options.rainWetness),
                value -> stage("rainWetness", value)),
            OptionInstance.createBoolean("options.radiante.snow_accumulation",
                tooltip("options.radiante.snow_accumulation"), staged("snowAccumulation", Options.snowAccumulation),
                value -> stage("snowAccumulation", value)),
            tunable(Tunable.RAIN_WETNESS, false), tunable(Tunable.RAIN_WET_WALLS, false),
            tunable(Tunable.PUDDLES, false), tunable(Tunable.SNOW_FROST, false),
            tunable(Tunable.RAIN_WET_UP_TIME, false),
            tunable(Tunable.RAIN_DRYING_TIME, false), tunable(Tunable.LIGHTNING_FLASH, false),
            packToggle(RtAttributes.WIND_ATTRIBUTE, "options.radiante.wind"), tunable(Tunable.WIND_STRENGTH, false),
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
            tunable(Tunable.WATER_WAVES, false), tunable(Tunable.WATER_RIPPLES, false),
            tunable(Tunable.WATER_DENSITY, false), tunable(Tunable.WATER_GOD_RAYS, false));
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
        return new OptionInstance<Integer>(key, SettingsRows.<Integer>tooltip(key),
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
        if (!PipelineSettings.supportsShaderPackToggle(attribute)) {
            return null;
        }
        boolean value = this.pendingPackToggles.computeIfAbsent(attribute, PipelineSettings::isShaderPackToggleOn);
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
        OptionInstance<Integer> fogSamples = this.pendingFogSamples == null || !PipelineSettings.supportsVolumetricFog() ? null
            : slider("options.radiante.volumetric_fog_samples", 4, 32, this.pendingFogSamples, value -> {
                this.pendingFogSamples = value;
                refreshQualityLater();
            });
        addRows(bounces, tunable(Tunable.MIRROR_BOUNCES, false), tunable(Tunable.MIRROR_SMOOTHNESS, false),
            tunable(Tunable.WATER_TEXTURE, false),
            tunable(Tunable.WATER_CLEAR_TEAL, false),
            tunable(Tunable.PORTAL_TRANSPARENCY, false),
            tunable(Tunable.RAIN_OPACITY, false), tunable(Tunable.RAIN_STREAKS, false),
            packToggle(RtAttributes.CACHE_DEEP_BOUNCES_ATTRIBUTE, "options.radiante.cache_deep_bounces"),
            packToggle(RtAttributes.BEDROCK_ONE_BOUNCE_ATTRIBUTE, "options.radiante.bedrock_one_bounce"),
            packToggle(RtAttributes.TRANSPARENT_CHECKERBOARD_ATTRIBUTE, "options.radiante.transparent_checkerboard"),
            parallax, packToggle(RtAttributes.BEDROCK_MATERIALS_ATTRIBUTE, "options.radiante.bedrock_materials"),
            textureDetailOption(), fogSamples,
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
                value -> stage("parallaxTransparentEdges", value)));
    }

    /**
     * The Debug section: what shows the renderer's inner workings, and what gets its state out of the game for a bug
     * report. None of it changes the look for good: the debug view and the HDR one are not saved.
     */
    private void addDebugOptions() {
        addRows(debugViewRow());
        for (SettingsLayout.Group group : AdvancedOptions.groups(Category.DEBUG, this.pendingAdvanced)) {
            this.page.addAll(group.options());
        }
        // Tints the SDR parts of the picture, so it says nothing while the output is not HDR.
        if (staged("hdrOutput", Options.hdrOutput)) {
            addRows(OptionInstance.createBoolean("options.radiante.hdr_debug_view",
                tooltip("options.radiante.hdr_debug_view"), staged("hdrDebugView", Options.hdrDebugView),
                value -> stage("hdrDebugView", value)));
        }
        addRows(OptionInstance.createBoolean("options.radiante.gpu_timings", tooltip("options.radiante.gpu_timings"),
                this.pendingGpuTimings != null ? this.pendingGpuTimings : RadianteDebugEntries.gpuTimingsShown(),
                value -> this.pendingGpuTimings = value),
            OptionInstance.createBoolean("options.radiante.debug_logging",
                tooltip("options.radiante.debug_logging"), this.pendingDebugLogging,
                value -> this.pendingDebugLogging = value),
            actionRow("options.radiante.debug_report", () -> {
                DebugReport.copy(this.minecraft);
                net.minecraft.client.gui.components.toasts.SystemToast.addOrUpdate(this.minecraft.gui.toastManager(),
                    net.minecraft.client.gui.components.toasts.SystemToast.SystemToastId.PERIODIC_NOTIFICATION,
                    Component.translatable("options.radiante.debug_report.done"), null);
            }),
            actionRow("options.radiante.ab_capture", () -> {
                DebugReport.captureSoon(this.minecraft);
                onClose();
            }),
            actionRow("options.radiante.open_logs", () -> VersionCompat.openFolder(
                this.minecraft.gameDirectory.toPath().resolve("logs"))),
            actionRow("options.radiante.open_shader_packs", () -> {
                java.nio.file.Path folder = com.g2806.radiante.client.pipeline.ShaderPackLibrary.folder();
                if (folder != null) {
                    VersionCompat.openFolder(folder);
                }
            }));
    }

    /** The picker of debug views: off, and the ones the pack in use lists, its own among them. */
    private OptionInstance<String> debugViewRow() {
        String current = this.pendingDebugView != null ? this.pendingDebugView : Pipeline.debugView();
        List<String> views = new ArrayList<>();
        views.add(Pipeline.DEBUG_VIEW_OFF);
        views.addAll(Pipeline.availableDebugViews());
        if (!views.contains(current)) {
            views.add(current);
        }
        String key = "options.radiante.debug_view";
        return new OptionInstance<>(key, value -> {
            Component view = AdvancedOptions.known(key + ".value." + value + ".tooltip");
            return net.minecraft.client.gui.components.Tooltip.create(view == null
                ? Component.translatable(key + ".tooltip")
                : Component.empty().append(Component.translatable(key + ".tooltip")).append("\n\n").append(view));
        }, (caption, value) -> {
            Component name = AdvancedOptions.known(key + ".value." + value);
            return Component.empty().append(caption).append(": ")
                .append(name != null ? name : Component.literal(AdvancedOptions.pretty(value)));
        }, new OptionInstance.Enum<>(List.copyOf(views), com.mojang.serialization.Codec.STRING), current,
            value -> this.pendingDebugView = value);
    }

    /**
     * A row that does something instead of holding a value: a click flips a switch nobody sees, and the switch shows
     * the action's name whichever way it stands.
     */
    private static OptionInstance<Boolean> actionRow(String key, Runnable action) {
        Component word = Component.translatable(key + ".action");
        return OptionInstance.createBoolean(key, tooltip(key), (caption, value) -> word, false, value -> action.run());
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
        // The guided way to set the two sliders below; it needs the HDR window, which a restart may still have to give.
        if (com.g2806.radiante.client.hdr.HdrDisplay.isActive()) {
            addRows(toggle, actionRow("options.radiante.hdr_calibration", this::openHdrCalibration));
        } else {
            addRows(toggle);
        }
        addRows(nitsSlider("options.radiante.hdr_peak", 200, 4000, staged("hdrPeakNits", Options.hdrPeakNits),
                value -> stage("hdrPeakNits", value)),
            nitsSlider("options.radiante.hdr_paper_white", 80, 400, staged("hdrPaperWhiteNits", Options.hdrPaperWhiteNits),
                value -> stage("hdrPaperWhiteNits", value)));
    }

    private void openHdrCalibration() {
        // Back from it, a new screen carries the choices, with the sliders at what was calibrated (null: cancelled).
        this.applied = true;
        this.minecraft.gui.setScreen(new HdrCalibrationScreen(this, staged("hdrPeakNits", Options.hdrPeakNits),
            staged("hdrPaperWhiteNits", Options.hdrPaperWhiteNits), (peak, paperWhite) -> {
                if (peak != null) {
                    stage("hdrPeakNits", peak);
                    stage("hdrPaperWhiteNits", paperWhite);
                }
                reopenWithSameChoices();
            }));
    }

    private static OptionInstance<Integer> nitsSlider(String key, int min, int max, int current,
        java.util.function.Consumer<Integer> onChange) {
        return new OptionInstance<Integer>(key, SettingsRows.<Integer>tooltip(key),
            (caption, value) -> Component.translatable("options.radiante.nits_value", caption, value),
            new OptionInstance.IntRange(min, max, false), Math.max(min, Math.min(max, current)), onChange::accept);
    }
}
