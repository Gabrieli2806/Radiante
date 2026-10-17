package com.g2806.radiante.client.pipeline;

public enum Presets {
    RT_DLSSRR("RT_DLSSRR", "render_pipeline.preset.rt_dlss"),
    RT_NRD("RT_NRD", "render_pipeline.preset.rt_nrd"),
    RT_NRD_FSR("RT_NRD_FSR", "render_pipeline.preset.rt_nrd_fsr"),
    RT_NRD_XESS("RT_NRD_XESS", "render_pipeline.preset.rt_nrd_xess"),
    // DLSS with Ray Reconstruction turned off: NRD denoises and DLSS only upscales. Not a pipeline of its own in the
    // settings, where it is the DLSS pipeline with the Ray Reconstruction option off.
    RT_NRD_DLSS("RT_NRD_DLSS", "render_pipeline.preset.rt_nrd_dlss"),
    ;

    public final String name;
    public final String key;

    Presets(String name, String key) {
        this.name = name;
        this.key = key;
    }

    /** Whether DLSS upscales this pipeline, with or without Ray Reconstruction. */
    public boolean usesDlss() {
        return this == RT_DLSSRR || this == RT_NRD_DLSS;
    }

    /** The preset with this translation key, or null for an unknown one. */
    public static Presets byKey(String key) {
        for (Presets preset : values()) {
            if (preset.key.equals(key)) {
                return preset;
            }
        }
        return null;
    }
}
