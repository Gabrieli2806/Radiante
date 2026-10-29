package com.g2806.radiante.client.gui;

import java.util.Map;

/**
 * How much an option costs in frame time, shown under its description as Sodium does. Measured where the code
 * comments say so (QualityPreset); the rest is what the option does per pixel. Options not listed cost nothing
 * noticeable, and say so.
 */
enum OptionImpact {
    NONE("options.radiante.impact.none", 0xFF909894),
    LOW("options.radiante.impact.low", 0xFF7FD17F),
    MEDIUM("options.radiante.impact.medium", 0xFFE8C860),
    HIGH("options.radiante.impact.high", 0xFFE86060),
    VARIES("options.radiante.impact.varies", 0xFFB0B0B0);

    final String key;
    final int color;

    OptionImpact(String key, int color) {
        this.key = key;
        this.color = color;
    }

    private static final Map<String, OptionImpact> BY_OPTION = Map.ofEntries(
        Map.entry("options.radiante.quality", VARIES),
        Map.entry("options.radiante.preset", HIGH),
        Map.entry("options.radiante.dlss_mode", HIGH),
        Map.entry("options.radiante.ray_reconstruction", MEDIUM),
        Map.entry("options.radiante.render_scale", HIGH),
        Map.entry("options.radiante.output_resolution", HIGH),
        Map.entry("options.radiante.frame_generation", VARIES),
        Map.entry("options.radiante.ray_bounces", HIGH),
        Map.entry("options.radiante.volumetric_fog", MEDIUM),
        Map.entry("options.radiante.volumetric_fog_samples", MEDIUM),
        Map.entry("options.radiante.cloud_mode", MEDIUM),
        Map.entry("options.radiante.block_light_sampling", MEDIUM),
        Map.entry("options.radiante.restir", MEDIUM),
        Map.entry("options.radiante.parallax", LOW),
        Map.entry("options.radiante.motion_blur", LOW),
        Map.entry("options.radiante.depth_of_field", LOW),
        Map.entry("options.radiante.tunable.dof_strength", LOW),
        Map.entry("options.radiante.tunable.cloud_quality", HIGH),
        Map.entry("options.radiante.tunable.mirror_bounces", VARIES),
        Map.entry("options.radiante.tunable.fog_distance", MEDIUM),
        Map.entry("options.radiante.tunable.light_shafts", LOW),
        Map.entry("options.radiante.tunable.aerial_perspective", NONE),
        Map.entry("options.radiante.tunable.water_god_rays", LOW),
        Map.entry("options.radiante.chunk_building_threads", VARIES),
        Map.entry("options.radiante.hdr_output", LOW),
        Map.entry("options.radiante.upscaler_mode", HIGH),
        Map.entry("options.radiante.ray_tracing", HIGH),
        Map.entry("options.radiante.frame_generation_backend", VARIES),
        Map.entry("options.radiante.fog_style", MEDIUM),
        Map.entry("options.radiante.cloud_shadows", LOW),
        Map.entry("options.radiante.held_item_light", LOW),
        Map.entry("options.radiante.glowing_pixel_light", LOW),
        Map.entry("options.radiante.first_person_shadow", LOW),
        Map.entry("options.radiante.ser", VARIES),
        Map.entry("options.radiante.cache_deep_bounces", HIGH),
        Map.entry("options.radiante.bedrock_one_bounce", HIGH),
        Map.entry("options.radiante.transparent_checkerboard", MEDIUM),
        Map.entry("options.radiante.bedrock_materials", LOW),
        Map.entry("options.radiante.collect_chunk_emission", MEDIUM),
        Map.entry("options.radiante.chunk_building_auto", VARIES),
        Map.entry("options.radiante.chunk_building_batch_size", VARIES),
        Map.entry("options.radiante.chunk_building_total_batches", VARIES),
        Map.entry("options.radiante.mod_overlays", LOW),
        Map.entry("options.radiante.debug_logging", LOW),
        Map.entry("options.radiante.tunable.dof_range", LOW),
        Map.entry("options.radiante.tunable.motion_blur_strength", LOW),
        Map.entry("options.radiante.tunable.glass_reflections", LOW),
        Map.entry("options.radiante.tunable.water_texture", LOW),
        Map.entry("options.radiante.tunable.portal_transparency", LOW),
        Map.entry("options.radiante.tunable.rain_opacity", LOW),
        Map.entry("options.radiante.wind", LOW),
        Map.entry("options.radiante.tunable.water_clear_teal", LOW),
        Map.entry("options.radiante.tunable.underwater_camera_effects", LOW));

    static OptionImpact of(String optionKey) {
        // Advanced rows without a translation have a literal caption and so no key: nothing is known of their cost.
        if (optionKey == null || !optionKey.startsWith("options.radiante.")) {
            return null;
        }
        return BY_OPTION.getOrDefault(optionKey, NONE);
    }
}
