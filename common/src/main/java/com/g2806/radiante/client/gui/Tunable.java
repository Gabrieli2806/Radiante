package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.pipeline.Pipeline;
import java.util.List;
import java.util.Locale;

/**
 * A numeric pipeline setting the Radiante screen shows as a slider: a shader pack attribute (module null) or another
 * module's. The slider runs in whole steps; the stored value is {@code steps * unit}. Several are also part of the
 * picture style (see {@link PictureStyle}).
 */
enum Tunable {
    SATURATION(Pipeline.TONE_MAPPING_MODULE_NAME, "render_pipeline.module.tone_mapping.attribute.saturation",
        0.01, 50, 200, 100, Format.PERCENT),
    EXPOSURE_ADAPTATION(Pipeline.TONE_MAPPING_MODULE_NAME,
        "render_pipeline.module.tone_mapping.attribute.exposure_adaptation", 0.01, 0, 100, 100, Format.PERCENT),
    EXPOSURE_BIAS(Pipeline.TONE_MAPPING_MODULE_NAME, "render_pipeline.module.tone_mapping.attribute.exposure_bias",
        0.1, -30, 30, 0, Format.EV),
    // One slider for both directions: how fast the exposure follows going into the light and into the dark.
    EXPOSURE_SPEED(Pipeline.TONE_MAPPING_MODULE_NAME, "render_pipeline.module.tone_mapping.attribute.exposure_up_speed",
        "render_pipeline.module.tone_mapping.attribute.exposure_down_speed", 0.08, 5, 250, 100, Format.PERCENT),
    DOF_STRENGTH(null, "render_pipeline.module.ray_tracing.attribute.post_dof_max_radius", 0.04, 25, 400, 100,
        Format.PERCENT),
    DOF_RANGE(null, "render_pipeline.module.ray_tracing.attribute.post_dof_focus_range", 0.1, 10, 400, 100,
        Format.PERCENT),
    DOF_FOCUS_DISTANCE(null, "render_pipeline.module.ray_tracing.attribute.post_dof_focus_distance", 1.0, 1, 256, 12,
        Format.BLOCKS),
    MOTION_BLUR_STRENGTH(null, "render_pipeline.module.ray_tracing.attribute.post_motion_blur_strength", 0.0135, 10,
        220, 100, Format.PERCENT),
    FSR_SHARPNESS(Pipeline.FSR_MODULE_NAME, "render_pipeline.module.fsr_upscaler.attribute.sharpness", 0.01, 0, 100,
        70, Format.PERCENT),
    // The most the exposure may brighten a dark scene; 100% is Bedrock RTX's 32, which lets a room lit through a
    // window come up as bright as in Bedrock.
    LOW_LIGHT_BOOST(Pipeline.TONE_MAPPING_MODULE_NAME, "render_pipeline.module.tone_mapping.attribute.max_exposure",
        0.32, 1, 200, 100, Format.PERCENT),
    CLOUD_COVERAGE(null, "render_pipeline.module.ray_tracing.attribute.volumetric_cloud_coverage", 0.01, 0, 100, 50,
        Format.PERCENT),
    CLOUD_DENSITY(null, "render_pipeline.module.ray_tracing.attribute.volumetric_cloud_density", 0.01, 0, 400, 100,
        Format.PERCENT),
    CLOUD_QUALITY(null, "render_pipeline.module.ray_tracing.attribute.volumetric_cloud_view_steps", 1.0, 8, 256, 64,
        Format.NUMBER),
    FOG_DISTANCE(null, "render_pipeline.module.ray_tracing.attribute.volumetric_light_max_distance", 1.0, 48, 192,
        128, Format.BLOCKS),
    // Reflections inside reflections allowed on top of the light bounces.
    MIRROR_BOUNCES(null, "render_pipeline.module.ray_tracing.attribute.mirror_extra_bounces", 1.0, 0, 64, 8,
        Format.NUMBER),
    // How far the textures' stored colours are converted to linear light: 100% is the full conversion (2.2).
    TEXTURE_CONTRAST(null, "render_pipeline.module.ray_tracing.attribute.albedo_gamma", 0.022, 46, 109, 100,
        Format.PERCENT),
    // How much of the sky a window shows on top of what is behind it.
    GLASS_REFLECTIONS(null, "render_pipeline.module.ray_tracing.attribute.glass_reflection", 0.01, 0, 100, 100,
        Format.PERCENT),
    // How much of the pack's water texture shows over the surface: Bedrock RTX's faint pattern, or clear water at 0.
    WATER_TEXTURE(null, "render_pipeline.module.ray_tracing.attribute.water_texture_layer", 0.01, 0, 100, 10,
        Format.PERCENT),
    // How far each biome's water is moved towards clear teal water (red absorbed first, green and blue far-reaching).
    WATER_CLEAR_TEAL(null, "render_pipeline.module.ray_tracing.attribute.water_clear_teal", 0.01, 0, 100, 0,
        Format.PERCENT),
    // How much of the pack's see-through share a nether portal keeps: 100% as the pack authors it, 0% opaque.
    PORTAL_TRANSPARENCY(null, "render_pipeline.module.ray_tracing.attribute.portal_transparency", 0.01, 0, 100, 50,
        Format.PERCENT),
    // How much of a rain streak shows: thin, nearly clear falling water at the default, an opaque sheet at 100%.
    RAIN_OPACITY(null, "render_pipeline.module.ray_tracing.attribute.rain_opacity", 0.01, 0, 100, 0,
        Format.PERCENT),
    // How much a rain streak catches the light around it: the overcast sky over it.
    RAIN_STREAKS(null, "render_pipeline.module.ray_tracing.attribute.rain_streak_brightness", 0.01, 0, 200, 100,
        Format.PERCENT),
    // How much standing water rain leaves on open flat block tops (with Wet Ground in Rain on); full from 50%.
    PUDDLES(null, "render_pipeline.module.ray_tracing.attribute.rain_puddles", 0.01, 0, 100, 50, Format.PERCENT),
    // How long steady rain takes to soak the ground through, and how long the ground takes to dry after it, in seconds.
    RAIN_WET_UP_TIME(null, "render_pipeline.module.ray_tracing.attribute.rain_wet_up_time", 1.0, 10, 600, 150,
        Format.SECONDS),
    RAIN_DRYING_TIME(null, "render_pipeline.module.ray_tracing.attribute.rain_drying_time", 1.0, 30, 600, 180,
        Format.SECONDS),
    // How strongly wet surfaces darken and shine.
    RAIN_WETNESS(null, "render_pipeline.module.ray_tracing.attribute.rain_wetness_strength", 0.01, 0, 200, 100,
        Format.PERCENT),
    // How wet walls and other upright faces open to the rain get, against the ground.
    RAIN_WET_WALLS(null, "render_pipeline.module.ray_tracing.attribute.rain_wet_walls", 0.01, 0, 100, 50,
        Format.PERCENT),
    // How brightly a lightning flash lights the world in a thunderstorm.
    LIGHTNING_FLASH(null, "render_pipeline.module.ray_tracing.attribute.lightning_flash", 0.01, 0, 200, 100,
        Format.PERCENT),
    // How far the wind moves leaves and plants, while Wind is on.
    WIND_STRENGTH(null, Pipeline.WIND_STRENGTH_ATTRIBUTE, 0.01, 0, 200, 100, Format.PERCENT),
    STARS(Pipeline.POST_RENDER_MODULE_NAME, "render_pipeline.module.post_render.attribute.star_count", 100.0, 1, 200,
        30, Format.NUMBER),
    BOUNCE_LIGHT(null, "render_pipeline.module.ray_tracing.attribute.bounce_light_boost", 0.01, 50, 200, 150,
        Format.PERCENT),
    SKY_LIGHT(null, "render_pipeline.module.ray_tracing.attribute.sky_light_boost", 0.01, 50, 400, 180, Format.PERCENT),
    // How much sun leaves and plants let through from behind, and how far round ice and snow it wraps.
    TRANSLUCENCY(null, "render_pipeline.module.ray_tracing.attribute.translucency", 0.01, 0, 200, 100, Format.PERCENT),
    SUN_GLOW(null, "render_pipeline.module.ray_tracing.attribute.sun_glow", 0.0008, 0, 300, 100, Format.PERCENT),
    LIGHT_SHAFTS(null, "render_pipeline.module.ray_tracing.attribute.light_shafts", 0.00015, 0, 400, 100,
        Format.PERCENT),
    // How much the light shafts' air dims what is behind it, as a share of the light it scatters: distant lava and
    // lights fade into the fog instead of keeping their whole glow under a pale veil.
    FOG_VEIL_DIMMING(null, "render_pipeline.module.ray_tracing.attribute.fog_veil_dimming", 0.01, 0, 100, 25,
        Format.PERCENT),
    // The air's own haze over the distance: far terrain (Distant Horizons' too) fading into the sky behind it.
    AERIAL_PERSPECTIVE(null, "render_pipeline.module.ray_tracing.attribute.aerial_perspective", 0.01, 0, 400, 100,
        Format.PERCENT),
    // How far from the camera the haze begins: nothing nearer, faded in over the next half of that distance.
    AERIAL_PERSPECTIVE_START(null, "render_pipeline.module.ray_tracing.attribute.aerial_perspective_start", 1.0, 0,
        1024, 128, Format.BLOCKS),
    WATER_WAVES(null, "render_pipeline.module.ray_tracing.attribute.water_wave_strength", 0.0035, 0, 400, 100,
        Format.PERCENT),
    WATER_DENSITY(null, "render_pipeline.module.ray_tracing.attribute.water_density", 0.01, 0, 400, 100,
        Format.PERCENT),
    WATER_GOD_RAYS(null, "render_pipeline.module.ray_tracing.attribute.water_god_rays", 0.0012, 0, 400, 100,
        Format.PERCENT),
    // How bright the picture is kept by day and at night, against what the light meter would make it.
    EXPOSURE_DAY(null, "render_pipeline.module.ray_tracing.attribute.exposure_day_scale", 0.01, 10, 150, 48,
        Format.PERCENT),
    EXPOSURE_NIGHT(null, "render_pipeline.module.ray_tracing.attribute.exposure_night_scale", 0.01, 10, 150, 65,
        Format.PERCENT),
    // How far the eye may open by day at most: low keeps sealed rooms dark, high brightens rooms lit by a window.
    DAY_LOW_LIGHT_LIMIT(null, "render_pipeline.module.ray_tracing.attribute.exposure_day_max", 0.01, 50, 3200, 250,
        Format.PERCENT),
    // Up to how rough a metal is still drawn as a clean mirror.
    MIRROR_SMOOTHNESS(null, "render_pipeline.module.ray_tracing.attribute.mirror_max_roughness", 0.001, 0, 200, 30,
        Format.PERCENT);

