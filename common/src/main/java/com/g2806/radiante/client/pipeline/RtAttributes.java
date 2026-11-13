package com.g2806.radiante.client.pipeline;

/**
 * The names of the ray tracing module's attributes the mod reads or sets by name: the shader pack's switches and
 * values, for {@link Pipeline#setShaderPackToggle}, {@link Pipeline#getShaderPackValue} and their kin. The cloud mode
 * and volumetric fog, which have setters of their own, are named in {@link Pipeline} with them, as are the upscalers'
 * attributes. {@code AdvancedOptions} takes every attribute named here or there as one the settings already have a row
 * for, and leaves it out of Advanced.
 */
public final class RtAttributes {

    private RtAttributes() {
    }

    public static final String MOTION_BLUR_ATTRIBUTE =
        "render_pipeline.module.ray_tracing.attribute.post_enable_motion_blur";
    public static final String DOF_AUTO_FOCUS_ATTRIBUTE =
        "render_pipeline.module.ray_tracing.attribute.post_dof_auto_focus";
    public static final String DEPTH_OF_FIELD_ATTRIBUTE =
        "render_pipeline.module.ray_tracing.attribute.post_enable_dof";
    /** ReSTIR reuse of block light samples across frames and pixels (needs block light sampling). */
    public static final String RESTIR_ATTRIBUTE = "render_pipeline.module.ray_tracing.attribute.restir";
    /** ReSTIR GI: the first diffuse bounce resampled across frames and neighbouring pixels. */
    public static final String RESTIR_GI_ATTRIBUTE = "render_pipeline.module.ray_tracing.attribute.restir_gi";
    /** Bedrock packs' MER maps read as authored instead of through their LabPBR conversion. */
    public static final String BEDROCK_MATERIALS_ATTRIBUTE =
        "render_pipeline.module.ray_tracing.attribute.bedrock_materials";
    /**
     * Vanilla's fixed face shading kept in the colour of block faces (tops full, sides 0.8 and 0.6, bottoms half).
     * Vanilla darkens faces that way for want of real light; traced light shades them already, and with it kept a
     * ceiling showed a fifth of its colour (the half is applied before the colour is decoded), as a different
     * material from the same block's top. Bedrock RTX has no such shading.
     */
    public static final String FACE_SHADING_ATTRIBUTE = "render_pipeline.module.ray_tracing.attribute.face_shading";
    /** Past the first bounce, diffuse light taken from the radiance cache instead of traced further. */
    public static final String CACHE_DEEP_BOUNCES_ATTRIBUTE =
        "render_pipeline.module.ray_tracing.attribute.cache_deep_bounces";
    /** One bounce traced everywhere, the rest of the light from the radiance cache, as Bedrock RTX does. */
    public static final String BEDROCK_ONE_BOUNCE_ATTRIBUTE =
        "render_pipeline.module.ray_tracing.attribute.bedrock_one_bounce";
    /** Water and glass trace their reflection or their refraction per pixel on a checkerboard, not both. */
    public static final String TRANSPARENT_CHECKERBOARD_ATTRIBUTE =
        "render_pipeline.module.ray_tracing.attribute.transparent_checkerboard";
    /**
     * The path tracer writes the light of what the camera sees apart for NRD (its glow and the sun's light left out of
     * what NRD blurs, the bounce's length as its hit distance). Not a setting: every build sets it to whether the
     * pipeline denoises with NRD (syncDenoiserSignals).
     */
    public static final String NRD_SIGNALS_ATTRIBUTE = "render_pipeline.module.ray_tracing.attribute.nrd_signals";
    /** Glass blocks without the frame vanilla draws around each one, so neighbours join into one sheet. */
    public static final String SEAMLESS_GLASS_ATTRIBUTE =
        "render_pipeline.module.ray_tracing.attribute.seamless_glass";
    /** Volumetric fog made with a lit 3D grid instead of a march per pixel. */
    public static final String FROXEL_FOG_ATTRIBUTE = "render_pipeline.module.ray_tracing.attribute.froxel_fog";
    /** Volumetric clouds shade the ground under them (only with Volumetric clouds). */
    public static final String CLOUD_SHADOWS_ATTRIBUTE =
        "render_pipeline.module.ray_tracing.attribute.volumetric_cloud_cast_shadow";
    /**
     * Leaves, grass, flowers, vines and water plants sway in the wind (util/wind.glsl). Switching it remeshes the
     * world: sections carry their wind codes only while it is on, so with it off nothing is moved and nothing is refit.
     */
    public static final String WIND_ATTRIBUTE = "render_pipeline.module.ray_tracing.attribute.wind";
    /** Pixel Rain: puddles, rain rings, wetness and splashes in pixels of a sixteenth of a block. */
    public static final String RAIN_PIXELS_ATTRIBUTE = "render_pipeline.module.ray_tracing.attribute.rain_pixels";
    /** How far the wind moves foliage, 1 as designed; live, without remeshing. */
    public static final String WIND_STRENGTH_ATTRIBUTE = "render_pipeline.module.ray_tracing.attribute.wind_strength";

    /** Light bounces per path. Each one after the first is another ray per pixel. */
    public static final String RAY_BOUNCES_ATTRIBUTE = "render_pipeline.module.ray_tracing.attribute.num_ray_bounces";
    /** Carved (parallax) surfaces of resource packs with height maps. */
    public static final String PARALLAX_ATTRIBUTE = "render_pipeline.module.ray_tracing.attribute.enable_parallax";
    public static final String BEDROCK_ATMOSPHERE_ATTRIBUTE =
        "render_pipeline.module.ray_tracing.attribute.bedrock_atmosphere";
    /** Steps the volumetric fog is marched in along each ray. */
    public static final String VOLUMETRIC_SAMPLES_ATTRIBUTE =
        "render_pipeline.module.ray_tracing.attribute.volumetric_light_samples";
}