    /** NUMBER is a whole number, written without a decimal point; BLOCKS is a distance in blocks. */
    enum Format { PERCENT, EV, NUMBER, BLOCKS, SECONDS }

    final String module;
    final String attribute;
    /** A second attribute of the same module kept at the same value, or null. */
    final String twin;
    final double unit;
    final int min;
    final int max;
    final int defaultSteps;
    final Format format;

    Tunable(String module, String attribute, double unit, int min, int max, int defaultSteps, Format format) {
        this(module, attribute, null, unit, min, max, defaultSteps, format);
    }

    Tunable(String module, String attribute, String twin, double unit, int min, int max, int defaultSteps,
        Format format) {
        this.module = module;
        this.attribute = attribute;
        this.twin = twin;
        this.unit = unit;
        this.min = min;
        this.max = max;
        this.defaultSteps = defaultSteps;
        this.format = format;
    }

    String key() {
        return "options.radiante.tunable." + name().toLowerCase(Locale.ROOT);
    }

    /** The current value in slider steps, or null when the pipeline does not have the setting. */
    Integer read() {
        String value = this.module == null ? Pipeline.getShaderPackValue(this.attribute)
            : Pipeline.getModuleValue(this.module, this.attribute);
        if (value == null) {
            return null;
        }
        try {
            return clamp((int) Math.round(Double.parseDouble(value.trim()) / this.unit));
        } catch (NumberFormatException notANumber) {
            return this.defaultSteps;
        }
    }

    /** Writes the value; true when it changed (see Pipeline.takeStructuralChange for whether that needs a rebuild). */
    boolean write(int steps) {
        String value = this.format == Format.NUMBER ? String.valueOf(Math.round(clamp(steps) * this.unit))
            : String.format(Locale.ROOT, "%.5f", clamp(steps) * this.unit).replaceAll("0+$", "")
                .replaceAll("\\.$", ".0");
        String current = this.module == null ? Pipeline.getShaderPackValue(this.attribute)
            : Pipeline.getModuleValue(this.module, this.attribute);
        try {
            if (current != null) {
                double stored = Double.parseDouble(current.trim());
                if (Math.abs(stored - clamp(steps) * this.unit) < this.unit * 0.5) {
                    return false;
                }
                // A slider left where it was shown is not written: a stored value outside the slider's range would
                // otherwise be clamped on every
                // Apply. A value that does not parse is still replaced.
                if (read() != null && read() == clamp(steps)) {
                    return false;
                }
            }
        } catch (NumberFormatException ignored) {
            // Written below.
        }
        boolean changed = this.module == null ? Pipeline.setShaderPackValue(this.attribute, value)
            : Pipeline.setModuleValue(this.module, this.attribute, value);
        if (this.twin != null) {
            changed |= this.module == null ? Pipeline.setShaderPackValue(this.twin, value)
                : Pipeline.setModuleValue(this.module, this.twin, value);
        }
        return changed;
    }

    int clamp(int steps) {
        return Math.max(this.min, Math.min(this.max, steps));
    }

    static final List<Tunable> ALL = List.of(values());
}
